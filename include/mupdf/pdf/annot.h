#ifndef MUPDF_PDF_ANNOT_H
#define MUPDF_PDF_ANNOT_H

enum
{
	F_Invisible = 1 << (1-1),
	F_Hidden = 1 << (2-1),
	F_Print = 1 << (3-1),
	F_NoZoom = 1 << (4-1),
	F_NoRotate = 1 << (5-1),
	F_NoView = 1 << (6-1),
	F_ReadOnly = 1 << (7-1),
	F_Locked = 1 << (8-1),
	F_ToggleNoView = 1 << (9-1),
	F_LockedContents = 1 << (10-1)
};

/*
	pdf_first_annot: Return the first annotation on a page.

	Does not throw exceptions.
*/
pdf_annot *pdf_first_annot(fz_context *ctx, pdf_page *page);

/*
	pdf_next_annot: Return the next annotation on a page.

	Does not throw exceptions.
*/
pdf_annot *pdf_next_annot(fz_context *ctx, pdf_page *page, pdf_annot *annot);

/*
	pdf_bound_annot: Return the rectangle for an annotation on a page.

	Does not throw exceptions.
*/
fz_rect *pdf_bound_annot(fz_context *ctx, pdf_page *page, pdf_annot *annot, fz_rect *rect);

/*
	pdf_annot_type: Return the type of an annotation
*/
fz_annot_type pdf_annot_type(fz_context *ctx, pdf_annot *annot);

/*
	pdf_run_annot: Interpret an annotation and render it on a device.

	page: A page loaded by pdf_load_page.

	annot: an annotation.

	dev: Device used for rendering, obtained from fz_new_*_device.

	ctm: A transformation matrix applied to the objects on the page,
	e.g. to scale or rotate the page contents as desired.
*/
void pdf_run_annot(fz_context *ctx, pdf_page *page, pdf_annot *annot, fz_device *dev, const fz_matrix *ctm, fz_cookie *cookie);

struct pdf_annot_s
{
	pdf_page *page;
	pdf_obj *obj;
	fz_rect rect;
	fz_rect pagerect;
	pdf_xobject *ap;
	int ap_iteration;
	fz_matrix matrix;
	pdf_annot *next;
	pdf_annot *next_changed;
	int annot_type;
	int widget_type;
};

fz_link_dest pdf_parse_link_dest(fz_context *ctx, pdf_document *doc, fz_link_kind kind, pdf_obj *dest);
char *pdf_parse_file_spec(fz_context *ctx, pdf_document *doc, pdf_obj *file_spec);
fz_link_dest pdf_parse_action(fz_context *ctx, pdf_document *doc, pdf_obj *action);
pdf_obj *pdf_lookup_dest(fz_context *ctx, pdf_document *doc, pdf_obj *needle);
pdf_obj *pdf_lookup_name(fz_context *ctx, pdf_document *doc, pdf_obj *which, pdf_obj *needle);
pdf_obj *pdf_load_name_tree(fz_context *ctx, pdf_document *doc, pdf_obj *which);

fz_link *pdf_load_link_annots(fz_context *ctx, pdf_document *, pdf_obj *annots, const fz_matrix *page_ctm);

void pdf_transform_annot(fz_context *ctx, pdf_annot *annot);
void pdf_load_annots(fz_context *ctx, pdf_document *, pdf_page *page, pdf_obj *annots);
void pdf_update_annot(fz_context *ctx, pdf_document *, pdf_annot *annot);
void pdf_drop_annot(fz_context *ctx, pdf_annot *link);

/*
	pdf_create_annot: create a new annotation of the specified type on the
	specified page. The returned pdf_annot structure is owned by the page
	and does not need to be freed.
*/
pdf_annot *pdf_create_annot(fz_context *ctx, pdf_document *doc, pdf_page *page, fz_annot_type type);

/*
	pdf_delete_annot: delete an annotation
*/
void pdf_delete_annot(fz_context *ctx, pdf_document *doc, pdf_page *page, pdf_annot *annot);

/*
	pdf_set_markup_annot_quadpoints: set the quadpoints for a text-markup annotation.
*/
void pdf_set_markup_annot_quadpoints(fz_context *ctx, pdf_document *doc, pdf_annot *annot, fz_point *qp, int n);

/*
	pdf_set_ink_annot_list: set the details of an ink annotation. All the points of the multiple arcs
	are carried in a single array, with the counts for each arc held in a secondary array.
*/
void pdf_set_ink_annot_list(fz_context *ctx, pdf_document *doc, pdf_annot *annot, fz_point *pts, int *counts, int ncount, float color[3], float thickness);

/*
	pdf_set_text_annot_position: set the position on page for a text (sticky note) annotation.
*/
void pdf_set_text_annot_position(fz_context *ctx, pdf_document *doc, pdf_annot *annot, fz_point pt);

/*
	pdf_set_annot_contents: set the contents of an annotation.
*/
void pdf_set_annot_contents(fz_context *ctx, pdf_document *doc, pdf_annot *annot, char *text);

/*
	pdf_annot_contents: return the contents of an annotation.
*/
char *pdf_annot_contents(fz_context *ctx, pdf_document *doc, pdf_annot *annot);

/*
	pdf_set_free_text_details: set the position, text, font and color for a free text annotation.
	Only base 14 fonts are supported and are specified by name.
*/
void pdf_set_free_text_details(fz_context *ctx, pdf_document *doc, pdf_annot *annot, fz_point *pos, char *text, char *font_name, float font_size, float color[3]);

fz_annot_type pdf_annot_obj_type(fz_context *ctx, pdf_obj *obj);

/*
	pdf_poll_changed_annot: enumerate the changed annotations recoreded
	by a call to pdf_update_page.
*/
pdf_annot *pdf_poll_changed_annot(fz_context *ctx, pdf_document *idoc, pdf_page *page);

#endif
