#ifndef MUPDF_PDF_ANNOT_H
#define MUPDF_PDF_ANNOT_H

enum pdf_annot_type
{
	PDF_ANNOT_TEXT,
	PDF_ANNOT_LINK,
	PDF_ANNOT_FREE_TEXT,
	PDF_ANNOT_LINE,
	PDF_ANNOT_SQUARE,
	PDF_ANNOT_CIRCLE,
	PDF_ANNOT_POLYGON,
	PDF_ANNOT_POLY_LINE,
	PDF_ANNOT_HIGHLIGHT,
	PDF_ANNOT_UNDERLINE,
	PDF_ANNOT_SQUIGGLY,
	PDF_ANNOT_STRIKE_OUT,
	PDF_ANNOT_STAMP,
	PDF_ANNOT_CARET,
	PDF_ANNOT_INK,
	PDF_ANNOT_POPUP,
	PDF_ANNOT_FILE_ATTACHMENT,
	PDF_ANNOT_SOUND,
	PDF_ANNOT_MOVIE,
	PDF_ANNOT_WIDGET,
	PDF_ANNOT_SCREEN,
	PDF_ANNOT_PRINTER_MARK,
	PDF_ANNOT_TRAP_NET,
	PDF_ANNOT_WATERMARK,
	PDF_ANNOT_3D,
	PDF_ANNOT_UNKNOWN = -1
};

const char *pdf_string_from_annot_type(fz_context *ctx, enum pdf_annot_type type);
enum pdf_annot_type pdf_annot_type_from_string(fz_context *ctx, const char *subtype);

enum
{
	PDF_ANNOT_IS_INVISIBLE = 1 << (1-1),
	PDF_ANNOT_IS_HIDDEN = 1 << (2-1),
	PDF_ANNOT_IS_PRINT = 1 << (3-1),
	PDF_ANNOT_IS_NO_ZOOM = 1 << (4-1),
	PDF_ANNOT_IS_NO_ROTATE = 1 << (5-1),
	PDF_ANNOT_IS_NO_VIEW = 1 << (6-1),
	PDF_ANNOT_IS_READ_ONLY = 1 << (7-1),
	PDF_ANNOT_IS_LOCKED = 1 << (8-1),
	PDF_ANNOT_IS_TOGGLE_NO_VIEW = 1 << (9-1),
	PDF_ANNOT_IS_LOCKED_CONTENTS = 1 << (10-1)
};

enum pdf_line_ending
{
	PDF_ANNOT_LE_NONE = 0,
	PDF_ANNOT_LE_SQUARE,
	PDF_ANNOT_LE_CIRCLE,
	PDF_ANNOT_LE_DIAMOND,
	PDF_ANNOT_LE_OPEN_ARROW,
	PDF_ANNOT_LE_CLOSED_ARROW,
	PDF_ANNOT_LE_BUTT,
	PDF_ANNOT_LE_R_OPEN_ARROW,
	PDF_ANNOT_LE_R_CLOSED_ARROW,
	PDF_ANNOT_LE_SLASH
};

enum
{
	PDF_ANNOT_Q_LEFT = 0,
	PDF_ANNOT_Q_CENTER = 1,
	PDF_ANNOT_Q_RIGHT = 2
};

enum pdf_line_ending pdf_line_ending_from_name(fz_context *ctx, pdf_obj *end);
enum pdf_line_ending pdf_line_ending_from_string(fz_context *ctx, const char *end);
pdf_obj *pdf_name_from_line_ending(fz_context *ctx, enum pdf_line_ending end);
const char *pdf_string_from_line_ending(fz_context *ctx, enum pdf_line_ending end);

/*
	pdf_first_annot: Return the first annotation on a page.
*/
pdf_annot *pdf_first_annot(fz_context *ctx, pdf_page *page);

/*
	pdf_next_annot: Return the next annotation on a page.
*/
pdf_annot *pdf_next_annot(fz_context *ctx, pdf_annot *annot);

/*
	pdf_bound_annot: Return the rectangle for an annotation on a page.
*/
fz_rect *pdf_bound_annot(fz_context *ctx, pdf_annot *annot, fz_rect *rect);

/*
	pdf_annot_type: Return the type of an annotation
*/
int pdf_annot_type(fz_context *ctx, pdf_annot *annot);

