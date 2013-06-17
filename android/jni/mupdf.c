#include <jni.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>
#include <android/bitmap.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef NDK_PROFILER
#include "prof.h"
#endif

#include "fitz.h"
#include "fitz-internal.h"
#include "mupdf.h"

#define JNI_FN(A) Java_com_artifex_mupdfdemo_ ## A
#define PACKAGENAME "com/artifex/mupdfdemo"

#define LOG_TAG "libmupdf"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGT(...) __android_log_print(ANDROID_LOG_INFO,"alert",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

/* Set to 1 to enable debug log traces. */
#define DEBUG 0

/* Enable to log rendering times (render each frame 100 times and time) */
#undef TIME_DISPLAY_LIST

#define MAX_SEARCH_HITS (500)
#define NUM_CACHE (3)

enum
{
	NONE,
	TEXT,
	LISTBOX,
	COMBOBOX
};

typedef struct rect_node_s rect_node;

struct rect_node_s
{
	fz_rect rect;
	rect_node *next;
};

typedef struct
{
	int number;
	int width;
	int height;
	fz_rect media_box;
	fz_page *page;
	rect_node *changed_rects;
	rect_node *hq_changed_rects;
	fz_display_list *page_list;
	fz_display_list *annot_list;
} page_cache;

typedef struct globals_s globals;

struct globals_s
{
	fz_colorspace *colorspace;
	fz_document *doc;
	int resolution;
	fz_context *ctx;
	fz_rect *hit_bbox;
	int current;
	char *current_path;

	page_cache pages[NUM_CACHE];

	int alerts_initialised;
	// fin_lock and fin_lock2 are used during shutdown. The two waiting tasks
	// show_alert and waitForAlertInternal respectively take these locks while
	// waiting. During shutdown, the conditions are signaled and then the fin_locks
	// are taken momentarily to ensure the blocked threads leave the controlled
	// area of code before the mutexes and condition variables are destroyed.
	pthread_mutex_t fin_lock;
	pthread_mutex_t fin_lock2;
	// alert_lock is the main lock guarding the variables directly below.
	pthread_mutex_t alert_lock;
	// Flag indicating if the alert system is active. When not active, both
	// show_alert and waitForAlertInternal return immediately.
	int alerts_active;
	// Pointer to the alert struct passed in by show_alert, and valid while
	// show_alert is blocked.
	fz_alert_event *current_alert;
	// Flag and condition varibles to signal a request is present and a reply
	// is present, respectively. The condition variables alone are not sufficient
	// because of the pthreads permit spurious signals.
	int alert_request;
	int alert_reply;
	pthread_cond_t alert_request_cond;
	pthread_cond_t alert_reply_cond;

	// For the buffer reading mode, we need to implement stream reading, which
	// needs access to the following.
	JNIEnv *env;
	jclass thiz;
};

static jfieldID global_fid;
static jfieldID buffer_fid;

static void drop_changed_rects(fz_context *ctx, rect_node **nodePtr)
{
	rect_node *node = *nodePtr;
	while (node)
	{
		rect_node *tnode = node;
		node = node->next;
		fz_free(ctx, tnode);
	}

	*nodePtr = NULL;
}

static void drop_page_cache(globals *glo, page_cache *pc)
{
	fz_context  *ctx = glo->ctx;
	fz_document *doc = glo->doc;

	LOGI("Drop page %d", pc->number);
	fz_free_display_list(ctx, pc->page_list);
	pc->page_list = NULL;
	fz_free_display_list(ctx, pc->annot_list);
	pc->annot_list = NULL;
	fz_free_page(doc, pc->page);
	pc->page = NULL;
	drop_changed_rects(ctx, &pc->changed_rects);
	drop_changed_rects(ctx, &pc->hq_changed_rects);
}

static void dump_annotation_display_lists(globals *glo)
{
	fz_context *ctx = glo->ctx;
	int i;

	for (i = 0; i < NUM_CACHE; i++) {
		fz_free_display_list(ctx, glo->pages[i].annot_list);
		glo->pages[i].annot_list = NULL;
	}
}

static void show_alert(globals *glo, fz_alert_event *alert)
{
	pthread_mutex_lock(&glo->fin_lock2);
	pthread_mutex_lock(&glo->alert_lock);

	LOGT("Enter show_alert: %s", alert->title);
	alert->button_pressed = 0;

	if (glo->alerts_active)
	{
		glo->current_alert = alert;
		glo->alert_request = 1;
		pthread_cond_signal(&glo->alert_request_cond);

		while (glo->alerts_active && !glo->alert_reply)
			pthread_cond_wait(&glo->alert_reply_cond, &glo->alert_lock);
		glo->alert_reply = 0;
		glo->current_alert = NULL;
	}

	LOGT("Exit show_alert");

	pthread_mutex_unlock(&glo->alert_lock);
	pthread_mutex_unlock(&glo->fin_lock2);
}

static void event_cb(fz_doc_event *event, void *data)
{
	globals *glo = (globals *)data;

	switch (event->type)
	{
	case FZ_DOCUMENT_EVENT_ALERT:
		show_alert(glo, fz_access_alert_event(event));
		break;
	}
}

static void alerts_init(globals *glo)
{
	fz_interactive *idoc = fz_interact(glo->doc);

	if (!idoc || glo->alerts_initialised)
		return;

	glo->alerts_active = 0;
	glo->alert_request = 0;
	glo->alert_reply = 0;
	pthread_mutex_init(&glo->fin_lock, NULL);
	pthread_mutex_init(&glo->fin_lock2, NULL);
	pthread_mutex_init(&glo->alert_lock, NULL);
	pthread_cond_init(&glo->alert_request_cond, NULL);
	pthread_cond_init(&glo->alert_reply_cond, NULL);

	fz_set_doc_event_callback(idoc, event_cb, glo);
	LOGT("alert_init");
	glo->alerts_initialised = 1;
}

static void alerts_fin(globals *glo)
{
	fz_interactive *idoc = fz_interact(glo->doc);
	if (!glo->alerts_initialised)
		return;

	LOGT("Enter alerts_fin");
	if (idoc)
		fz_set_doc_event_callback(idoc, NULL, NULL);

	// Set alerts_active false and wake up show_alert and waitForAlertInternal,
	pthread_mutex_lock(&glo->alert_lock);
	glo->current_alert = NULL;
	glo->alerts_active = 0;
	pthread_cond_signal(&glo->alert_request_cond);
	pthread_cond_signal(&glo->alert_reply_cond);
	pthread_mutex_unlock(&glo->alert_lock);

	// Wait for the fin_locks.
	pthread_mutex_lock(&glo->fin_lock);
	pthread_mutex_unlock(&glo->fin_lock);
	pthread_mutex_lock(&glo->fin_lock2);
	pthread_mutex_unlock(&glo->fin_lock2);

	pthread_cond_destroy(&glo->alert_reply_cond);
	pthread_cond_destroy(&glo->alert_request_cond);
	pthread_mutex_destroy(&glo->alert_lock);
	pthread_mutex_destroy(&glo->fin_lock2);
	pthread_mutex_destroy(&glo->fin_lock);
	LOGT("Exit alerts_fin");
	glo->alerts_initialised = 0;
}

static globals *get_globals(JNIEnv *env, jobject thiz)
{
	globals *glo = (globals *)(void *)((*env)->GetLongField(env, thiz, global_fid));
	glo->env = env;
	glo->thiz = thiz;
	return glo;
}

