#ifndef MUPDF_FITZ_COLORSPACE_H
#define MUPDF_FITZ_COLORSPACE_H

#include "mupdf/fitz/system.h"
#include "mupdf/fitz/context.h"
#include "mupdf/fitz/store.h"

enum { FZ_MAX_COLORS = 32 };

/*
	An fz_colorspace object represents an abstract colorspace. While
	this should be treated as a black box by callers of the library at
	this stage, know that it encapsulates knowledge of how to convert
	colors to and from the colorspace, any lookup tables generated, the
	number of components in the colorspace etc.
*/
typedef struct fz_colorspace_s fz_colorspace;

/*
	fz_lookup_device_colorspace: Find a standard colorspace based upon
	it's name.
*/
fz_colorspace *fz_lookup_device_colorspace(fz_context *ctx, char *name);

/*
	fz_colorspace_is_indexed: Return true, iff a given colorspace is
	indexed.
*/
int fz_colorspace_is_indexed(fz_context *ctx, fz_colorspace *cs);

/*
	fz_device_gray: Get colorspace representing device specific gray.
*/
fz_colorspace *fz_device_gray(fz_context *ctx);

/*
	fz_device_rgb: Get colorspace representing device specific rgb.
*/
fz_colorspace *fz_device_rgb(fz_context *ctx);

/*
	fz_device_bgr: Get colorspace representing device specific bgr.
*/
fz_colorspace *fz_device_bgr(fz_context *ctx);

/*
	fz_device_cmyk: Get colorspace representing device specific CMYK.
*/
fz_colorspace *fz_device_cmyk(fz_context *ctx);

/*
	fz_set_device_gray: Set colorspace representing device specific gray.
*/
void fz_set_device_gray(fz_context *ctx, fz_colorspace *cs);

/*
	fz_set_device_rgb: Set colorspace representing device specific rgb.
*/
void fz_set_device_rgb(fz_context *ctx, fz_colorspace *cs);

/*
	fz_set_device_bgr: Set colorspace representing device specific bgr.
*/
void fz_set_device_bgr(fz_context *ctx, fz_colorspace *cs);

/*
	fz_set_device_cmyk: Set colorspace representing device specific CMYK.
*/
void fz_set_device_cmyk(fz_context *ctx, fz_colorspace *cs);

struct fz_colorspace_s
{
	fz_storable storable;
	unsigned int size;
	char name[16];
	int n;
	void (*to_rgb)(fz_context *ctx, fz_colorspace *, const float *src, float *rgb);
	void (*from_rgb)(fz_context *ctx, fz_colorspace *, const float *rgb, float *dst);
	void (*free_data)(fz_context *Ctx, fz_colorspace *);
	void *data;
};

fz_colorspace *fz_new_colorspace(fz_context *ctx, char *name, int n);
fz_colorspace *fz_new_indexed_colorspace(fz_context *ctx, fz_colorspace *base, int high, unsigned char *lookup);
fz_colorspace *fz_keep_colorspace(fz_context *ctx, fz_colorspace *colorspace);
void fz_drop_colorspace(fz_context *ctx, fz_colorspace *colorspace);
void fz_drop_colorspace_imp(fz_context *ctx, fz_storable *colorspace);

void fz_convert_color(fz_context *ctx, fz_colorspace *dsts, float *dstv, fz_colorspace *srcs, const float *srcv);

void fz_new_colorspace_context(fz_context *ctx);
fz_colorspace_context *fz_keep_colorspace_context(fz_context *ctx);
void fz_drop_colorspace_context(fz_context *ctx);

typedef struct fz_color_converter_s fz_color_converter;

/* This structure is public because it allows us to avoid dynamic allocations.
 * Callers should only rely on the convert entry - the rest of the structure
 * is subject to change without notice.
 */
struct fz_color_converter_s
{
	void (*convert)(fz_context *, fz_color_converter *, float *, const float *);
	fz_colorspace *ds;
	fz_colorspace *ss;
	void *opaque;
};

void fz_lookup_color_converter(fz_context *ctx, fz_color_converter *cc, fz_colorspace *ds, fz_colorspace *ss);

void fz_init_cached_color_converter(fz_context *ctx, fz_color_converter *cc, fz_colorspace *ds, fz_colorspace *ss);
void fz_fin_cached_color_converter(fz_context *ctx, fz_color_converter *cc);

#endif
