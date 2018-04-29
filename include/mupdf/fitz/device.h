#ifndef MUPDF_FITZ_DEVICE_H
#define MUPDF_FITZ_DEVICE_H

#include "mupdf/fitz/system.h"
#include "mupdf/fitz/context.h"
#include "mupdf/fitz/geometry.h"
#include "mupdf/fitz/colorspace.h"
#include "mupdf/fitz/image.h"
#include "mupdf/fitz/shade.h"
#include "mupdf/fitz/path.h"
#include "mupdf/fitz/text.h"

/*
	The different format handlers (pdf, xps etc) interpret pages to a
	device. These devices can then process the stream of calls they
	receive in various ways:
		The trace device outputs debugging information for the calls.
		The draw device will render them.
		The list device stores them in a list to play back later.
		The text device performs text extraction and searching.
		The bbox device calculates the bounding box for the page.
	Other devices can (and will) be written in the future.
*/
typedef struct fz_device_s fz_device;

enum
{
	/* Flags */
	FZ_DEVFLAG_MASK = 1,
	FZ_DEVFLAG_COLOR = 2,
	FZ_DEVFLAG_UNCACHEABLE = 4,
	FZ_DEVFLAG_FILLCOLOR_UNDEFINED = 8,
	FZ_DEVFLAG_STROKECOLOR_UNDEFINED = 16,
	FZ_DEVFLAG_STARTCAP_UNDEFINED = 32,
	FZ_DEVFLAG_DASHCAP_UNDEFINED = 64,
	FZ_DEVFLAG_ENDCAP_UNDEFINED = 128,
	FZ_DEVFLAG_LINEJOIN_UNDEFINED = 256,
	FZ_DEVFLAG_MITERLIMIT_UNDEFINED = 512,
	FZ_DEVFLAG_LINEWIDTH_UNDEFINED = 1024,
	/* Arguably we should have a bit for the dash pattern itself being
	 * undefined, but that causes problems; do we assume that it should
	 * always be set to non-dashing at the start of every glyph? */
	FZ_DEVFLAG_BBOX_DEFINED = 2048,
	FZ_DEVFLAG_GRIDFIT_AS_TILED = 4096,
};

enum
{
	/* PDF 1.4 -- standard separable */
	FZ_BLEND_NORMAL,
	FZ_BLEND_MULTIPLY,
	FZ_BLEND_SCREEN,
	FZ_BLEND_OVERLAY,
	FZ_BLEND_DARKEN,
	FZ_BLEND_LIGHTEN,
	FZ_BLEND_COLOR_DODGE,
	FZ_BLEND_COLOR_BURN,
	FZ_BLEND_HARD_LIGHT,
	FZ_BLEND_SOFT_LIGHT,
	FZ_BLEND_DIFFERENCE,
	FZ_BLEND_EXCLUSION,

	/* PDF 1.4 -- standard non-separable */
	FZ_BLEND_HUE,
	FZ_BLEND_SATURATION,
	FZ_BLEND_COLOR,
	FZ_BLEND_LUMINOSITY,

	/* For packing purposes */
	FZ_BLEND_MODEMASK = 15,
	FZ_BLEND_ISOLATED = 16,
	FZ_BLEND_KNOCKOUT = 32
};

int fz_lookup_blendmode(const char *name);
char *fz_blendmode_name(int blendmode);

typedef struct fz_device_container_stack_s fz_device_container_stack;

struct fz_device_s
{
	int refs;
	int hints;
	int flags;

	void (*close_device)(fz_context *, fz_device *);
	void (*drop_device)(fz_context *, fz_device *);

