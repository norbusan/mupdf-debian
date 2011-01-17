#include <jni.h>
#include <time.h>
#include <android/log.h>
#include <android/bitmap.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "fitz.h"
#include "mupdf.h"

#define  LOG_TAG    "libmupdf"
#define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

/* Set to 1 to enable debug log traces. */
#define DEBUG 0

/* Globals */
fz_colorspace  *colorspace;
fz_glyphcache  *glyphcache;
pdf_xref       *xref;
int             pagenum = 1;
int             resolution = 160;
float           pageWidth  = 100;
float           pageHeight = 100;
fz_displaylist *currentPageList;
fz_rect         currentMediabox;
int             currentRotate;

JNIEXPORT int JNICALL Java_com_artifex_mupdf_MuPDFCore_openFile(JNIEnv * env, jobject thiz, jstring jfilename)
{
    const char *filename;
    char *password = "";
    int accelerate = 1;
    fz_error error;

    filename = (*env)->GetStringUTFChars(env, jfilename, NULL);
    if (filename == NULL)
    {
        LOGE("Failed to get filename");
        return 0;
    }

    if (accelerate)
        fz_accelerate();
    glyphcache = fz_newglyphcache();
    colorspace = fz_devicergb;

    LOGE("Opening document...");
    error = pdf_openxref(&xref, filename, password);
    if (error)
    {
        LOGE("Cannot open document: '%s'\n", filename);
        return 0;
    }

    LOGE("Loading page tree...");
    error = pdf_loadpagetree(xref);
    if (error)
    {
        LOGE("Cannot load page tree: '%s'\n", filename);
        return 0;
    }
    LOGE("Done! %d pages", pdf_getpagecount(xref));

    return pdf_getpagecount(xref);
}

JNIEXPORT void JNICALL Java_com_artifex_mupdf_MuPDFCore_gotoPageInternal(
                                            JNIEnv  *env,
                                            jobject  thiz,
                                            int      page)
{
    float      zoom;
    fz_matrix  ctm;
    fz_obj    *pageobj;
    fz_bbox    bbox;
    fz_error   error;
    fz_device *dev;
    pdf_page  *currentPage;

    /* In the event of an error, ensure we give a non-empty page */
    pageWidth  = 100;
    pageHeight = 100;

    LOGE("Goto page %d...", page);
    if (currentPageList != NULL)
    {
	fz_freedisplaylist(currentPageList);
	currentPageList = NULL;
    }
    pagenum = page;
    pageobj = pdf_getpageobject(xref, pagenum);
    if (pageobj == NULL)
        return;
    error = pdf_loadpage(&currentPage, xref, pageobj);
    if (error)
        return;
    zoom = resolution / 72;
    currentMediabox = currentPage->mediabox;
    currentRotate   = currentPage->rotate;
    ctm = fz_translate(0, -currentMediabox.y1);
    ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
    ctm = fz_concat(ctm, fz_rotate(currentRotate));
    bbox = fz_roundrect(fz_transformrect(ctm, currentMediabox));
    pageWidth  = bbox.x1-bbox.x0;
    pageHeight = bbox.y1-bbox.y0;
    /* Render to list */
    currentPageList = fz_newdisplaylist();
    dev = fz_newlistdevice(currentPageList);
    error = pdf_runpage(xref, currentPage, dev, fz_identity);
    pdf_freepage(currentPage);
    if (error)
        LOGE("cannot make displaylist from page %d", pagenum);
    fz_freedevice(dev);
}

JNIEXPORT float JNICALL Java_com_artifex_mupdf_MuPDFCore_getPageWidth(
                                                               JNIEnv  *env,
                                                               jobject  thiz)
{
    LOGE("PageWidth=%g", pageWidth);
    return pageWidth;
}

JNIEXPORT float JNICALL Java_com_artifex_mupdf_MuPDFCore_getPageHeight(
                                                               JNIEnv  *env,
                                                               jobject  thiz)
{
    LOGE("PageHeight=%g", pageHeight);
    return pageHeight;
}


JNIEXPORT jboolean JNICALL Java_com_artifex_mupdf_MuPDFCore_drawPage(
                                            JNIEnv  *env,
                                            jobject  thiz,
                                            jobject  bitmap,
                                            int      pageW,
                                            int      pageH,
                                            int      patchX,
                                            int      patchY,
                                            int      patchW,
                                            int      patchH)
{
    AndroidBitmapInfo  info;
    void              *pixels;
    int                ret;
    fz_error           error;
    fz_device         *dev;
    float              zoom;
    fz_matrix          ctm;
    fz_bbox            bbox;
    fz_pixmap         *pix;
    float              xscale, yscale;

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
    LOGE("Rendering page=%dx%d patch=[%d,%d,%d,%d]",
         pageW, pageH, patchX, patchY, patchW, patchH);

    pix = fz_newpixmapwithdata(colorspace,
                               patchX,
                               patchY,
                               patchW,
                               patchH,
                               pixels);
    if (currentPageList == NULL)
    {
        fz_clearpixmapwithcolor(pix, 0xd0);
        return 0;
    }
    fz_clearpixmapwithcolor(pix, 0xff);

    zoom = resolution / 72;
    ctm = fz_translate(0, -currentMediabox.y1);
    ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
    ctm = fz_concat(ctm, fz_rotate(currentRotate));
    bbox = fz_roundrect(fz_transformrect(ctm,currentMediabox));
    /* Now, adjust ctm so that it would give the correct page width
     * heights. */
    xscale = (float)pageW/(float)(bbox.x1-bbox.x0);
    yscale = (float)pageH/(float)(bbox.y1-bbox.y0);
    ctm = fz_concat(ctm, fz_scale(xscale, yscale));
    dev = fz_newdrawdevice(glyphcache, pix);
    fz_executedisplaylist(currentPageList, dev, ctm);
    fz_freedevice(dev);
    fz_droppixmap(pix);
    LOGE("Rendered");

    AndroidBitmap_unlockPixels(env, bitmap);

    return 1;
}
