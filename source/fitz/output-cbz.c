#include "mupdf/fitz.h"

#include <zlib.h>

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct fz_cbz_writer_s fz_cbz_writer;

struct fz_cbz_writer_s
{
	fz_document_writer super;
	fz_draw_options options;
	fz_pixmap *pixmap;
	int count;
	fz_zip_writer *zip;
};

static fz_device *
cbz_begin_page(fz_context *ctx, fz_document_writer *wri_, const fz_rect *mediabox)
{
	fz_cbz_writer *wri = (fz_cbz_writer*)wri_;
	return fz_new_draw_device_with_options(ctx, &wri->options, mediabox, &wri->pixmap);
}

static void
cbz_end_page(fz_context *ctx, fz_document_writer *wri_, fz_device *dev)
{
	fz_cbz_writer *wri = (fz_cbz_writer*)wri_;
	fz_buffer *buffer;
	char name[40];

	fz_try(ctx)
		fz_close_device(ctx, dev);
	fz_always(ctx)
		fz_drop_device(ctx, dev);
	fz_catch(ctx)
		fz_rethrow(ctx);

	wri->count += 1;

	fz_snprintf(name, sizeof name, "p%04d.png", wri->count);

	buffer = fz_new_buffer_from_pixmap_as_png(ctx, wri->pixmap, NULL);
	fz_try(ctx)
		fz_write_zip_entry(ctx, wri->zip, name, buffer, 0);
	fz_always(ctx)
		fz_drop_buffer(ctx, buffer);
	fz_catch(ctx)
		fz_rethrow(ctx);

	fz_drop_pixmap(ctx, wri->pixmap);
	wri->pixmap = NULL;
}

static void
cbz_close_writer(fz_context *ctx, fz_document_writer *wri_)
{
	fz_cbz_writer *wri = (fz_cbz_writer*)wri_;
	fz_close_zip_writer(ctx, wri->zip);
}

static void
cbz_drop_writer(fz_context *ctx, fz_document_writer *wri_)
{
	fz_cbz_writer *wri = (fz_cbz_writer*)wri_;
	fz_drop_zip_writer(ctx, wri->zip);
	fz_drop_pixmap(ctx, wri->pixmap);
}

fz_document_writer *
fz_new_cbz_writer(fz_context *ctx, const char *path, const char *options)
{
	fz_cbz_writer *wri = fz_new_derived_document_writer(ctx, fz_cbz_writer, cbz_begin_page, cbz_end_page, cbz_close_writer, cbz_drop_writer);

	fz_try(ctx)
	{
		fz_parse_draw_options(ctx, &wri->options, options);
		wri->zip = fz_new_zip_writer(ctx, path ? path : "out.cbz");
	}
	fz_catch(ctx)
	{
		fz_free(ctx, wri);
		fz_rethrow(ctx);
	}

	return (fz_document_writer*)wri;
}

/* generic image file output writer */

typedef struct fz_pixmap_writer_s fz_pixmap_writer;

struct fz_pixmap_writer_s
{
	fz_document_writer super;
	fz_draw_options options;
	fz_pixmap *pixmap;
	void (*save)(fz_context *ctx, fz_pixmap *pix, const char *filename);
	int count;
	char *path;
};

static fz_device *
pixmap_begin_page(fz_context *ctx, fz_document_writer *wri_, const fz_rect *mediabox)
{
	fz_pixmap_writer *wri = (fz_pixmap_writer*)wri_;
	return fz_new_draw_device_with_options(ctx, &wri->options, mediabox, &wri->pixmap);
}

static void
pixmap_end_page(fz_context *ctx, fz_document_writer *wri_, fz_device *dev)
{
	fz_pixmap_writer *wri = (fz_pixmap_writer*)wri_;
	char path[PATH_MAX];

	fz_try(ctx)
		fz_close_device(ctx, dev);
	fz_always(ctx)
		fz_drop_device(ctx, dev);
	fz_catch(ctx)
		fz_rethrow(ctx);

	wri->count += 1;

	fz_format_output_path(ctx, path, sizeof path, wri->path, wri->count);
	wri->save(ctx, wri->pixmap, path);
	fz_drop_pixmap(ctx, wri->pixmap);
	wri->pixmap = NULL;
}

static void
pixmap_drop_writer(fz_context *ctx, fz_document_writer *wri_)
{
	fz_pixmap_writer *wri = (fz_pixmap_writer*)wri_;
	fz_drop_pixmap(ctx, wri->pixmap);
	fz_free(ctx, wri->path);
}

fz_document_writer *
fz_new_pixmap_writer(fz_context *ctx, const char *path, const char *options,
	const char *default_path, int n,
	void (*save)(fz_context *ctx, fz_pixmap *pix, const char *filename))
{
	fz_pixmap_writer *wri = fz_new_derived_document_writer(ctx, fz_pixmap_writer, pixmap_begin_page, pixmap_end_page, NULL, pixmap_drop_writer);

	fz_try(ctx)
	{
		fz_parse_draw_options(ctx, &wri->options, options);
		wri->path = fz_strdup(ctx, path ? path : default_path);
		wri->save = save;
		switch (n)
		{
		case 1: wri->options.colorspace = fz_device_gray(ctx); break;
		case 3: wri->options.colorspace = fz_device_rgb(ctx); break;
		case 4: wri->options.colorspace = fz_device_cmyk(ctx); break;
		}
	}
	fz_catch(ctx)
	{
		fz_free(ctx, wri);
		fz_rethrow(ctx);
	}

	return (fz_document_writer*)wri;
}