	void (*fill_path)(fz_context *, fz_device *, const fz_path *, int even_odd, const fz_matrix *, fz_colorspace *, const float *color, float alpha, const fz_color_params *);
	void (*stroke_path)(fz_context *, fz_device *, const fz_path *, const fz_stroke_state *, const fz_matrix *, fz_colorspace *, const float *color, float alpha, const fz_color_params *);
	void (*clip_path)(fz_context *, fz_device *, const fz_path *, int even_odd, const fz_matrix *, const fz_rect *scissor);
	void (*clip_stroke_path)(fz_context *, fz_device *, const fz_path *, const fz_stroke_state *, const fz_matrix *, const fz_rect *scissor);

	void (*fill_text)(fz_context *, fz_device *, const fz_text *, const fz_matrix *, fz_colorspace *, const float *color, float alpha, const fz_color_params *);
	void (*stroke_text)(fz_context *, fz_device *, const fz_text *, const fz_stroke_state *, const fz_matrix *, fz_colorspace *, const float *color, float alpha, const fz_color_params *);
	void (*clip_text)(fz_context *, fz_device *, const fz_text *, const fz_matrix *, const fz_rect *scissor);
	void (*clip_stroke_text)(fz_context *, fz_device *, const fz_text *, const fz_stroke_state *, const fz_matrix *, const fz_rect *scissor);
	void (*ignore_text)(fz_context *, fz_device *, const fz_text *, const fz_matrix *);

	void (*fill_shade)(fz_context *, fz_device *, fz_shade *shd, const fz_matrix *ctm, float alpha, const fz_color_params *color_params);
	void (*fill_image)(fz_context *, fz_device *, fz_image *img, const fz_matrix *ctm, float alpha, const fz_color_params *color_params);
	void (*fill_image_mask)(fz_context *, fz_device *, fz_image *img, const fz_matrix *ctm, fz_colorspace *, const float *color, float alpha, const fz_color_params *color_params);
	void (*clip_image_mask)(fz_context *, fz_device *, fz_image *img, const fz_matrix *ctm, const fz_rect *scissor);

	void (*pop_clip)(fz_context *, fz_device *);

	void (*begin_mask)(fz_context *, fz_device *, const fz_rect *, int luminosity, fz_colorspace *, const float *bc, const fz_color_params *);
	void (*end_mask)(fz_context *, fz_device *);
	void (*begin_group)(fz_context *, fz_device *, const fz_rect *, fz_colorspace *cs, int isolated, int knockout, int blendmode, float alpha);
	void (*end_group)(fz_context *, fz_device *);

	int (*begin_tile)(fz_context *, fz_device *, const fz_rect *area, const fz_rect *view, float xstep, float ystep, const fz_matrix *ctm, int id);
	void (*end_tile)(fz_context *, fz_device *);

	void (*render_flags)(fz_context *, fz_device *, int set, int clear);
	void (*set_default_colorspaces)(fz_context *, fz_device *, fz_default_colorspaces *);

	void (*begin_layer)(fz_context *, fz_device *, const char *layer_name);
	void (*end_layer)(fz_context *, fz_device *);

	fz_rect d1_rect;

	int error_depth;
	char errmess[256];

	int container_len;
	int container_cap;
	fz_device_container_stack *container;
};