JNIEXPORT jlong JNICALL
JNI_FN(MuPDFCore_openFile)(JNIEnv * env, jobject thiz, jstring jfilename)
{
	const char *filename;
	globals    *glo;
	fz_context *ctx;
	jclass      clazz;

#ifdef NDK_PROFILER
	monstartup("libmupdf.so");
#endif

	clazz = (*env)->GetObjectClass(env, thiz);
	global_fid = (*env)->GetFieldID(env, clazz, "globals", "J");

	glo = calloc(1, sizeof(*glo));
	if (glo == NULL)
		return 0;
	glo->resolution = 160;
	glo->alerts_initialised = 0;

	filename = (*env)->GetStringUTFChars(env, jfilename, NULL);
	if (filename == NULL)
	{
		LOGE("Failed to get filename");
		free(glo);
		return 0;
	}

	/* 128 MB store for low memory devices. Tweak as necessary. */
	glo->ctx = ctx = fz_new_context(NULL, NULL, 128 << 20);
	if (!ctx)
	{
		LOGE("Failed to initialise context");
		(*env)->ReleaseStringUTFChars(env, jfilename, filename);
		free(glo);
		return 0;
	}

	glo->doc = NULL;
	fz_try(ctx)
	{
		glo->colorspace = fz_device_rgb;

		LOGE("Opening document...");
		fz_try(ctx)
		{
			glo->current_path = fz_strdup(ctx, (char *)filename);
			glo->doc = fz_open_document(ctx, (char *)filename);
			alerts_init(glo);
		}
		fz_catch(ctx)
		{
			fz_throw(ctx, "Cannot open document: '%s'", filename);
		}
		LOGE("Done!");
	}
	fz_catch(ctx)
	{
		LOGE("Failed: %s", ctx->error->message);
		fz_close_document(glo->doc);
		glo->doc = NULL;
		fz_free_context(ctx);
		glo->ctx = NULL;
		free(glo);
		glo = NULL;
	}

	(*env)->ReleaseStringUTFChars(env, jfilename, filename);

	return (jlong)(void *)glo;
}

static int bufferStreamRead(fz_stream *stream, unsigned char *buf, int len)
{
	globals    *glo = (globals *)stream->state;
	JNIEnv     *env = glo->env;
	jbyteArray  array = (jbyteArray)(void *)((*env)->GetObjectField(env, glo->thiz, buffer_fid));
	int         arrayLength = (*env)->GetArrayLength(env, array);

	if (stream->pos > arrayLength)
		stream->pos = arrayLength;
	if (stream->pos < 0)
		stream->pos = 0;
	if (len + stream->pos > arrayLength)
		len = arrayLength - stream->pos;

	(*env)->GetByteArrayRegion(env, array, stream->pos, len, buf);
	(*env)->DeleteLocalRef(env, array);
	return len;
}

static void bufferStreamClose(fz_context *ctx, void *state)
{
	/* Nothing to do */
}

static void bufferStreamSeek(fz_stream *stream, int offset, int whence)
{
	globals    *glo = (globals *)stream->state;
	JNIEnv     *env = glo->env;
	jbyteArray  array = (jbyteArray)(void *)((*env)->GetObjectField(env, glo->thiz, buffer_fid));
	int         arrayLength = (*env)->GetArrayLength(env, array);

	(*env)->DeleteLocalRef(env, array);

	if (whence == 0) /* SEEK_SET */
		stream->pos = offset;
	else if (whence == 1) /* SEEK_CUR */
		stream->pos += offset;
	else if (whence == 2) /* SEEK_END */
		stream->pos = arrayLength + offset;

	if (stream->pos > arrayLength)
		stream->pos = arrayLength;
	if (stream->pos < 0)
		stream->pos = 0;

	stream->rp = stream->bp;
	stream->wp = stream->bp;
}

JNIEXPORT jlong JNICALL
JNI_FN(MuPDFCore_openBuffer)(JNIEnv * env, jobject thiz)
{
	globals    *glo;
	fz_context *ctx;
	jclass      clazz;
	fz_stream *stream;

#ifdef NDK_PROFILER
	monstartup("libmupdf.so");
#endif

	clazz = (*env)->GetObjectClass(env, thiz);
	global_fid = (*env)->GetFieldID(env, clazz, "globals", "J");

	glo = calloc(1, sizeof(*glo));
	if (glo == NULL)
		return 0;
	glo->resolution = 160;
	glo->alerts_initialised = 0;
	glo->env = env;
	glo->thiz = thiz;
	buffer_fid = (*env)->GetFieldID(env, clazz, "fileBuffer", "[B");

	/* 128 MB store for low memory devices. Tweak as necessary. */
	glo->ctx = ctx = fz_new_context(NULL, NULL, 128 << 20);
	if (!ctx)
	{
		LOGE("Failed to initialise context");
		free(glo);
		return 0;
	}

	glo->doc = NULL;
	fz_try(ctx)
	{
		stream = fz_new_stream(ctx, glo, bufferStreamRead, bufferStreamClose);
		stream->seek = bufferStreamSeek;

		glo->colorspace = fz_device_rgb;

		LOGE("Opening document...");
		fz_try(ctx)
		{
			glo->current_path = NULL;
			glo->doc = fz_open_document_with_stream(ctx, "", stream);
			alerts_init(glo);
		}
		fz_catch(ctx)
		{
			fz_throw(ctx, "Cannot open memory document");
		}
		LOGE("Done!");
	}
	fz_catch(ctx)
	{
		LOGE("Failed: %s", ctx->error->message);
		fz_close_document(glo->doc);
		glo->doc = NULL;
		fz_free_context(ctx);
		glo->ctx = NULL;
		free(glo);
		glo = NULL;
	}

	return (jlong)(void *)glo;
}

JNIEXPORT int JNICALL
JNI_FN(MuPDFCore_countPagesInternal)(JNIEnv *env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);

	return fz_count_pages(glo->doc);
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_gotoPageInternal)(JNIEnv *env, jobject thiz, int page)
{
	int i;
	int furthest;
	int furthest_dist = -1;
	float zoom;
	fz_matrix ctm;
	fz_irect bbox;
	page_cache *pc;
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;

	for (i = 0; i < NUM_CACHE; i++)
	{
		if (glo->pages[i].page != NULL && glo->pages[i].number == page)
		{
			/* The page is already cached */
			glo->current = i;
			return;
		}

		if (glo->pages[i].page == NULL)
		{
			/* cache record unused, and so a good one to use */
			furthest = i;
			furthest_dist = INT_MAX;
		}
		else
		{
			int dist = abs(glo->pages[i].number - page);

			/* Further away - less likely to be needed again */
			if (dist > furthest_dist)
			{
				furthest_dist = dist;
				furthest = i;
			}
		}
	}

	glo->current = furthest;
	pc = &glo->pages[glo->current];

	drop_page_cache(glo, pc);

	/* In the event of an error, ensure we give a non-empty page */
	pc->width = 100;
	pc->height = 100;

	pc->number = page;
	LOGE("Goto page %d...", page);
	fz_try(ctx)
	{
		fz_rect rect;
		LOGI("Load page %d", pc->number);
		pc->page = fz_load_page(glo->doc, pc->number);
		zoom = glo->resolution / 72;
		fz_bound_page(glo->doc, pc->page, &pc->media_box);
		fz_scale(&ctm, zoom, zoom);
		rect = pc->media_box;
		fz_round_rect(&bbox, fz_transform_rect(&rect, &ctm));
		pc->width = bbox.x1-bbox.x0;
		pc->height = bbox.y1-bbox.y0;
	}
	fz_catch(ctx)
	{
		LOGE("cannot make displaylist from page %d", pc->number);
	}
}

JNIEXPORT float JNICALL
JNI_FN(MuPDFCore_getPageWidth)(JNIEnv *env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	LOGE("PageWidth=%d", glo->pages[glo->current].width);
	return glo->pages[glo->current].width;
}

JNIEXPORT float JNICALL
JNI_FN(MuPDFCore_getPageHeight)(JNIEnv *env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	LOGE("PageHeight=%d", glo->pages[glo->current].height);
	return glo->pages[glo->current].height;
}

JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_javascriptSupported)(JNIEnv *env, jobject thiz)
{
	return fz_javascript_supported();
}

static void update_changed_rects(globals *glo, page_cache *pc, fz_interactive *idoc)
{
	fz_annot *annot;

	fz_update_page(idoc, pc->page);
	while ((annot = fz_poll_changed_annot(idoc, pc->page)) != NULL)
	{
		/* FIXME: We bound the annot twice here */
		rect_node *node = fz_malloc_struct(glo->ctx, rect_node);
		fz_bound_annot(glo->doc, annot, &node->rect);
		node->next = pc->changed_rects;
		pc->changed_rects = node;

		node = fz_malloc_struct(glo->ctx, rect_node);
		fz_bound_annot(glo->doc, annot, &node->rect);
		node->next = pc->hq_changed_rects;
		pc->hq_changed_rects = node;
	}
}

JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_drawPage)(JNIEnv *env, jobject thiz, jobject bitmap,
		int pageW, int pageH, int patchX, int patchY, int patchW, int patchH)
{
	AndroidBitmapInfo info;
	void *pixels;
	int ret;
	fz_device *dev = NULL;
	float zoom;
	fz_matrix ctm;
	fz_irect bbox;
	fz_rect rect;
	fz_pixmap *pix = NULL;
	float xscale, yscale;
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_document *doc = glo->doc;
	page_cache *pc = &glo->pages[glo->current];
	int hq = (patchW < pageW || patchH < pageH);
	fz_matrix scale;

	if (pc->page == NULL)
		return 0;

	fz_var(pix);
	fz_var(dev);

	LOGI("In native method\n");
	if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
		LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
		return 0;
	}

	LOGI("Checking format\n");
	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		LOGE("Bitmap format is not RGBA_8888 !");
		return 0;
	}

	LOGI("locking pixels\n");
	if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
		LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
		return 0;
	}

	/* Call mupdf to render display list to screen */
	LOGE("Rendering page(%d)=%dx%d patch=[%d,%d,%d,%d]",
			pc->number, pageW, pageH, patchX, patchY, patchW, patchH);

	fz_try(ctx)
	{
		fz_interactive *idoc = fz_interact(doc);

		if (idoc)
		{
			/* Update the changed-rects for both hq patch and main bitmap */
			update_changed_rects(glo, pc, idoc);

			/* Then drop the changed-rects for the bitmap we're about to
			render because we are rendering the entire area */
			drop_changed_rects(ctx, hq ? &pc->hq_changed_rects : &pc->changed_rects);
		}

		if (pc->page_list == NULL)
		{
			/* Render to list */
			pc->page_list = fz_new_display_list(ctx);
			dev = fz_new_list_device(ctx, pc->page_list);
			fz_run_page_contents(doc, pc->page, dev, &fz_identity, NULL);
		}
		if (pc->annot_list == NULL)
		{
			fz_annot *annot;
			if (dev)
			{
				fz_free_device(dev);
				dev = NULL;
			}
			pc->annot_list = fz_new_display_list(ctx);
			dev = fz_new_list_device(ctx, pc->annot_list);
			for (annot = fz_first_annot(doc, pc->page); annot; annot = fz_next_annot(doc, annot))
				fz_run_annot(doc, pc->page, annot, dev, &fz_identity, NULL);
		}
		bbox.x0 = patchX;
		bbox.y0 = patchY;
		bbox.x1 = patchX + patchW;
		bbox.y1 = patchY + patchH;
		pix = fz_new_pixmap_with_bbox_and_data(ctx, glo->colorspace, &bbox, pixels);
		if (pc->page_list == NULL && pc->annot_list == NULL)
		{
			fz_clear_pixmap_with_value(ctx, pix, 0xd0);
			break;
		}
		fz_clear_pixmap_with_value(ctx, pix, 0xff);

		zoom = glo->resolution / 72;
		fz_scale(&ctm, zoom, zoom);
		rect = pc->media_box;
		fz_round_rect(&bbox, fz_transform_rect(&rect, &ctm));
		/* Now, adjust ctm so that it would give the correct page width
		 * heights. */
		xscale = (float)pageW/(float)(bbox.x1-bbox.x0);
		yscale = (float)pageH/(float)(bbox.y1-bbox.y0);
		fz_concat(&ctm, &ctm, fz_scale(&scale, xscale, yscale));
		rect = pc->media_box;
		fz_transform_rect(&rect, &ctm);
		dev = fz_new_draw_device(ctx, pix);
#ifdef TIME_DISPLAY_LIST
		{
			clock_t time;
			int i;

			LOGE("Executing display list");
			time = clock();
			for (i=0; i<100;i++) {
#endif
				if (pc->page_list)
					fz_run_display_list(pc->page_list, dev, &ctm, &rect, NULL);
				if (pc->annot_list)
					fz_run_display_list(pc->annot_list, dev, &ctm, &rect, NULL);
#ifdef TIME_DISPLAY_LIST
			}
			time = clock() - time;
			LOGE("100 renders in %d (%d per sec)", time, CLOCKS_PER_SEC);
		}
#endif
		fz_free_device(dev);
		dev = NULL;
		fz_drop_pixmap(ctx, pix);
		LOGE("Rendered");
	}
	fz_catch(ctx)
	{
		fz_free_device(dev);
		LOGE("Render failed");
	}

	AndroidBitmap_unlockPixels(env, bitmap);

	return 1;
}

static char *widget_type_string(int t)
{
	switch(t)
	{
	case FZ_WIDGET_TYPE_PUSHBUTTON: return "pushbutton";
	case FZ_WIDGET_TYPE_CHECKBOX: return "checkbox";
	case FZ_WIDGET_TYPE_RADIOBUTTON: return "radiobutton";
	case FZ_WIDGET_TYPE_TEXT: return "text";
	case FZ_WIDGET_TYPE_LISTBOX: return "listbox";
	case FZ_WIDGET_TYPE_COMBOBOX: return "combobox";
	default: return "non-widget";
	}
}
JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_updatePageInternal)(JNIEnv *env, jobject thiz, jobject bitmap, int page,
		int pageW, int pageH, int patchX, int patchY, int patchW, int patchH)
{
	AndroidBitmapInfo info;
	void *pixels;
	int ret;
	fz_device *dev = NULL;
	float zoom;
	fz_matrix ctm;
	fz_irect bbox;
	fz_rect rect;
	fz_pixmap *pix = NULL;
	float xscale, yscale;
	fz_interactive *idoc;
	page_cache *pc = NULL;
	int hq = (patchW < pageW || patchH < pageH);
	int i;
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_document *doc = glo->doc;
	rect_node *crect;
	fz_matrix scale;

	for (i = 0; i < NUM_CACHE; i++)
	{
		if (glo->pages[i].page != NULL && glo->pages[i].number == page)
		{
			pc = &glo->pages[i];
			break;
		}
	}

	if (pc == NULL)
	{
		/* Without a cached page object we cannot perform a partial update so
		render the entire bitmap instead */
		JNI_FN(MuPDFCore_gotoPageInternal)(env, thiz, page);
		return JNI_FN(MuPDFCore_drawPage)(env, thiz, bitmap, pageW, pageH, patchX, patchY, patchW, patchH);
	}

	idoc = fz_interact(doc);

	fz_var(pix);
	fz_var(dev);

	LOGI("In native method\n");
	if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
		LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
		return 0;
	}

	LOGI("Checking format\n");
	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		LOGE("Bitmap format is not RGBA_8888 !");
		return 0;
	}

	LOGI("locking pixels\n");
	if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
		LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
		return 0;
	}

	/* Call mupdf to render display list to screen */
	LOGE("Rendering page(%d)=%dx%d patch=[%d,%d,%d,%d]",
			pc->number, pageW, pageH, patchX, patchY, patchW, patchH);

	fz_try(ctx)
	{
		fz_annot *annot;

		if (idoc)
		{
			/* Update the changed-rects for both hq patch and main bitmap */
			update_changed_rects(glo, pc, idoc);
		}

		if (pc->page_list == NULL)
		{
			/* Render to list */
			pc->page_list = fz_new_display_list(ctx);
			dev = fz_new_list_device(ctx, pc->page_list);
			fz_run_page_contents(doc, pc->page, dev, &fz_identity, NULL);
		}

		if (pc->annot_list == NULL) {
			if (dev) {
				fz_free_device(dev);
				dev = NULL;
			}
			pc->annot_list = fz_new_display_list(ctx);
			dev = fz_new_list_device(ctx, pc->annot_list);
			for (annot = fz_first_annot(doc, pc->page); annot; annot = fz_next_annot(doc, annot))
				fz_run_annot(doc, pc->page, annot, dev, &fz_identity, NULL);
		}

		bbox.x0 = patchX;
		bbox.y0 = patchY;
		bbox.x1 = patchX + patchW;
		bbox.y1 = patchY + patchH;
		pix = fz_new_pixmap_with_bbox_and_data(ctx, glo->colorspace, &bbox, pixels);

		zoom = glo->resolution / 72;
		fz_scale(&ctm, zoom, zoom);
		rect = pc->media_box;
		fz_round_rect(&bbox, fz_transform_rect(&rect, &ctm));
		/* Now, adjust ctm so that it would give the correct page width
		 * heights. */
		xscale = (float)pageW/(float)(bbox.x1-bbox.x0);
		yscale = (float)pageH/(float)(bbox.y1-bbox.y0);
		fz_concat(&ctm, &ctm, fz_scale(&scale, xscale, yscale));
		rect = pc->media_box;
		fz_transform_rect(&rect, &ctm);

		LOGI("Start partial update");
		for (crect = hq ? pc->hq_changed_rects : pc->changed_rects; crect; crect = crect->next)
		{
			fz_irect abox;
			fz_rect arect = crect->rect;
			fz_intersect_rect(fz_transform_rect(&arect, &ctm), &rect);
			fz_round_rect(&abox, &arect);

			LOGI("Update rectangle (%d, %d, %d, %d)", abox.x0, abox.y0, abox.x1, abox.y1);
			if (!fz_is_empty_irect(&abox))
			{
				LOGI("And it isn't empty");
				fz_clear_pixmap_rect_with_value(ctx, pix, 0xff, &abox);
				dev = fz_new_draw_device_with_bbox(ctx, pix, &abox);
				if (pc->page_list)
					fz_run_display_list(pc->page_list, dev, &ctm, &arect, NULL);
				if (pc->annot_list)
					fz_run_display_list(pc->annot_list, dev, &ctm, &arect, NULL);
				fz_free_device(dev);
				dev = NULL;
			}
		}
		LOGI("End partial update");

		/* Drop the changed rects we've just rendered */
		drop_changed_rects(ctx, hq ? &pc->hq_changed_rects : &pc->changed_rects);

		LOGE("Rendered");
	}
	fz_catch(ctx)
	{
		fz_free_device(dev);
		LOGE("Render failed");
	}

	fz_drop_pixmap(ctx, pix);
	AndroidBitmap_unlockPixels(env, bitmap);

	return 1;
}