/*
	pdf_run_annot: Interpret an annotation and render it on a device.

	page: A page loaded by pdf_load_page.

	annot: an annotation.

	dev: Device used for rendering, obtained from fz_new_*_device.

	ctm: A transformation matrix applied to the objects on the page,
	e.g. to scale or rotate the page contents as desired.
*/
void pdf_run_annot(fz_context *ctx, pdf_annot *annot, fz_device *dev, const fz_matrix *ctm, fz_cookie *cookie);

struct pdf_annot_s
{
	fz_annot super;
	pdf_page *page;
	pdf_obj *obj;

	pdf_obj *ap;

	int needs_new_ap;
	int has_new_ap;

	pdf_annot *next;
};

char *pdf_parse_file_spec(fz_context *ctx, pdf_document *doc, pdf_obj *file_spec, pdf_obj *dest);
char *pdf_parse_link_dest(fz_context *ctx, pdf_document *doc, pdf_obj *obj);
char *pdf_parse_link_action(fz_context *ctx, pdf_document *doc, pdf_obj *obj, int pagenum);
pdf_obj *pdf_lookup_dest(fz_context *ctx, pdf_document *doc, pdf_obj *needle);
pdf_obj *pdf_lookup_name(fz_context *ctx, pdf_document *doc, pdf_obj *which, pdf_obj *needle);
pdf_obj *pdf_load_name_tree(fz_context *ctx, pdf_document *doc, pdf_obj *which);

int pdf_resolve_link(fz_context *ctx, pdf_document *doc, const char *uri, float *xp, float *yp);
fz_link *pdf_load_link_annots(fz_context *ctx, pdf_document *, pdf_obj *annots, int pagenum, const fz_matrix *page_ctm);

void pdf_annot_transform(fz_context *ctx, pdf_annot *annot, fz_matrix *annot_ctm);
void pdf_load_annots(fz_context *ctx, pdf_page *page, pdf_obj *annots);
void pdf_update_annot(fz_context *ctx, pdf_annot *annot);
void pdf_drop_annots(fz_context *ctx, pdf_annot *annot_list);

/*
	pdf_create_annot: create a new annotation of the specified type on the
	specified page. The returned pdf_annot structure is owned by the page
	and does not need to be freed.
*/
pdf_annot *pdf_create_annot(fz_context *ctx, pdf_page *page, enum pdf_annot_type type);

/*
	pdf_delete_annot: delete an annotation
*/
void pdf_delete_annot(fz_context *ctx, pdf_page *page, pdf_annot *annot);