void fz_fill_path(fz_context *ctx, fz_device *dev, const fz_path *path, int even_odd, const fz_matrix *ctm, fz_colorspace *colorspace, const float *color, float alpha, const fz_color_params *color_params);
void fz_stroke_path(fz_context *ctx, fz_device *dev, const fz_path *path, const fz_stroke_state *stroke, const fz_matrix *ctm, fz_colorspace *colorspace, const float *color, float alpha, const fz_color_params *color_params);
void fz_clip_path(fz_context *ctx, fz_device *dev, const fz_path *path, int even_odd, const fz_matrix *ctm, const fz_rect *scissor);
void fz_clip_stroke_path(fz_context *ctx, fz_device *dev, const fz_path *path, const fz_stroke_state *stroke, const fz_matrix *ctm, const fz_rect *scissor);
void fz_fill_text(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_matrix *ctm, fz_colorspace *colorspace, const float *color, float alpha, const fz_color_params *color_params);
void fz_stroke_text(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_stroke_state *stroke, const fz_matrix *ctm, fz_colorspace *colorspace, const float *color, float alpha, const fz_color_params *color_params);
void fz_clip_text(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_matrix *ctm, const fz_rect *scissor);
void fz_clip_stroke_text(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_stroke_state *stroke, const fz_matrix *ctm, const fz_rect *scissor);
void fz_ignore_text(fz_context *ctx, fz_device *dev, const fz_text *text, const fz_matrix *ctm);
void fz_pop_clip(fz_context *ctx, fz_device *dev);
void fz_fill_shade(fz_context *ctx, fz_device *dev, fz_shade *shade, const fz_matrix *ctm, float alpha, const fz_color_params *color_params);
void fz_fill_image(fz_context *ctx, fz_device *dev, fz_image *image, const fz_matrix *ctm, float alpha, const fz_color_params *color_params);
void fz_fill_image_mask(fz_context *ctx, fz_device *dev, fz_image *image, const fz_matrix *ctm, fz_colorspace *colorspace, const float *color, float alpha, const fz_color_params *color_params);
void fz_clip_image_mask(fz_context *ctx, fz_device *dev, fz_image *image, const fz_matrix *ctm, const fz_rect *scissor);
void fz_begin_mask(fz_context *ctx, fz_device *dev, const fz_rect *area, int luminosity, fz_colorspace *colorspace, const float *bc, const fz_color_params *color_params);
void fz_end_mask(fz_context *ctx, fz_device *dev);
void fz_begin_group(fz_context *ctx, fz_device *dev, const fz_rect *area, fz_colorspace *cs, int isolated, int knockout, int blendmode, float alpha);
void fz_end_group(fz_context *ctx, fz_device *dev);
void fz_begin_tile(fz_context *ctx, fz_device *dev, const fz_rect *area, const fz_rect *view, float xstep, float ystep, const fz_matrix *ctm);
int fz_begin_tile_id(fz_context *ctx, fz_device *dev, const fz_rect *area, const fz_rect *view, float xstep, float ystep, const fz_matrix *ctm, int id);
void fz_end_tile(fz_context *ctx, fz_device *dev);
void fz_render_flags(fz_context *ctx, fz_device *dev, int set, int clear);
void fz_set_default_colorspaces(fz_context *ctx, fz_device *dev, fz_default_colorspaces *default_cs);
void fz_begin_layer(fz_context *ctx, fz_device *dev, const char *layer_name);
void fz_end_layer(fz_context *ctx, fz_device *dev);
fz_device *fz_new_device_of_size(fz_context *ctx, int size);