static fz_text_char textcharat(fz_text_page *page, int idx)
{
	static fz_text_char emptychar = { {0,0,0,0}, ' ' };
	fz_text_block *block;
	fz_text_line *line;
	fz_text_span *span;
	int ofs = 0;
	for (block = page->blocks; block < page->blocks + page->len; block++)
	{
		for (line = block->lines; line < block->lines + block->len; line++)
		{
			for (span = line->spans; span < line->spans + line->len; span++)
			{
				if (idx < ofs + span->len)
					return span->text[idx - ofs];
				/* pseudo-newline */
				if (span + 1 == line->spans + line->len)
				{
					if (idx == ofs + span->len)
						return emptychar;
					ofs++;
				}
				ofs += span->len;
			}
		}
	}
	return emptychar;
}

static int
charat(fz_text_page *page, int idx)
{
	return textcharat(page, idx).c;
}

static fz_rect
bboxcharat(fz_text_page *page, int idx)
{
	return textcharat(page, idx).bbox;
}

static int
textlen(fz_text_page *page)
{
	fz_text_block *block;
	fz_text_line *line;
	fz_text_span *span;
	int len = 0;
	for (block = page->blocks; block < page->blocks + page->len; block++)
	{
		for (line = block->lines; line < block->lines + block->len; line++)
		{
			for (span = line->spans; span < line->spans + line->len; span++)
				len += span->len;
			len++; /* pseudo-newline */
		}
	}
	return len;
}

static int
match(fz_text_page *page, const char *s, int n)
{
	int orig = n;
	int c;
	while (*s) {
		s += fz_chartorune(&c, (char *)s);
		if (c == ' ' && charat(page, n) == ' ') {
			while (charat(page, n) == ' ')
				n++;
		} else {
			if (tolower(c) != tolower(charat(page, n)))
				return 0;
			n++;
		}
	}
	return n - orig;
}

static int
countOutlineItems(fz_outline *outline)
{
	int count = 0;

	while (outline)
	{
		if (outline->dest.kind == FZ_LINK_GOTO
				&& outline->dest.ld.gotor.page >= 0
				&& outline->title)
			count++;

		count += countOutlineItems(outline->down);
		outline = outline->next;
	}

	return count;
}

static int
fillInOutlineItems(JNIEnv * env, jclass olClass, jmethodID ctor, jobjectArray arr, int pos, fz_outline *outline, int level)
{
	while (outline)
	{
		if (outline->dest.kind == FZ_LINK_GOTO)
		{
			int page = outline->dest.ld.gotor.page;
			if (page >= 0 && outline->title)
			{
				jobject ol;
				jstring title = (*env)->NewStringUTF(env, outline->title);
				if (title == NULL) return -1;
				ol = (*env)->NewObject(env, olClass, ctor, level, title, page);
				if (ol == NULL) return -1;
				(*env)->SetObjectArrayElement(env, arr, pos, ol);
				(*env)->DeleteLocalRef(env, ol);
				(*env)->DeleteLocalRef(env, title);
				pos++;
			}
		}
		pos = fillInOutlineItems(env, olClass, ctor, arr, pos, outline->down, level+1);
		if (pos < 0) return -1;
		outline = outline->next;
	}

	return pos;
}

JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_needsPasswordInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);

	return fz_needs_password(glo->doc) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_authenticatePasswordInternal)(JNIEnv *env, jobject thiz, jstring password)
{
	const char *pw;
	int         result;
	globals    *glo = get_globals(env, thiz);

	pw = (*env)->GetStringUTFChars(env, password, NULL);
	if (pw == NULL)
		return JNI_FALSE;

	result = fz_authenticate_password(glo->doc, (char *)pw);
	(*env)->ReleaseStringUTFChars(env, password, pw);
	return result;
}

JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_hasOutlineInternal)(JNIEnv * env, jobject thiz)
{
	globals    *glo = get_globals(env, thiz);
	fz_outline *outline = fz_load_outline(glo->doc);

	return (outline == NULL) ? JNI_FALSE : JNI_TRUE;
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_getOutlineInternal)(JNIEnv * env, jobject thiz)
{
	jclass        olClass;
	jmethodID     ctor;
	jobjectArray  arr;
	jobject       ol;
	fz_outline   *outline;
	int           nItems;
	globals      *glo = get_globals(env, thiz);

	olClass = (*env)->FindClass(env, PACKAGENAME "/OutlineItem");
	if (olClass == NULL) return NULL;
	ctor = (*env)->GetMethodID(env, olClass, "<init>", "(ILjava/lang/String;I)V");
	if (ctor == NULL) return NULL;

	outline = fz_load_outline(glo->doc);
	nItems = countOutlineItems(outline);

	arr = (*env)->NewObjectArray(env,
					nItems,
					olClass,
					NULL);
	if (arr == NULL) return NULL;

	return fillInOutlineItems(env, olClass, ctor, arr, 0, outline, 0) > 0
			? arr
			:NULL;
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_searchPage)(JNIEnv * env, jobject thiz, jstring jtext)
{
	jclass         rectClass;
	jmethodID      ctor;
	jobjectArray   arr;
	jobject        rect;
	fz_text_sheet *sheet = NULL;
	fz_text_page  *text = NULL;
	fz_device     *dev  = NULL;
	float          zoom;
	fz_matrix      ctm;
	int            pos;
	int            len;
	int            i, n;
	int            hit_count = 0;
	const char    *str;
	globals       *glo = get_globals(env, thiz);
	fz_context    *ctx = glo->ctx;
	fz_document   *doc = glo->doc;
	page_cache    *pc = &glo->pages[glo->current];

	rectClass = (*env)->FindClass(env, "android/graphics/RectF");
	if (rectClass == NULL) return NULL;
	ctor = (*env)->GetMethodID(env, rectClass, "<init>", "(FFFF)V");
	if (ctor == NULL) return NULL;
	str = (*env)->GetStringUTFChars(env, jtext, NULL);
	if (str == NULL) return NULL;

	fz_var(sheet);
	fz_var(text);
	fz_var(dev);

	fz_try(ctx)
	{
		fz_rect mbrect;

		if (glo->hit_bbox == NULL)
			glo->hit_bbox = fz_malloc_array(ctx, MAX_SEARCH_HITS, sizeof(*glo->hit_bbox));

		zoom = glo->resolution / 72;
		fz_scale(&ctm, zoom, zoom);
		mbrect = pc->media_box;
		fz_transform_rect(&mbrect, &ctm);
		sheet = fz_new_text_sheet(ctx);
		text = fz_new_text_page(ctx, &mbrect);
		dev  = fz_new_text_device(ctx, sheet, text);
		fz_run_page(doc, pc->page, dev, &ctm, NULL);
		fz_free_device(dev);
		dev = NULL;

		len = textlen(text);
		for (pos = 0; pos < len; pos++)
		{
			fz_rect rr = fz_empty_rect;
			n = match(text, str, pos);
			for (i = 0; i < n; i++)
			{
				fz_rect bbox = bboxcharat(text, pos + i);
				fz_union_rect(&rr, &bbox);
			}

			if (!fz_is_empty_rect(&rr) && hit_count < MAX_SEARCH_HITS)
				glo->hit_bbox[hit_count++] = rr;
		}
	}
	fz_always(ctx)
	{
		fz_free_text_page(ctx, text);
		fz_free_text_sheet(ctx, sheet);
		fz_free_device(dev);
	}
	fz_catch(ctx)
	{
		jclass cls;
		(*env)->ReleaseStringUTFChars(env, jtext, str);
		cls = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
		if (cls != NULL)
			(*env)->ThrowNew(env, cls, "Out of memory in MuPDFCore_searchPage");
		(*env)->DeleteLocalRef(env, cls);

		return NULL;
	}

	(*env)->ReleaseStringUTFChars(env, jtext, str);

	arr = (*env)->NewObjectArray(env,
					hit_count,
					rectClass,
					NULL);
	if (arr == NULL) return NULL;

	for (i = 0; i < hit_count; i++) {
		rect = (*env)->NewObject(env, rectClass, ctor,
				(float) (glo->hit_bbox[i].x0),
				(float) (glo->hit_bbox[i].y0),
				(float) (glo->hit_bbox[i].x1),
				(float) (glo->hit_bbox[i].y1));
		if (rect == NULL)
			return NULL;
		(*env)->SetObjectArrayElement(env, arr, i, rect);
		(*env)->DeleteLocalRef(env, rect);
	}

	return arr;
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_text)(JNIEnv * env, jobject thiz)
{
	jclass textCharClass;
	jclass textSpanClass;
	jclass textLineClass;
	jclass textBlockClass;
	jmethodID ctor;
	jobjectArray barr = NULL;
	fz_text_sheet *sheet = NULL;
	fz_text_page *text = NULL;
	fz_device *dev  = NULL;
	float zoom;
	fz_matrix ctm;
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_document *doc = glo->doc;
	page_cache *pc = &glo->pages[glo->current];

	textCharClass = (*env)->FindClass(env, PACKAGENAME "/TextChar");
	if (textCharClass == NULL) return NULL;
	textSpanClass = (*env)->FindClass(env, "[L" PACKAGENAME "/TextChar;");
	if (textSpanClass == NULL) return NULL;
	textLineClass = (*env)->FindClass(env, "[[L" PACKAGENAME "/TextChar;");
	if (textLineClass == NULL) return NULL;
	textBlockClass = (*env)->FindClass(env, "[[[L" PACKAGENAME "/TextChar;");
	if (textBlockClass == NULL) return NULL;
	ctor = (*env)->GetMethodID(env, textCharClass, "<init>", "(FFFFC)V");
	if (ctor == NULL) return NULL;

	fz_var(sheet);
	fz_var(text);
	fz_var(dev);

	fz_try(ctx)
	{
		fz_rect mbrect;
		int b, l, s, c;

		zoom = glo->resolution / 72;
		fz_scale(&ctm, zoom, zoom);
		mbrect = pc->media_box;
		fz_transform_rect(&mbrect, &ctm);
		sheet = fz_new_text_sheet(ctx);
		text = fz_new_text_page(ctx, &mbrect);
		dev  = fz_new_text_device(ctx, sheet, text);
		fz_run_page(doc, pc->page, dev, &ctm, NULL);
		fz_free_device(dev);
		dev = NULL;

		barr = (*env)->NewObjectArray(env, text->len, textBlockClass, NULL);
		if (barr == NULL) fz_throw(ctx, "NewObjectArray failed");

		for (b = 0; b < text->len; b++)
		{
			fz_text_block *block = &text->blocks[b];
			jobjectArray *larr = (*env)->NewObjectArray(env, block->len, textLineClass, NULL);
			if (larr == NULL) fz_throw(ctx, "NewObjectArray failed");

			for (l = 0; l < block->len; l++)
			{
				fz_text_line *line = &block->lines[l];
				jobjectArray *sarr = (*env)->NewObjectArray(env, line->len, textSpanClass, NULL);
				if (sarr == NULL) fz_throw(ctx, "NewObjectArray failed");

				for (s = 0; s < line->len; s++)
				{
					fz_text_span *span = &line->spans[s];
					jobjectArray *carr = (*env)->NewObjectArray(env, span->len, textCharClass, NULL);
					if (carr == NULL) fz_throw(ctx, "NewObjectArray failed");

					for (c = 0; c < span->len; c++)
					{
						fz_text_char *ch = &span->text[c];
						jobject cobj = (*env)->NewObject(env, textCharClass, ctor, ch->bbox.x0, ch->bbox.y0, ch->bbox.x1, ch->bbox.y1, ch->c);
						if (cobj == NULL) fz_throw(ctx, "NewObjectfailed");

						(*env)->SetObjectArrayElement(env, carr, c, cobj);
						(*env)->DeleteLocalRef(env, cobj);
					}

					(*env)->SetObjectArrayElement(env, sarr, s, carr);
					(*env)->DeleteLocalRef(env, carr);
				}

				(*env)->SetObjectArrayElement(env, larr, l, sarr);
				(*env)->DeleteLocalRef(env, sarr);
			}

			(*env)->SetObjectArrayElement(env, barr, b, larr);
			(*env)->DeleteLocalRef(env, larr);
		}
	}
	fz_always(ctx)
	{
		fz_free_text_page(ctx, text);
		fz_free_text_sheet(ctx, sheet);
		fz_free_device(dev);
	}
	fz_catch(ctx)
	{
		jclass cls = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
		if (cls != NULL)
			(*env)->ThrowNew(env, cls, "Out of memory in MuPDFCore_text");
		(*env)->DeleteLocalRef(env, cls);

		return NULL;
	}

	return barr;
}

