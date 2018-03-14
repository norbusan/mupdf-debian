#ifndef MUPDF_FITZ_PIXMAP_H
#define MUPDF_FITZ_PIXMAP_H

#include "mupdf/fitz/system.h"
#include "mupdf/fitz/context.h"
#include "mupdf/fitz/geometry.h"
#include "mupdf/fitz/store.h"
#include "mupdf/fitz/colorspace.h"
#include "mupdf/fitz/separation.h"

/*
	Pixmaps represent a set of pixels for a 2 dimensional region of a
	plane. Each pixel has n components per pixel, the last of which is
	always alpha. The data is in premultiplied alpha when rendering, but
	non-premultiplied for colorspace conversions and rescaling.
*/
typedef struct fz_pixmap_s fz_pixmap;

typedef struct fz_overprint_s fz_overprint;

/*
	fz_pixmap_bbox: Return the bounding box for a pixmap.
*/
fz_irect *fz_pixmap_bbox(fz_context *ctx, const fz_pixmap *pix, fz_irect *bbox);

/*
	fz_pixmap_width: Return the width of the pixmap in pixels.
*/
int fz_pixmap_width(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_height: Return the height of the pixmap in pixels.
*/
int fz_pixmap_height(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_x: Return the x value of the pixmap in pixels.
*/
int fz_pixmap_x(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_y: Return the y value of the pixmap in pixels.
*/
int fz_pixmap_y(fz_context *ctx, fz_pixmap *pix);

/*
	fz_new_pixmap: Create a new pixmap, with its origin at (0,0)

	cs: The colorspace to use for the pixmap, or NULL for an alpha
	plane/mask.

	w: The width of the pixmap (in pixels)

	h: The height of the pixmap (in pixels)

	seps: Details of separations.

	alpha: 0 for no alpha, 1 for alpha.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap(fz_context *ctx, fz_colorspace *cs, int w, int h, fz_separations *seps, int alpha);

/*
	fz_new_pixmap_with_bbox: Create a pixmap of a given size,
	location and pixel format.

	The bounding box specifies the size of the created pixmap and
	where it will be located. The colorspace determines the number
	of components per pixel. Alpha is always present. Pixmaps are
	reference counted, so drop references using fz_drop_pixmap.

	colorspace: Colorspace format used for the created pixmap. The
	pixmap will keep a reference to the colorspace.

	bbox: Bounding box specifying location/size of created pixmap.

	seps: Details of separations.

	alpha: 0 for no alpha, 1 for alpha.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_with_bbox(fz_context *ctx, fz_colorspace *colorspace, const fz_irect *bbox, fz_separations *seps, int alpha);

/*
	fz_new_pixmap_with_data: Create a new pixmap, with its origin at
	(0,0) using the supplied data block.

	cs: The colorspace to use for the pixmap, or NULL for an alpha
	plane/mask.

	w: The width of the pixmap (in pixels)

	h: The height of the pixmap (in pixels)

	seps: Details of separations.

	alpha: 0 for no alpha, 1 for alpha.

	stride: The byte offset from the pixel data in a row to the pixel
	data in the next row.

	samples: The data block to keep the samples in.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_with_data(fz_context *ctx, fz_colorspace *colorspace, int w, int h, fz_separations *seps, int alpha, int stride, unsigned char *samples);

/*
	fz_new_pixmap_with_bbox_and_data: Create a pixmap of a given size,
	location and pixel format, using the supplied data block.

	The bounding box specifies the size of the created pixmap and
	where it will be located. The colorspace determines the number
	of components per pixel. Alpha is always present. Pixmaps are
	reference counted, so drop references using fz_drop_pixmap.

	colorspace: Colorspace format used for the created pixmap. The
	pixmap will keep a reference to the colorspace.

	rect: Bounding box specifying location/size of created pixmap.

	seps: Details of separations.

	alpha: Number of alpha planes (0 or 1).

	samples: The data block to keep the samples in.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_with_bbox_and_data(fz_context *ctx, fz_colorspace *colorspace, const fz_irect *rect, fz_separations *seps, int alpha, unsigned char *samples);

/*
	fz_new_pixmap_from_pixmap: Create a new pixmap that represents
	a subarea of the specified pixmap. A reference is taken to his
	pixmap that will be dropped on destruction.

	The supplied rectangle must be wholly contained within the original
	pixmap.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_from_pixmap(fz_context *ctx, fz_pixmap *pixmap, const fz_irect *rect);

/*
	fz_keep_pixmap: Take a reference to a pixmap.

	pix: The pixmap to increment the reference for.

	Returns pix.
*/
fz_pixmap *fz_keep_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_drop_pixmap: Drop a reference and free a pixmap.

	Decrement the reference count for the pixmap. When no
	references remain the pixmap will be freed.
*/
void fz_drop_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_colorspace: Return the colorspace of a pixmap

	Returns colorspace.
*/
fz_colorspace *fz_pixmap_colorspace(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_components: Return the number of components in a pixmap.

	Returns the number of components (including spots and alpha).
*/
int fz_pixmap_components(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_colorants: Return the number of colorants in a pixmap.

	Returns the number of colorants (components, less any spots and alpha).
*/
int fz_pixmap_colorants(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_spots: Return the number of spots in a pixmap.

	Returns the number of spots (components, less colorants and alpha). Does not throw exceptions.
*/
int fz_pixmap_spots(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_alpha: Return the number of alpha planes in a pixmap.

	Returns the number of alphas. Does not throw exceptions.
*/
int fz_pixmap_alpha(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_samples: Returns a pointer to the pixel data of a pixmap.

	Returns the pointer.
*/
unsigned char *fz_pixmap_samples(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_stride: Return the number of bytes in a row in the pixmap.
*/
int fz_pixmap_stride(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_set_resolution: Set the pixels per inch resolution of the pixmap.
*/
void fz_set_pixmap_resolution(fz_context *ctx, fz_pixmap *pix, int xres, int yres);

/*
	fz_clear_pixmap_with_value: Clears a pixmap with the given value.

	pix: The pixmap to clear.

	value: Values in the range 0 to 255 are valid. Each component
	sample for each pixel in the pixmap will be set to this value,
	while alpha will always be set to 255 (non-transparent).
*/
void fz_clear_pixmap_with_value(fz_context *ctx, fz_pixmap *pix, int value);

/*
	Fill pixmap with solid color.
*/
void fz_fill_pixmap_with_color(fz_context *ctx, fz_pixmap *pix, fz_colorspace *colorspace, float *color, const fz_color_params *color_params);

/*
	fz_clear_pixmap_with_value: Clears a subrect of a pixmap with the given value.

	pix: The pixmap to clear.

	value: Values in the range 0 to 255 are valid. Each component
	sample for each pixel in the pixmap will be set to this value,
	while alpha will always be set to 255 (non-transparent).

	r: the rectangle.
*/
void fz_clear_pixmap_rect_with_value(fz_context *ctx, fz_pixmap *pix, int value, const fz_irect *r);

/*
	fz_clear_pixmap_with_value: Sets all components (including alpha) of
	all pixels in a pixmap to 0.

	pix: The pixmap to clear.
*/
void fz_clear_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_invert_pixmap: Invert all the pixels in a pixmap. All components
	of all pixels are inverted (except alpha, which is unchanged).
*/
void fz_invert_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_tint_pixmap: Tint all the pixels in an RGB or Gray pixmap.

	Multiplies all the samples with the input color argument.

	r,g,b: The color to tint with, in 0 to 255 range.
*/
void fz_tint_pixmap(fz_context *ctx, fz_pixmap *pix, int r, int g, int b);

/*
	fz_invert_pixmap: Invert all the pixels in a given rectangle of a
	pixmap. All components of all pixels in the rectangle are inverted
	(except alpha, which is unchanged).
*/
void fz_invert_pixmap_rect(fz_context *ctx, fz_pixmap *image, const fz_irect *rect);

/*
	fz_gamma_pixmap: Apply gamma correction to a pixmap. All components
	of all pixels are modified (except alpha, which is unchanged).

	gamma: The gamma value to apply; 1.0 for no change.
*/
void fz_gamma_pixmap(fz_context *ctx, fz_pixmap *pix, float gamma);

/*
	fz_unmultiply_pixmap: Convert a pixmap from premultiplied to
	non-premultiplied format.
*/
void fz_unmultiply_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_convert_pixmap: Convert an existing pixmap to a desired
	colorspace. Other properties of the pixmap, such as resolution
	and position are copied to the converted pixmap.

	pix: The pixmap to convert.

	default_cs: If NULL pix->colorspace is used. It is possible that the data
	may need to be interpreted as one of the color spaces in default_cs.

	cs_des: Desired colorspace, may be NULL to denote alpha-only.

	prf: Proofing color space through which we need to convert.

	color_params: Parameters that may be used in conversion (e.g. ri).

	keep_alpha: If 0 any alpha component is removed, otherwise
	alpha is kept if present in the pixmap.
*/
fz_pixmap *fz_convert_pixmap(fz_context *ctx, fz_pixmap *pix, fz_colorspace *cs_des, fz_colorspace *prf, fz_default_colorspaces *default_cs, const fz_color_params *color_params, int keep_alpha);

/*
	Pixmaps represent a set of pixels for a 2 dimensional region of a
	plane. Each pixel has n components per pixel, the last of which is
	always alpha. The data is in premultiplied alpha when rendering, but
	non-premultiplied for colorspace conversions and rescaling.

	x, y: The minimum x and y coord of the region in pixels.

	w, h: The width and height of the region in pixels.

	n: The number of color components in the image.
		n = num composite colors + num spots + num alphas

	s: The number of spot channels in the image.

	alpha: 0 for no alpha, 1 for alpha present.

	flags: flag bits.
		Bit 0: If set, draw the image with linear interpolation.
		Bit 1: If set, free the samples buffer when the pixmap
		is destroyed.

	stride: The byte offset from the data for any given pixel
	to the data for the same pixel on the row below.

	seps: NULL, or a pointer to a separations structure. If NULL,
	s should be 0.

	xres, yres: Image resolution in dpi. Default is 96 dpi.

	colorspace: Pointer to a colorspace object describing the colorspace
	the pixmap is in. If NULL, the image is a mask.

	samples: A simple block of memory w * h * n bytes of memory in which
	the components are stored. The first n bytes are components 0 to n-1
	for the pixel at (x,y). Each successive n bytes gives another pixel
	in scanline order. Subsequent scanlines follow on with no padding.
*/
struct fz_pixmap_s
{
	fz_storable storable;
	int x, y, w, h;
	unsigned char n;
	unsigned char s;
	unsigned char alpha;
	unsigned char flags;
	ptrdiff_t stride;
	fz_separations *seps;
	int xres, yres;
	fz_colorspace *colorspace;
	unsigned char *samples;
	fz_pixmap *underlying;
};

enum
{
	FZ_PIXMAP_FLAG_INTERPOLATE = 1,
	FZ_PIXMAP_FLAG_FREE_SAMPLES = 2
};

void fz_drop_pixmap_imp(fz_context *ctx, fz_storable *pix);

void fz_copy_pixmap_rect(fz_context *ctx, fz_pixmap *dest, fz_pixmap *src, const fz_irect *r, const fz_default_colorspaces *default_cs);
void fz_premultiply_pixmap(fz_context *ctx, fz_pixmap *pix);
fz_pixmap *fz_alpha_from_gray(fz_context *ctx, fz_pixmap *gray);
size_t fz_pixmap_size(fz_context *ctx, fz_pixmap *pix);

fz_pixmap *fz_scale_pixmap(fz_context *ctx, fz_pixmap *src, float x, float y, float w, float h, fz_irect *clip);

typedef struct fz_scale_cache_s fz_scale_cache;

fz_scale_cache *fz_new_scale_cache(fz_context *ctx);
void fz_drop_scale_cache(fz_context *ctx, fz_scale_cache *cache);
fz_pixmap *fz_scale_pixmap_cached(fz_context *ctx, const fz_pixmap *src, float x, float y, float w, float h, const fz_irect *clip, fz_scale_cache *cache_x, fz_scale_cache *cache_y);

void fz_subsample_pixmap(fz_context *ctx, fz_pixmap *tile, int factor);

fz_irect *fz_pixmap_bbox_no_ctx(const fz_pixmap *src, fz_irect *bbox);

void fz_decode_tile(fz_context *ctx, fz_pixmap *pix, const float *decode);
void fz_decode_indexed_tile(fz_context *ctx, fz_pixmap *pix, const float *decode, int maxval);
void fz_unpack_tile(fz_context *ctx, fz_pixmap *dst, unsigned char * restrict src, int n, int depth, size_t stride, int scale);

/*
	fz_pixmap_converter: Color convert a pixmap. The passing of default_cs is needed due to the base cs of the image possibly
	needing to be treated as being in one of the page default color spaces.
*/
typedef void (fz_pixmap_converter)(fz_context *ctx, fz_pixmap *dp, fz_pixmap *sp, fz_colorspace *prf, const fz_default_colorspaces *default_cs, const fz_color_params *color_params, int copy_spots);
fz_pixmap_converter *fz_lookup_pixmap_converter(fz_context *ctx, fz_colorspace *ds, fz_colorspace *ss);

/*
	fz_md5_pixmap: Return the md5 digest for a pixmap
*/
void fz_md5_pixmap(fz_context *ctx, fz_pixmap *pixmap, unsigned char digest[16]);

fz_pixmap *fz_new_pixmap_from_8bpp_data(fz_context *ctx, int x, int y, int w, int h, unsigned char *sp, int span);
fz_pixmap *fz_new_pixmap_from_1bpp_data(fz_context *ctx, int x, int y, int w, int h, unsigned char *sp, int span);

#ifdef HAVE_VALGRIND
int fz_valgrind_pixmap(const fz_pixmap *pix);
#else
#define fz_valgrind_pixmap(pix) do {} while (0)
#endif

/*
	fz_clone_pixmap_area_with_different_seps: Convert between
	different separation results.
*/
fz_pixmap *fz_clone_pixmap_area_with_different_seps(fz_context *ctx, fz_pixmap *src, const fz_irect *bbox, fz_colorspace *dcs, fz_separations *seps, const fz_color_params *color_params, fz_default_colorspaces *default_cs);

fz_pixmap *fz_copy_pixmap_area_converting_seps(fz_context *ctx, fz_pixmap *dst, fz_pixmap *src, const fz_color_params *color_params, fz_colorspace *prf, fz_default_colorspaces *default_cs);

#endif