#define fz_new_derived_device(CTX, TYPE) \
	((TYPE *)Memento_label(fz_new_device_of_size(ctx,sizeof(TYPE)),#TYPE))

/*
	fz_close_device: Signal the end of input, and flush any buffered output.
	This is NOT called implicitly on fz_drop_device.
*/
void fz_close_device(fz_context *ctx, fz_device *dev);

/*
	fz_drop_device: Free a device of any type and its resources.
	Don't forget to call fz_close_device before dropping the device,
	or you may get incomplete output!
*/
void fz_drop_device(fz_context *ctx, fz_device *dev);

fz_device *fz_keep_device(fz_context *ctx, fz_device *dev);

/*
	fz_enable_device_hints : Enable hints in a device.

	hints: mask of hints to enable.
*/
void fz_enable_device_hints(fz_context *ctx, fz_device *dev, int hints);

/*
	fz_disable_device_hints : Disable hints in a device.

	hints: mask of hints to disable.
*/
void fz_disable_device_hints(fz_context *ctx, fz_device *dev, int hints);

/*
	Find current scissor region as tracked by the device.
*/
const fz_rect *fz_device_current_scissor(fz_context *ctx, fz_device *dev);

enum
{
	/* Hints */
	FZ_DONT_INTERPOLATE_IMAGES = 1,
	FZ_MAINTAIN_CONTAINER_STACK = 2,
	FZ_NO_CACHE = 4,
};

/*
	Cookie support - simple communication channel between app/library.
*/

typedef struct fz_cookie_s fz_cookie;

/*
	Provide two-way communication between application and library.
	Intended for multi-threaded applications where one thread is
	rendering pages and another thread wants to read progress
	feedback or abort a job that takes a long time to finish. The
	communication is unsynchronized without locking.

	abort: The application should set this field to 0 before
	calling fz_run_page to render a page. At any point when the
	page is being rendered the application my set this field to 1
	which will cause the rendering to finish soon. This field is
	checked periodically when the page is rendered, but exactly
	when is not known, therefore there is no upper bound on
	exactly when the rendering will abort. If the application
	did not provide a set of locks to fz_new_context, it must also
	await the completion of fz_run_page before issuing another
	call to fz_run_page. Note that once the application has set
	this field to 1 after it called fz_run_page it may not change
	the value again.

	progress: Communicates rendering progress back to the
	application and is read only. Increments as a page is being
	rendered. The value starts out at 0 and is limited to less
	than or equal to progress_max, unless progress_max is -1.

	progress_max: Communicates the known upper bound of rendering
	back to the application and is read only. The maximum value
	that the progress field may take. If there is no known upper
	bound on how long the rendering may take this value is -1 and
	progress is not limited. Note that the value of progress_max
	may change from -1 to a positive value once an upper bound is
	known, so take this into consideration when comparing the
	value of progress to that of progress_max.

	errors: count of errors during current rendering.

	incomplete_ok: If this is set to 1 by the caller, then TRYLATER
	errors are swallowed as they occur, setting the 'incomplete' flag.
	Rendering continues as much as possible ignoring errors. The caller
	is expected to check the 'incomplete' flag at the end to see if the
	rendering may be considered final or not.

	incomplete: Initially should be set to 0. Will be set to non-zero
	if a TRYLATER error is thrown during rendering and the incomplete_ok
	flag is set.
*/
struct fz_cookie_s
{
	int abort;
	int progress;
	int progress_max; /* -1 for unknown */
	int errors;
	int incomplete_ok;
	int incomplete;
};

/*
	fz_new_trace_device: Create a device to print a debug trace of all device calls.
*/
fz_device *fz_new_trace_device(fz_context *ctx, fz_output *out);

/*
	fz_new_bbox_device: Create a device to compute the bounding
	box of all marks on a page.

	The returned bounding box will be the union of all bounding
	boxes of all objects on a page.
*/
fz_device *fz_new_bbox_device(fz_context *ctx, fz_rect *rectp);

/*
	fz_new_test_device: Create a device to test for features.

	Currently only tests for the presence of non-grayscale colors.

	is_color: Possible values returned:
		0: Definitely greyscale
		1: Probably color (all colors were grey, but there
		were images or shadings in a non grey colorspace).
		2: Definitely color

	threshold: The difference from grayscale that will be tolerated.
	Typical values to use are either 0 (be exact) and 0.02 (allow an
	imperceptible amount of slop).

	options: A set of bitfield options, from the FZ_TEST_OPT set.

	passthrough: A device to pass all calls through to, or NULL.
	If set, then the test device can both test and pass through to
	an underlying device (like, say, the display list device). This
	means that a display list can be created and at the end we'll
	know if it's colored or not.

	In the absence of a passthrough device, the device will throw
	an exception to stop page interpretation when color is found.
*/
fz_device *fz_new_test_device(fz_context *ctx, int *is_color, float threshold, int options, fz_device *passthrough);

enum
{
	/* If set, test every pixel of images exhaustively.
	 * If clear, just look at colorspaces for images. */
	FZ_TEST_OPT_IMAGES = 1,

	/* If set, test every pixel of shadings. */
	/* If clear, just look at colorspaces for shadings. */
	FZ_TEST_OPT_SHADINGS = 2
};

/*
	fz_new_draw_device: Create a device to draw on a pixmap.

	dest: Target pixmap for the draw device. See fz_new_pixmap*
	for how to obtain a pixmap. The pixmap is not cleared by the
	draw device, see fz_clear_pixmap* for how to clear it prior to
	calling fz_new_draw_device. Free the device by calling
	fz_drop_device.

	transform: Transform from user space in points to device space in pixels.
*/
fz_device *fz_new_draw_device(fz_context *ctx, const fz_matrix *transform, fz_pixmap *dest);

/*
	fz_new_draw_device_with_bbox: Create a device to draw on a pixmap.

	dest: Target pixmap for the draw device. See fz_new_pixmap*
	for how to obtain a pixmap. The pixmap is not cleared by the
	draw device, see fz_clear_pixmap* for how to clear it prior to
	calling fz_new_draw_device. Free the device by calling
	fz_drop_device.

	transform: Transform from user space in points to device space in pixels.

	clip: Bounding box to restrict any marking operations of the
	draw device.
*/
fz_device *fz_new_draw_device_with_bbox(fz_context *ctx, const fz_matrix *transform, fz_pixmap *dest, const fz_irect *clip);

/*
	fz_new_draw_device_with_proof: Create a device to draw on a pixmap.

	dest: Target pixmap for the draw device. See fz_new_pixmap*
	for how to obtain a pixmap. The pixmap is not cleared by the
	draw device, see fz_clear_pixmap* for how to clear it prior to
	calling fz_new_draw_device. Free the device by calling
	fz_drop_device.

	transform: Transform from user space in points to device space in pixels.

	proof_cs: Intermediate color space to map though when mapping to
	color space defined by pixmap.
*/
fz_device *fz_new_draw_device_with_proof(fz_context *ctx, const fz_matrix *transform, fz_pixmap *dest, fz_colorspace *proof_cs);

/*
	fz_new_draw_device_with_bbox_proof: Create a device to draw on a pixmap.

	dest: Target pixmap for the draw device. See fz_new_pixmap*
	for how to obtain a pixmap. The pixmap is not cleared by the
	draw device, see fz_clear_pixmap* for how to clear it prior to
	calling fz_new_draw_device. Free the device by calling
	fz_drop_device.

	transform: Transform from user space in points to device space in pixels.

	clip: Bounding box to restrict any marking operations of the
	draw device.

	proof_cs: Color space to render to prior to mapping to color space defined by pixmap.
*/
fz_device *fz_new_draw_device_with_bbox_proof(fz_context *ctx, const fz_matrix *transform, fz_pixmap *dest, const fz_irect *clip, fz_colorspace *cs);

fz_device *fz_new_draw_device_type3(fz_context *ctx, const fz_matrix *transform, fz_pixmap *dest);

/*
	struct fz_draw_options: Options for creating a pixmap and draw device.
*/
typedef struct fz_draw_options_s fz_draw_options;

struct fz_draw_options_s
{
	int rotate;
	int x_resolution;
	int y_resolution;
	int width;
	int height;
	fz_colorspace *colorspace;
	int alpha;
	int graphics;
	int text;
};

extern const char *fz_draw_options_usage;

/*
	fz_parse_draw_options: Parse draw device options from a comma separated key-value string.
*/
fz_draw_options *fz_parse_draw_options(fz_context *ctx, fz_draw_options *options, const char *string);

/*
	fz_new_draw_device_with_options: Create a new pixmap and draw device, using the specified options.

	options: Options to configure the draw device, and choose the resolution and colorspace.
	mediabox: The bounds of the page in points.
	pixmap: An out parameter containing the newly created pixmap.
*/
fz_device *fz_new_draw_device_with_options(fz_context *ctx, const fz_draw_options *options, const fz_rect *mediabox, fz_pixmap **pixmap);

#endif