JNIEXPORT jbyteArray JNICALL
JNI_FN(MuPDFCore_textAsHtml)(JNIEnv * env, jobject thiz)
{
	fz_text_sheet *sheet = NULL;
	fz_text_page *text = NULL;
	fz_device *dev  = NULL;
	fz_matrix ctm;
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_document *doc = glo->doc;
	page_cache *pc = &glo->pages[glo->current];
	jbyteArray bArray = NULL;
	fz_buffer *buf = NULL;
	fz_output *out = NULL;

	fz_var(sheet);
	fz_var(text);
	fz_var(dev);

	fz_try(ctx)
	{
		fz_rect mbrect;
		int b, l, s, c;

		ctm = fz_identity;
		mbrect = pc->media_box;
		fz_transform_rect(&mbrect, &ctm);
		sheet = fz_new_text_sheet(ctx);
		text = fz_new_text_page(ctx, &mbrect);
		dev  = fz_new_text_device(ctx, sheet, text);
		fz_run_page(doc, pc->page, dev, &ctm, NULL);
		fz_free_device(dev);
		dev = NULL;

		buf = fz_new_buffer(ctx, 256);
		out = fz_new_output_buffer(ctx, buf);
		fz_printf(out, "<html>\n");
		fz_printf(out, "<style>\n");
		fz_printf(out, "body{margin:0;}\n");
		fz_printf(out, "div.page{background-color:white;}\n");
		fz_printf(out, "div.block{margin:0pt;padding:0pt;}\n");
		//fz_printf(out, "p{margin:0;padding:0;}\n");
		fz_printf(out, "</style>\n");
		fz_printf(out, "<body style=\"margin:0\"><div style=\"padding:10px\" id=\"content\">");
		fz_print_text_page_html(ctx, out, text);
		fz_printf(out, "</div></body>\n");
		fz_printf(out, "<style>\n");
		fz_print_text_sheet(ctx, out, sheet);
		fz_printf(out, "</style>\n</html>\n");
		fz_close_output(out);
		out = NULL;

		bArray = (*env)->NewByteArray(env, buf->len);
		if (bArray == NULL)
			fz_throw(ctx, "Failed to make byteArray");
		(*env)->SetByteArrayRegion(env, bArray, 0, buf->len, buf->data);

	}
	fz_always(ctx)
	{
		fz_free_text_page(ctx, text);
		fz_free_text_sheet(ctx, sheet);
		fz_free_device(dev);
		fz_close_output(out);
		fz_drop_buffer(ctx, buf);
	}
	fz_catch(ctx)
	{
		jclass cls = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
		if (cls != NULL)
			(*env)->ThrowNew(env, cls, "Out of memory in MuPDFCore_textAsHtml");
		(*env)->DeleteLocalRef(env, cls);

		return NULL;
	}

	return bArray;
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_addStrikeOutAnnotationInternal)(JNIEnv * env, jobject thiz, jobjectArray lines)
{
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_document *doc = glo->doc;
	fz_interactive *idoc = fz_interact(doc);
	page_cache *pc = &glo->pages[glo->current];
	jclass rect_cls;
	jfieldID x0_fid, y0_fid, x1_fid, y1_fid;
	int i, n;
	fz_path *path = NULL;
	fz_stroke_state *stroke = NULL;
	fz_device *dev = NULL;
	fz_display_list *strike_list;

	if (idoc == NULL)
		return;

	strike_list = fz_new_display_list(ctx);

	fz_var(path);
	fz_var(stroke);
	fz_var(dev);
	fz_try(ctx)
	{
		fz_annot *annot;
		fz_matrix ctm;

		float color[3] = {1.0, 0.0, 0.0};
		float zoom = glo->resolution / 72;
		zoom = 1.0 / zoom;
		fz_scale(&ctm, zoom, zoom);
		LOGI("P1 %f", zoom);
		rect_cls = (*env)->FindClass(env, "android.graphics.RectF");
		if (rect_cls == NULL) fz_throw(ctx, "FindClass");
		LOGI("P2");
		x0_fid = (*env)->GetFieldID(env, rect_cls, "left", "F");
		if (x0_fid == NULL) fz_throw(ctx, "GetFieldID(left)");
		LOGI("P3");
		y0_fid = (*env)->GetFieldID(env, rect_cls, "top", "F");
		if (y0_fid == NULL) fz_throw(ctx, "GetFieldID(top)");
		LOGI("P4");
		x1_fid = (*env)->GetFieldID(env, rect_cls, "right", "F");
		if (x1_fid == NULL) fz_throw(ctx, "GetFieldID(right)");
		LOGI("P5");
		y1_fid = (*env)->GetFieldID(env, rect_cls, "bottom", "F");
		if (y1_fid == NULL) fz_throw(ctx, "GetFieldID(bottom)");
		LOGI("P6");

		n = (*env)->GetArrayLength(env, lines);
		LOGI("nlines=%d", n);

		dev = fz_new_list_device(ctx, strike_list);

		for (i = 0; i < n; i++)
		{
			jobject line = (*env)->GetObjectArrayElement(env, lines, i);
			float x0 = (*env)->GetFloatField(env, line, x0_fid);
			float y0 = (*env)->GetFloatField(env, line, y0_fid);
			float x1 = (*env)->GetFloatField(env, line, x1_fid);
			float y1 = (*env)->GetFloatField(env, line, y1_fid);
			float vcenter = (y0 + y1)/2;
			float thickness = y1 - y0;

			if (!stroke || stroke->linewidth != thickness)
			{
				if (stroke)
				{
					// assert(path)
					fz_stroke_path(dev, path, stroke, &ctm, fz_device_rgb, color, 1.0);
					LOGI("Path stroked");
					fz_drop_stroke_state(ctx, stroke);
					stroke = NULL;
					fz_free_path(ctx, path);
					path = NULL;
				}

				stroke = fz_new_stroke_state(ctx);
				LOGI("thickness(%f)", thickness);
				stroke->linewidth = thickness;
				path = fz_new_path(ctx);
			}

			fz_moveto(ctx, path, x0, vcenter);
			LOGI("moveto(%f,%f)", x0, vcenter);
			fz_lineto(ctx, path, x1, vcenter);
			LOGI("lineto(%f,%f)", x1, vcenter);
		}

		if (stroke)
		{
			fz_stroke_path(dev, path, stroke, &ctm, fz_device_rgb, color, 1.0);
			LOGI("Path stroked");
		}

		annot = fz_create_annot(idoc, pc->page, FZ_ANNOT_STRIKEOUT);
		fz_set_annot_appearance(idoc, annot, strike_list);
		dump_annotation_display_lists(glo);
	}
	fz_always(ctx)
	{
		fz_free_device(dev);
		fz_drop_stroke_state(ctx, stroke);
		fz_free_path(ctx, path);
		fz_free_display_list(ctx, strike_list);
	}
	fz_catch(ctx)
	{
		fz_free_device(dev);
		LOGE("addStrikeOutAnnotation: %s failed", ctx->error->message);
		jclass cls = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
		if (cls != NULL)
			(*env)->ThrowNew(env, cls, "Out of memory in MuPDFCore_searchPage");
		(*env)->DeleteLocalRef(env, cls);
	}
}

static void close_doc(globals *glo)
{
	int i;

	fz_free(glo->ctx, glo->hit_bbox);
	glo->hit_bbox = NULL;

	for (i = 0; i < NUM_CACHE; i++)
		drop_page_cache(glo, &glo->pages[i]);

	alerts_fin(glo);

	fz_close_document(glo->doc);
	glo->doc = NULL;
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_destroying)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);

	LOGI("Destroying");
	close_doc(glo);
	fz_free(glo->ctx, glo->current_path);
	glo->current_path = NULL;
	free(glo);

#ifdef NDK_PROFILER
	// Apparently we should really be writing to whatever path we get
	// from calling getFilesDir() in the java part, which supposedly
	// gives /sdcard/data/data/com.artifex.MuPDF/gmon.out, but that's
	// unfriendly.
	setenv("CPUPROFILE", "/sdcard/gmon.out", 1);
	moncleanup();