int pdf_annot_has_ink_list(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_quad_points(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_vertices(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_line(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_interior_color(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_line_ending_styles(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_icon_name(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_open(fz_context *ctx, pdf_annot *annot);
int pdf_annot_has_author(fz_context *ctx, pdf_annot *annot);

int pdf_annot_flags(fz_context *ctx, pdf_annot *annot);
void pdf_annot_rect(fz_context *ctx, pdf_annot *annot, fz_rect *rect);
float pdf_annot_border(fz_context *ctx, pdf_annot *annot);
float pdf_annot_opacity(fz_context *ctx, pdf_annot *annot);
void pdf_annot_color(fz_context *ctx, pdf_annot *annot, int *n, float color[4]);
void pdf_annot_interior_color(fz_context *ctx, pdf_annot *annot, int *n, float color[4]);
int pdf_annot_quadding(fz_context *ctx, pdf_annot *annot);

int pdf_annot_quad_point_count(fz_context *ctx, pdf_annot *annot);
void pdf_annot_quad_point(fz_context *ctx, pdf_annot *annot, int i, float qp[8]);

int pdf_annot_ink_list_count(fz_context *ctx, pdf_annot *annot);
int pdf_annot_ink_list_stroke_count(fz_context *ctx, pdf_annot *annot, int i);
fz_point pdf_annot_ink_list_stroke_vertex(fz_context *ctx, pdf_annot *annot, int i, int k);

void pdf_set_annot_flags(fz_context *ctx, pdf_annot *annot, int flags);
void pdf_set_annot_rect(fz_context *ctx, pdf_annot *annot, const fz_rect *rect);
void pdf_set_annot_border(fz_context *ctx, pdf_annot *annot, float width);
void pdf_set_annot_opacity(fz_context *ctx, pdf_annot *annot, float opacity);
void pdf_set_annot_color(fz_context *ctx, pdf_annot *annot, int n, const float color[4]);
void pdf_set_annot_interior_color(fz_context *ctx, pdf_annot *annot, int n, const float color[4]);
void pdf_set_annot_quadding(fz_context *ctx, pdf_annot *annot, int q);

void pdf_set_annot_quad_points(fz_context *ctx, pdf_annot *annot, int n, const float *v);
void pdf_clear_annot_quad_points(fz_context *ctx, pdf_annot *annot);
void pdf_add_annot_quad_point(fz_context *ctx, pdf_annot *annot, fz_rect bbox);

void pdf_set_annot_ink_list(fz_context *ctx, pdf_annot *annot, int n, const int *count, const fz_point *v);
void pdf_clear_annot_ink_list(fz_context *ctx, pdf_annot *annot);
void pdf_add_annot_ink_list(fz_context *ctx, pdf_annot *annot, int n, fz_point stroke[]);

void pdf_set_annot_icon_name(fz_context *ctx, pdf_annot *annot, const char *name);
void pdf_set_annot_is_open(fz_context *ctx, pdf_annot *annot, int is_open);

enum pdf_line_ending pdf_annot_line_start_style(fz_context *ctx, pdf_annot *annot);
enum pdf_line_ending pdf_annot_line_end_style(fz_context *ctx, pdf_annot *annot);
void pdf_annot_line_ending_styles(fz_context *ctx, pdf_annot *annot, enum pdf_line_ending *start_style, enum pdf_line_ending *end_style);
void pdf_set_annot_line_start_style(fz_context *ctx, pdf_annot *annot, enum pdf_line_ending s);
void pdf_set_annot_line_end_style(fz_context *ctx, pdf_annot *annot, enum pdf_line_ending e);
void pdf_set_annot_line_ending_styles(fz_context *ctx, pdf_annot *annot, enum pdf_line_ending start_style, enum pdf_line_ending end_style);

const char *pdf_annot_icon_name(fz_context *ctx, pdf_annot *annot);
int pdf_annot_is_open(fz_context *ctx, pdf_annot *annot);

void pdf_annot_line(fz_context *ctx, pdf_annot *annot, fz_point *a, fz_point *b);
void pdf_set_annot_line(fz_context *ctx, pdf_annot *annot, fz_point a, fz_point b);

int pdf_annot_vertex_count(fz_context *ctx, pdf_annot *annot);
fz_point pdf_annot_vertex(fz_context *ctx, pdf_annot *annot, int i);

void pdf_set_annot_vertices(fz_context *ctx, pdf_annot *annot, int n, const fz_point *v);
void pdf_clear_annot_vertices(fz_context *ctx, pdf_annot *annot);
void pdf_add_annot_vertex(fz_context *ctx, pdf_annot *annot, fz_point p);
void pdf_set_annot_vertex(fz_context *ctx, pdf_annot *annot, int i, fz_point p);

/*
	pdf_set_text_annot_position: set the position on page for a text (sticky note) annotation.
*/
void pdf_set_text_annot_position(fz_context *ctx, pdf_annot *annot, fz_point pt);

/*
	pdf_copy_annot_contents: return a copy of the contents of an annotation.
*/
char *pdf_copy_annot_contents(fz_context *ctx, pdf_annot *annot);

/*
	pdf_set_annot_contents: set the contents of an annotation.
*/
void pdf_set_annot_contents(fz_context *ctx, pdf_annot *annot, const char *text);

/*
	pdf_copy_annot_author: return a copy of the author of an annotation.
*/
char *pdf_copy_annot_author(fz_context *ctx, pdf_annot *annot);

/*
	pdf_set_annot_author: set the author of an annotation.
*/
void pdf_set_annot_author(fz_context *ctx, pdf_annot *annot, const char *author);

/*
	pdf_annot_modification_date: Get annotation's modification date in seconds since the epoch.
*/
int64_t pdf_annot_modification_date(fz_context *ctx, pdf_annot *annot);

/*
	pdf_set_annot_modification_date: Set annotation's modification date in seconds since the epoch.
*/
void pdf_set_annot_modification_date(fz_context *ctx, pdf_annot *annot, int64_t time);

/*
	pdf_set_free_text_details: set the position, text, font and color for a free text annotation.
	Only base 14 fonts are supported and are specified by name.
*/
void pdf_set_free_text_details(fz_context *ctx, pdf_annot *annot, fz_point *pos, char *text, char *font_name, float font_size, float color[3]);

/*
	pdf_new_annot: Internal function for creating a new pdf annotation.
*/
pdf_annot *pdf_new_annot(fz_context *ctx, pdf_page *page, pdf_obj *obj);

void pdf_update_appearance(fz_context *ctx, pdf_annot *annot);
void pdf_dirty_annot(fz_context *ctx, pdf_annot *annot);

#endif