#endif
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_getPageLinksInternal)(JNIEnv * env, jobject thiz, int pageNumber)
{
	jclass       linkInfoClass;
	jclass       linkInfoInternalClass;
	jclass       linkInfoExternalClass;
	jclass       linkInfoRemoteClass;
	jmethodID    ctorInternal;
	jmethodID    ctorExternal;
	jmethodID    ctorRemote;
	jobjectArray arr;
	jobject      linkInfo;
	fz_matrix    ctm;
	float        zoom;
	fz_link     *list;
	fz_link     *link;
	int          count;
	page_cache  *pc;
	globals     *glo = get_globals(env, thiz);

	linkInfoClass = (*env)->FindClass(env, PACKAGENAME "/LinkInfo");
	if (linkInfoClass == NULL) return NULL;
	linkInfoInternalClass = (*env)->FindClass(env, PACKAGENAME "/LinkInfoInternal");
	if (linkInfoInternalClass == NULL) return NULL;
	linkInfoExternalClass = (*env)->FindClass(env, PACKAGENAME "/LinkInfoExternal");
	if (linkInfoExternalClass == NULL) return NULL;
	linkInfoRemoteClass = (*env)->FindClass(env, PACKAGENAME "/LinkInfoRemote");
	if (linkInfoRemoteClass == NULL) return NULL;
	ctorInternal = (*env)->GetMethodID(env, linkInfoInternalClass, "<init>", "(FFFFI)V");
	if (ctorInternal == NULL) return NULL;
	ctorExternal = (*env)->GetMethodID(env, linkInfoExternalClass, "<init>", "(FFFFLjava/lang/String;)V");
	if (ctorExternal == NULL) return NULL;
	ctorRemote = (*env)->GetMethodID(env, linkInfoRemoteClass, "<init>", "(FFFFLjava/lang/String;IZ)V");
	if (ctorRemote == NULL) return NULL;

	JNI_FN(MuPDFCore_gotoPageInternal)(env, thiz, pageNumber);
	pc = &glo->pages[glo->current];
	if (pc->page == NULL || pc->number != pageNumber)
		return NULL;

	zoom = glo->resolution / 72;
	fz_scale(&ctm, zoom, zoom);

	list = fz_load_links(glo->doc, pc->page);
	count = 0;
	for (link = list; link; link = link->next)
	{
		switch (link->dest.kind)
		{
		case FZ_LINK_GOTO:
		case FZ_LINK_GOTOR:
		case FZ_LINK_URI:
			count++ ;
		}
	}

	arr = (*env)->NewObjectArray(env, count, linkInfoClass, NULL);
	if (arr == NULL) return NULL;

	count = 0;
	for (link = list; link; link = link->next)
	{
		fz_rect rect = link->rect;
		fz_transform_rect(&rect, &ctm);

		switch (link->dest.kind)
		{
		case FZ_LINK_GOTO:
		{
			linkInfo = (*env)->NewObject(env, linkInfoInternalClass, ctorInternal,
					(float)rect.x0, (float)rect.y0, (float)rect.x1, (float)rect.y1,
					link->dest.ld.gotor.page);
			break;
		}

		case FZ_LINK_GOTOR:
		{
			jstring juri = (*env)->NewStringUTF(env, link->dest.ld.gotor.file_spec);
			linkInfo = (*env)->NewObject(env, linkInfoRemoteClass, ctorRemote,
					(float)rect.x0, (float)rect.y0, (float)rect.x1, (float)rect.y1,
					juri, link->dest.ld.gotor.page, link->dest.ld.gotor.new_window ? JNI_TRUE : JNI_FALSE);
			break;
		}

		case FZ_LINK_URI:
		{
			jstring juri = (*env)->NewStringUTF(env, link->dest.ld.uri.uri);
			linkInfo = (*env)->NewObject(env, linkInfoExternalClass, ctorExternal,
					(float)rect.x0, (float)rect.y0, (float)rect.x1, (float)rect.y1,
					juri);
			break;
		}

		default:
			continue;
		}

		if (linkInfo == NULL) return NULL;
		(*env)->SetObjectArrayElement(env, arr, count, linkInfo);
		(*env)->DeleteLocalRef(env, linkInfo);
		count++;
	}

	return arr;
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_getWidgetAreasInternal)(JNIEnv * env, jobject thiz, int pageNumber)
{
	jclass       rectFClass;
	jmethodID    ctor;
	jobjectArray arr;
	jobject      rectF;
	fz_interactive *idoc;
	fz_widget *widget;
	fz_matrix    ctm;
	float        zoom;
	int          count;
	page_cache  *pc;
	globals     *glo = get_globals(env, thiz);

	rectFClass = (*env)->FindClass(env, "android/graphics/RectF");
	if (rectFClass == NULL) return NULL;
	ctor = (*env)->GetMethodID(env, rectFClass, "<init>", "(FFFF)V");
	if (ctor == NULL) return NULL;

	JNI_FN(MuPDFCore_gotoPageInternal)(env, thiz, pageNumber);
	pc = &glo->pages[glo->current];
	if (pc->number != pageNumber || pc->page == NULL)
		return NULL;

	idoc = fz_interact(glo->doc);
	if (idoc == NULL)
		return NULL;

	zoom = glo->resolution / 72;
	fz_scale(&ctm, zoom, zoom);

	count = 0;
	for (widget = fz_first_widget(idoc, pc->page); widget; widget = fz_next_widget(idoc, widget))
		count ++;

	arr = (*env)->NewObjectArray(env, count, rectFClass, NULL);
	if (arr == NULL) return NULL;

	count = 0;
	for (widget = fz_first_widget(idoc, pc->page); widget; widget = fz_next_widget(idoc, widget))
	{
		fz_rect rect;
		fz_bound_widget(widget, &rect);
		fz_transform_rect(&rect, &ctm);

		rectF = (*env)->NewObject(env, rectFClass, ctor,
				(float)rect.x0, (float)rect.y0, (float)rect.x1, (float)rect.y1);
		if (rectF == NULL) return NULL;
		(*env)->SetObjectArrayElement(env, arr, count, rectF);
		(*env)->DeleteLocalRef(env, rectF);

		count ++;
	}

	return arr;
}

JNIEXPORT int JNICALL
JNI_FN(MuPDFCore_passClickEventInternal)(JNIEnv * env, jobject thiz, int pageNumber, float x, float y)
{
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_matrix ctm;
	fz_interactive *idoc = fz_interact(glo->doc);
	float zoom;
	fz_point p;
	fz_ui_event event;
	int changed = 0;
	page_cache *pc;

	if (idoc == NULL)
		return 0;

	JNI_FN(MuPDFCore_gotoPageInternal)(env, thiz, pageNumber);
	pc = &glo->pages[glo->current];
	if (pc->number != pageNumber || pc->page == NULL)
		return 0;

	p.x = x;
	p.y = y;

	/* Ultimately we should probably return a pointer to a java structure
	 * with the link details in, but for now, page number will suffice.
	 */
	zoom = glo->resolution / 72;
	fz_scale(&ctm, zoom, zoom);
	fz_invert_matrix(&ctm, &ctm);

	fz_transform_point(&p, &ctm);

	fz_try(ctx)
	{
		event.etype = FZ_EVENT_TYPE_POINTER;
		event.event.pointer.pt = p;
		event.event.pointer.ptype = FZ_POINTER_DOWN;
		changed = fz_pass_event(idoc, pc->page, &event);
		event.event.pointer.ptype = FZ_POINTER_UP;
		changed |= fz_pass_event(idoc, pc->page, &event);
		if (changed) {
			dump_annotation_display_lists(glo);
		}
	}
	fz_catch(ctx)
	{
		LOGE("passClickEvent: %s", ctx->error->message);
	}

	return changed;
}

JNIEXPORT jstring JNICALL
JNI_FN(MuPDFCore_getFocusedWidgetTextInternal)(JNIEnv * env, jobject thiz)
{
	char *text = "";
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;

	fz_try(ctx)
	{
		fz_interactive *idoc = fz_interact(glo->doc);

		if (idoc)
		{
			fz_widget *focus = fz_focused_widget(idoc);

			if (focus)
				text = fz_text_widget_text(idoc, focus);
		}
	}
	fz_catch(ctx)
	{
		LOGE("getFocusedWidgetText failed: %s", ctx->error->message);
	}

	return (*env)->NewStringUTF(env, text);
}

JNIEXPORT int JNICALL
JNI_FN(MuPDFCore_setFocusedWidgetTextInternal)(JNIEnv * env, jobject thiz, jstring jtext)
{
	const char *text;
	int result = 0;
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;

	text = (*env)->GetStringUTFChars(env, jtext, NULL);
	if (text == NULL)
	{
		LOGE("Failed to get text");
		return 0;
	}

	fz_try(ctx)
	{
		fz_interactive *idoc = fz_interact(glo->doc);

		if (idoc)
		{
			fz_widget *focus = fz_focused_widget(idoc);

			if (focus)
			{
				result = fz_text_widget_set_text(idoc, focus, (char *)text);
				dump_annotation_display_lists(glo);
			}
		}
	}
	fz_catch(ctx)
	{
		LOGE("setFocusedWidgetText failed: %s", ctx->error->message);
	}

	(*env)->ReleaseStringUTFChars(env, jtext, text);

	return result;
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_getFocusedWidgetChoiceOptions)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_interactive *idoc = fz_interact(glo->doc);
	fz_widget *focus;
	int type;
	int nopts, i;
	char **opts = NULL;
	jclass stringClass;
	jobjectArray arr;

	if (idoc == NULL)
		return NULL;

	focus = fz_focused_widget(idoc);
	if (focus == NULL)
		return NULL;

	type = fz_widget_get_type(focus);
	if (type != FZ_WIDGET_TYPE_LISTBOX && type != FZ_WIDGET_TYPE_COMBOBOX)
		return NULL;

	fz_var(opts);
	fz_try(ctx)
	{
		nopts = fz_choice_widget_options(idoc, focus, NULL);
		opts = fz_malloc(ctx, nopts * sizeof(*opts));
		(void)fz_choice_widget_options(idoc, focus, opts);
	}
	fz_catch(ctx)
	{
		fz_free(ctx, opts);
		LOGE("Failed in getFocuseedWidgetChoiceOptions");
		return NULL;
	}

	stringClass = (*env)->FindClass(env, "java/lang/String");

	arr = (*env)->NewObjectArray(env, nopts, stringClass, NULL);

	for (i = 0; i < nopts; i++)
	{
		jstring s = (*env)->NewStringUTF(env, opts[i]);
		if (s != NULL)
			(*env)->SetObjectArrayElement(env, arr, i, s);

		(*env)->DeleteLocalRef(env, s);
	}

	fz_free(ctx, opts);

	return arr;
}

JNIEXPORT jobjectArray JNICALL
JNI_FN(MuPDFCore_getFocusedWidgetChoiceSelected)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_interactive *idoc = fz_interact(glo->doc);
	fz_widget *focus;
	int type;
	int nsel, i;
	char **sel = NULL;
	jclass stringClass;
	jobjectArray arr;

	if (idoc == NULL)
		return NULL;

	focus = fz_focused_widget(idoc);
	if (focus == NULL)
		return NULL;

	type = fz_widget_get_type(focus);
	if (type != FZ_WIDGET_TYPE_LISTBOX && type != FZ_WIDGET_TYPE_COMBOBOX)
		return NULL;

	fz_var(sel);
	fz_try(ctx)
	{
		nsel = fz_choice_widget_value(idoc, focus, NULL);
		sel = fz_malloc(ctx, nsel * sizeof(*sel));
		(void)fz_choice_widget_value(idoc, focus, sel);
	}
	fz_catch(ctx)
	{
		fz_free(ctx, sel);
		LOGE("Failed in getFocuseedWidgetChoiceOptions");
		return NULL;
	}

	stringClass = (*env)->FindClass(env, "java/lang/String");

	arr = (*env)->NewObjectArray(env, nsel, stringClass, NULL);

	for (i = 0; i < nsel; i++)
	{
		jstring s = (*env)->NewStringUTF(env, sel[i]);
		if (s != NULL)
			(*env)->SetObjectArrayElement(env, arr, i, s);

		(*env)->DeleteLocalRef(env, s);
	}

	fz_free(ctx, sel);

	return arr;
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_setFocusedWidgetChoiceSelectedInternal)(JNIEnv * env, jobject thiz, jobjectArray arr)
{
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;
	fz_interactive *idoc = fz_interact(glo->doc);
	fz_widget *focus;
	int type;
	int nsel, i;
	char **sel = NULL;
	jstring *objs = NULL;

	if (idoc == NULL)
		return;

	focus = fz_focused_widget(idoc);
	if (focus == NULL)
		return;

	type = fz_widget_get_type(focus);
	if (type != FZ_WIDGET_TYPE_LISTBOX && type != FZ_WIDGET_TYPE_COMBOBOX)
		return;

	nsel = (*env)->GetArrayLength(env, arr);

	sel = calloc(nsel, sizeof(*sel));
	objs = calloc(nsel, sizeof(*objs));
	if (objs == NULL || sel == NULL)
	{
		free(sel);
		free(objs);
		LOGE("Failed in setFocusWidgetChoiceSelected");
		return;
	}

	for (i = 0; i < nsel; i++)
	{
		objs[i] = (jstring)(*env)->GetObjectArrayElement(env, arr, i);
		sel[i] = (char *)(*env)->GetStringUTFChars(env, objs[i], NULL);
	}

	fz_try(ctx)
	{
		fz_choice_widget_set_value(idoc, focus, nsel, sel);
		dump_annotation_display_lists(glo);
	}
	fz_catch(ctx)
	{
		LOGE("Failed in setFocusWidgetChoiceSelected");
	}

	for (i = 0; i < nsel; i++)
		(*env)->ReleaseStringUTFChars(env, objs[i], sel[i]);

	free(sel);
	free(objs);
}

JNIEXPORT int JNICALL
JNI_FN(MuPDFCore_getFocusedWidgetTypeInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	fz_interactive *idoc = fz_interact(glo->doc);
	fz_widget *focus;

	if (idoc == NULL)
		return NONE;

	focus = fz_focused_widget(idoc);

	if (focus == NULL)
		return NONE;

	switch (fz_widget_get_type(focus))
	{
	case FZ_WIDGET_TYPE_TEXT: return TEXT;
	case FZ_WIDGET_TYPE_LISTBOX: return LISTBOX;
	case FZ_WIDGET_TYPE_COMBOBOX: return COMBOBOX;
	}

	return NONE;
}

JNIEXPORT jobject JNICALL
JNI_FN(MuPDFCore_waitForAlertInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	jclass alertClass;
	jmethodID ctor;
	jstring title;
	jstring message;
	int alert_present;
	fz_alert_event alert;

	LOGT("Enter waitForAlert");
	pthread_mutex_lock(&glo->fin_lock);
	pthread_mutex_lock(&glo->alert_lock);

	while (glo->alerts_active && !glo->alert_request)
		pthread_cond_wait(&glo->alert_request_cond, &glo->alert_lock);
	glo->alert_request = 0;

	alert_present = (glo->alerts_active && glo->current_alert);

	if (alert_present)
		alert = *glo->current_alert;

	pthread_mutex_unlock(&glo->alert_lock);
	pthread_mutex_unlock(&glo->fin_lock);
	LOGT("Exit waitForAlert %d", alert_present);

	if (!alert_present)
		return NULL;

	alertClass = (*env)->FindClass(env, PACKAGENAME "/MuPDFAlertInternal");
	if (alertClass == NULL)
		return NULL;

	ctor = (*env)->GetMethodID(env, alertClass, "<init>", "(Ljava/lang/String;IILjava/lang/String;I)V");
	if (ctor == NULL)
		return NULL;

	title = (*env)->NewStringUTF(env, alert.title);
	if (title == NULL)
		return NULL;

	message = (*env)->NewStringUTF(env, alert.message);
	if (message == NULL)
		return NULL;

	return (*env)->NewObject(env, alertClass, ctor, message, alert.icon_type, alert.button_group_type, title, alert.button_pressed);
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_replyToAlertInternal)(JNIEnv * env, jobject thiz, jobject alert)
{
	globals *glo = get_globals(env, thiz);
	jclass alertClass;
	jfieldID field;
	int button_pressed;

	alertClass = (*env)->FindClass(env, PACKAGENAME "/MuPDFAlertInternal");
	if (alertClass == NULL)
		return;

	field = (*env)->GetFieldID(env, alertClass, "buttonPressed", "I");
	if (field == NULL)
		return;

	button_pressed = (*env)->GetIntField(env, alert, field);

	LOGT("Enter replyToAlert");
	pthread_mutex_lock(&glo->alert_lock);

	if (glo->alerts_active && glo->current_alert)
	{
		// Fill in button_pressed and signal reply received.
		glo->current_alert->button_pressed = button_pressed;
		glo->alert_reply = 1;
		pthread_cond_signal(&glo->alert_reply_cond);
	}

	pthread_mutex_unlock(&glo->alert_lock);
	LOGT("Exit replyToAlert");
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_startAlertsInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);

	if (!glo->alerts_initialised)
		return;

	LOGT("Enter startAlerts");
	pthread_mutex_lock(&glo->alert_lock);

	glo->alert_reply = 0;
	glo->alert_request = 0;
	glo->alerts_active = 1;
	glo->current_alert = NULL;

	pthread_mutex_unlock(&glo->alert_lock);
	LOGT("Exit startAlerts");
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_stopAlertsInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);

	if (!glo->alerts_initialised)
		return;

	LOGT("Enter stopAlerts");
	pthread_mutex_lock(&glo->alert_lock);

	glo->alert_reply = 0;
	glo->alert_request = 0;
	glo->alerts_active = 0;
	glo->current_alert = NULL;
	pthread_cond_signal(&glo->alert_reply_cond);
	pthread_cond_signal(&glo->alert_request_cond);

	pthread_mutex_unlock(&glo->alert_lock);
	LOGT("Exit stopAleerts");
}

JNIEXPORT jboolean JNICALL
JNI_FN(MuPDFCore_hasChangesInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	fz_interactive *idoc = fz_interact(glo->doc);

	return (idoc && fz_has_unsaved_changes(idoc)) ? JNI_TRUE : JNI_FALSE;
}

static char *tmp_path(char *path)
{
	int f;
	char *buf = malloc(strlen(path) + 6 + 1);
	if (!buf)
		return NULL;

	strcpy(buf, path);
	strcat(buf, "XXXXXX");

	f = mkstemp(buf);

	if (f >= 0)
	{
		close(f);
		return buf;
	}
	else
	{
		free(buf);
		return NULL;
	}
}

JNIEXPORT void JNICALL
JNI_FN(MuPDFCore_saveInternal)(JNIEnv * env, jobject thiz)
{
	globals *glo = get_globals(env, thiz);
	fz_context *ctx = glo->ctx;

	if (glo->doc && glo->current_path)
	{
		char *tmp;
		fz_write_options opts;
		opts.do_ascii = 1;
		opts.do_expand = 0;
		opts.do_garbage = 1;
		opts.do_linear = 0;

		tmp = tmp_path(glo->current_path);
		if (tmp)
		{
			int written;

			fz_var(written);
			fz_try(ctx)
			{
				fz_write_document(glo->doc, tmp, &opts);
				written = 1;
			}
			fz_catch(ctx)
			{
				written = 0;
			}

			if (written)
			{
				close_doc(glo);
				rename(tmp, glo->current_path);
			}

			free(tmp);
		}
	}
}
