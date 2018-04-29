#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <string.h>
#include <time.h>

#ifdef _WIN32
#define timegm _mkgmtime
#endif

#define TEXT_ANNOT_SIZE (25.0f)

#define isdigit(c) (c >= '0' && c <= '9')

const char *
pdf_string_from_annot_type(fz_context *ctx, enum pdf_annot_type type)
{
	switch (type)
	{
	case PDF_ANNOT_TEXT: return "Text";
	case PDF_ANNOT_LINK: return "Link";
	case PDF_ANNOT_FREE_TEXT: return "FreeText";
	case PDF_ANNOT_LINE: return "Line";
	case PDF_ANNOT_SQUARE: return "Square";
	case PDF_ANNOT_CIRCLE: return "Circle";
	case PDF_ANNOT_POLYGON: return "Polygon";
	case PDF_ANNOT_POLY_LINE: return "PolyLine";
	case PDF_ANNOT_HIGHLIGHT: return "Highlight";
	case PDF_ANNOT_UNDERLINE: return "Underline";
	case PDF_ANNOT_SQUIGGLY: return "Squiggly";
	case PDF_ANNOT_STRIKE_OUT: return "StrikeOut";
	case PDF_ANNOT_STAMP: return "Stamp";
	case PDF_ANNOT_CARET: return "Caret";
	case PDF_ANNOT_INK: return "Ink";
	case PDF_ANNOT_POPUP: return "Popup";
	case PDF_ANNOT_FILE_ATTACHMENT: return "FileAttachment";
	case PDF_ANNOT_SOUND: return "Sound";
	case PDF_ANNOT_MOVIE: return "Movie";
	case PDF_ANNOT_WIDGET: return "Widget";
	case PDF_ANNOT_SCREEN: return "Screen";
	case PDF_ANNOT_PRINTER_MARK: return "PrinterMark";
	case PDF_ANNOT_TRAP_NET: return "TrapNet";
	case PDF_ANNOT_WATERMARK: return "Watermark";
	case PDF_ANNOT_3D: return "3D";
	default: return "UNKNOWN";
	}
}

int
pdf_annot_type_from_string(fz_context *ctx, const char *subtype)
{
	if (!strcmp("Text", subtype)) return PDF_ANNOT_TEXT;
	if (!strcmp("Link", subtype)) return PDF_ANNOT_LINK;
	if (!strcmp("FreeText", subtype)) return PDF_ANNOT_FREE_TEXT;
	if (!strcmp("Line", subtype)) return PDF_ANNOT_LINE;
	if (!strcmp("Square", subtype)) return PDF_ANNOT_SQUARE;
	if (!strcmp("Circle", subtype)) return PDF_ANNOT_CIRCLE;
	if (!strcmp("Polygon", subtype)) return PDF_ANNOT_POLYGON;
	if (!strcmp("PolyLine", subtype)) return PDF_ANNOT_POLY_LINE;
	if (!strcmp("Highlight", subtype)) return PDF_ANNOT_HIGHLIGHT;
	if (!strcmp("Underline", subtype)) return PDF_ANNOT_UNDERLINE;
	if (!strcmp("Squiggly", subtype)) return PDF_ANNOT_SQUIGGLY;
	if (!strcmp("StrikeOut", subtype)) return PDF_ANNOT_STRIKE_OUT;
	if (!strcmp("Stamp", subtype)) return PDF_ANNOT_STAMP;
	if (!strcmp("Caret", subtype)) return PDF_ANNOT_CARET;
	if (!strcmp("Ink", subtype)) return PDF_ANNOT_INK;
	if (!strcmp("Popup", subtype)) return PDF_ANNOT_POPUP;
	if (!strcmp("FileAttachment", subtype)) return PDF_ANNOT_FILE_ATTACHMENT;
	if (!strcmp("Sound", subtype)) return PDF_ANNOT_SOUND;
	if (!strcmp("Movie", subtype)) return PDF_ANNOT_MOVIE;
	if (!strcmp("Widget", subtype)) return PDF_ANNOT_WIDGET;
	if (!strcmp("Screen", subtype)) return PDF_ANNOT_SCREEN;
	if (!strcmp("PrinterMark", subtype)) return PDF_ANNOT_PRINTER_MARK;
	if (!strcmp("TrapNet", subtype)) return PDF_ANNOT_TRAP_NET;
	if (!strcmp("Watermark", subtype)) return PDF_ANNOT_WATERMARK;
	if (!strcmp("3D", subtype)) return PDF_ANNOT_3D;
	return PDF_ANNOT_UNKNOWN;
}

static int is_allowed_subtype(fz_context *ctx, pdf_annot *annot, pdf_obj *property, pdf_obj **allowed)
{
	pdf_obj *subtype = pdf_dict_get(ctx, annot->obj, PDF_NAME_Subtype);
	while (*allowed) {
		if (pdf_name_eq(ctx, subtype, *allowed))
			return 1;
		allowed++;
	}

	return 0;
}

static void check_allowed_subtypes(fz_context *ctx, pdf_annot *annot, pdf_obj *property, pdf_obj **allowed)
{
	pdf_obj *subtype = pdf_dict_get(ctx, annot->obj, PDF_NAME_Subtype);
	if (!is_allowed_subtype(ctx, annot, property, allowed))
		fz_throw(ctx, FZ_ERROR_GENERIC, "%s annotations have no %s property", pdf_to_name(ctx, subtype), pdf_to_name(ctx, property));
}

pdf_annot *
pdf_create_annot(fz_context *ctx, pdf_page *page, enum pdf_annot_type type)
{
	pdf_annot *annot = NULL;
	pdf_document *doc = page->doc;
	pdf_obj *annot_obj = pdf_new_dict(ctx, doc, 0);
	pdf_obj *ind_obj = NULL;

	fz_var(annot);
	fz_var(ind_obj);
	fz_try(ctx)
	{
		int ind_obj_num;
		const char *type_str;
		pdf_obj *annot_arr;

		type_str = pdf_string_from_annot_type(ctx, type);
		if (type == PDF_ANNOT_UNKNOWN)
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot create unknown annotation");

		annot_arr = pdf_dict_get(ctx, page->obj, PDF_NAME_Annots);
		if (annot_arr == NULL)
		{
			annot_arr = pdf_new_array(ctx, doc, 0);
			pdf_dict_put_drop(ctx, page->obj, PDF_NAME_Annots, annot_arr);
		}

		pdf_dict_put(ctx, annot_obj, PDF_NAME_Type, PDF_NAME_Annot);
		pdf_dict_put_name(ctx, annot_obj, PDF_NAME_Subtype, type_str);

		/* Make printable as default */
		pdf_dict_put_int(ctx, annot_obj, PDF_NAME_F, PDF_ANNOT_IS_PRINT);

		/*
			Both annotation object and annotation structure are now created.
			Insert the object in the hierarchy and the structure in the
			page's array.
		*/
		ind_obj_num = pdf_create_object(ctx, doc);
		pdf_update_object(ctx, doc, ind_obj_num, annot_obj);
		ind_obj = pdf_new_indirect(ctx, doc, ind_obj_num, 0);
		pdf_array_push(ctx, annot_arr, ind_obj);

		annot = pdf_new_annot(ctx, page, ind_obj);
		annot->ap = NULL;

		/*
			Linking must be done after any call that might throw because
			pdf_drop_annots below actually frees a list. Put the new annot
			at the end of the list, so that it will be drawn last.
		*/
		*page->annot_tailp = annot;
		page->annot_tailp = &annot->next;

		doc->dirty = 1;
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, annot_obj);
		pdf_drop_obj(ctx, ind_obj);
	}
	fz_catch(ctx)
	{
		pdf_drop_annots(ctx, annot);
		fz_rethrow(ctx);
	}

	return annot;
}

void
pdf_delete_annot(fz_context *ctx, pdf_page *page, pdf_annot *annot)
{
	pdf_document *doc = annot->page->doc;
	pdf_annot **annotptr;
	pdf_obj *annot_arr;
	int i;

	if (annot == NULL)
		return;

	/* Remove annot from page's list */
	for (annotptr = &page->annots; *annotptr; annotptr = &(*annotptr)->next)
	{
		if (*annotptr == annot)
			break;
	}

	/* Check the passed annotation was of this page */
	if (*annotptr == NULL)
		return;

	*annotptr = annot->next;

	/* If the removed annotation was the last in the list adjust the end pointer */
	if (*annotptr == NULL)
		page->annot_tailp = annotptr;

	/* If the removed annotation has the focus, blur it. */
	if (doc->focus == annot)
	{
		doc->focus = NULL;
		doc->focus_obj = NULL;
	}

	/* Remove the annot from the "Annots" array. */
	annot_arr = pdf_dict_get(ctx, page->obj, PDF_NAME_Annots);
	i = pdf_array_find(ctx, annot_arr, annot->obj);
	if (i >= 0)
		pdf_array_delete(ctx, annot_arr, i);

	/* The garbage collection pass when saving will remove the annot object,
	 * removing it here may break files if multiple pages use the same annot. */

	/* And free it. */
	fz_drop_annot(ctx, &annot->super);

	doc->dirty = 1;
}

int
pdf_annot_type(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *obj = annot->obj;
	pdf_obj *subtype = pdf_dict_get(ctx, obj, PDF_NAME_Subtype);
	return pdf_annot_type_from_string(ctx, pdf_to_name(ctx, subtype));
}

int
pdf_annot_flags(fz_context *ctx, pdf_annot *annot)
{
	return pdf_to_int(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_F));
}

void
pdf_set_annot_flags(fz_context *ctx, pdf_annot *annot, int flags)
{
	pdf_dict_put_int(ctx, annot->obj, PDF_NAME_F, flags);
	pdf_dirty_annot(ctx, annot);
}

void
pdf_annot_rect(fz_context *ctx, pdf_annot *annot, fz_rect *rect)
{
	fz_matrix page_ctm;
	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	pdf_to_rect(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Rect), rect);
	fz_transform_rect(rect, &page_ctm);
}

void
pdf_set_annot_rect(fz_context *ctx, pdf_annot *annot, const fz_rect *rect)
{
	fz_rect trect = *rect;
	fz_matrix page_ctm, inv_page_ctm;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);
	fz_transform_rect(&trect, &inv_page_ctm);

	pdf_dict_put_rect(ctx, annot->obj, PDF_NAME_Rect, &trect);
	pdf_dirty_annot(ctx, annot);
}

char *
pdf_copy_annot_contents(fz_context *ctx, pdf_annot *annot)
{
	return pdf_to_utf8(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Contents));
}

void
pdf_set_annot_contents(fz_context *ctx, pdf_annot *annot, const char *text)
{
	pdf_dict_put_text_string(ctx, annot->obj, PDF_NAME_Contents, text);
	pdf_dirty_annot(ctx, annot);
}

static pdf_obj *open_subtypes[] = {
	PDF_NAME_Popup,
	PDF_NAME_Text,
	NULL,
};

int
pdf_annot_has_open(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_Open, open_subtypes);
}

int
pdf_annot_is_open(fz_context *ctx, pdf_annot *annot)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_Open, open_subtypes);
	return pdf_to_bool(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Open));
}

void
pdf_set_annot_is_open(fz_context *ctx, pdf_annot *annot, int is_open)
{
	pdf_document *doc = annot->page->doc;
	check_allowed_subtypes(ctx, annot, PDF_NAME_Open, open_subtypes);
	pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_Open,
			pdf_new_bool(ctx, doc, is_open));
	pdf_dirty_annot(ctx, annot);
}

static pdf_obj *icon_name_subtypes[] = {
	PDF_NAME_FileAttachment,
	PDF_NAME_Sound,
	PDF_NAME_Stamp,
	PDF_NAME_Text,
	NULL,
};

int
pdf_annot_has_icon_name(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_Name, icon_name_subtypes);
}

const char *
pdf_annot_icon_name(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *name;
	check_allowed_subtypes(ctx, annot, PDF_NAME_Name, icon_name_subtypes);
	name = pdf_dict_get(ctx, annot->obj, PDF_NAME_Name);
	if (!name)
	{
		pdf_obj *subtype = pdf_dict_get(ctx, annot->obj, PDF_NAME_Subtype);
		if (pdf_name_eq(ctx, subtype, PDF_NAME_Text))
			return "Note";
		if (pdf_name_eq(ctx, subtype, PDF_NAME_Stamp))
			return "Draft";
		if (pdf_name_eq(ctx, subtype, PDF_NAME_FileAttachment))
			return "PushPin";
		if (pdf_name_eq(ctx, subtype, PDF_NAME_Sound))
			return "Speaker";
	}
	return pdf_to_name(ctx, name);
}

void
pdf_set_annot_icon_name(fz_context *ctx, pdf_annot *annot, const char *name)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_Name, icon_name_subtypes);
	pdf_dict_put_name(ctx, annot->obj, PDF_NAME_Name, name);
	pdf_dirty_annot(ctx, annot);
}

enum pdf_line_ending pdf_line_ending_from_name(fz_context *ctx, pdf_obj *end)
{
	if (pdf_name_eq(ctx, end, PDF_NAME_None)) return PDF_ANNOT_LE_NONE;
	else if (pdf_name_eq(ctx, end, PDF_NAME_Square)) return PDF_ANNOT_LE_SQUARE;
	else if (pdf_name_eq(ctx, end, PDF_NAME_Circle)) return PDF_ANNOT_LE_CIRCLE;
	else if (pdf_name_eq(ctx, end, PDF_NAME_Diamond)) return PDF_ANNOT_LE_DIAMOND;
	else if (pdf_name_eq(ctx, end, PDF_NAME_OpenArrow)) return PDF_ANNOT_LE_OPEN_ARROW;
	else if (pdf_name_eq(ctx, end, PDF_NAME_ClosedArrow)) return PDF_ANNOT_LE_CLOSED_ARROW;
	else if (pdf_name_eq(ctx, end, PDF_NAME_Butt)) return PDF_ANNOT_LE_BUTT;
	else if (pdf_name_eq(ctx, end, PDF_NAME_ROpenArrow)) return PDF_ANNOT_LE_R_OPEN_ARROW;
	else if (pdf_name_eq(ctx, end, PDF_NAME_RClosedArrow)) return PDF_ANNOT_LE_R_CLOSED_ARROW;
	else if (pdf_name_eq(ctx, end, PDF_NAME_Slash)) return PDF_ANNOT_LE_SLASH;
	else return PDF_ANNOT_LE_NONE;
}

enum pdf_line_ending pdf_line_ending_from_string(fz_context *ctx, const char *end)
{
	if (!strcmp(end, "None")) return PDF_ANNOT_LE_NONE;
	else if (!strcmp(end, "Square")) return PDF_ANNOT_LE_SQUARE;
	else if (!strcmp(end, "Circle")) return PDF_ANNOT_LE_CIRCLE;
	else if (!strcmp(end, "Diamond")) return PDF_ANNOT_LE_DIAMOND;
	else if (!strcmp(end, "OpenArrow")) return PDF_ANNOT_LE_OPEN_ARROW;
	else if (!strcmp(end, "ClosedArrow")) return PDF_ANNOT_LE_CLOSED_ARROW;
	else if (!strcmp(end, "Butt")) return PDF_ANNOT_LE_BUTT;
	else if (!strcmp(end, "ROpenArrow")) return PDF_ANNOT_LE_R_OPEN_ARROW;
	else if (!strcmp(end, "RClosedArrow")) return PDF_ANNOT_LE_R_CLOSED_ARROW;
	else if (!strcmp(end, "Slash")) return PDF_ANNOT_LE_SLASH;
	else return PDF_ANNOT_LE_NONE;
}

pdf_obj *pdf_name_from_line_ending(fz_context *ctx, enum pdf_line_ending end)
{
	switch (end)
	{
	default:
	case PDF_ANNOT_LE_NONE: return PDF_NAME_None;
	case PDF_ANNOT_LE_SQUARE: return PDF_NAME_Square;
	case PDF_ANNOT_LE_CIRCLE: return PDF_NAME_Circle;
	case PDF_ANNOT_LE_DIAMOND: return PDF_NAME_Diamond;
	case PDF_ANNOT_LE_OPEN_ARROW: return PDF_NAME_OpenArrow;
	case PDF_ANNOT_LE_CLOSED_ARROW: return PDF_NAME_ClosedArrow;
	case PDF_ANNOT_LE_BUTT: return PDF_NAME_Butt;
	case PDF_ANNOT_LE_R_OPEN_ARROW: return PDF_NAME_ROpenArrow;
	case PDF_ANNOT_LE_R_CLOSED_ARROW: return PDF_NAME_RClosedArrow;
	case PDF_ANNOT_LE_SLASH: return PDF_NAME_Slash;
	}
}

const char *pdf_string_from_line_ending(fz_context *ctx, enum pdf_line_ending end)
{
	switch (end)
	{
	default:
	case PDF_ANNOT_LE_NONE: return "None";
	case PDF_ANNOT_LE_SQUARE: return "Square";
	case PDF_ANNOT_LE_CIRCLE: return "Circle";
	case PDF_ANNOT_LE_DIAMOND: return "Diamond";
	case PDF_ANNOT_LE_OPEN_ARROW: return "OpenArrow";
	case PDF_ANNOT_LE_CLOSED_ARROW: return "ClosedArrow";
	case PDF_ANNOT_LE_BUTT: return "Butt";
	case PDF_ANNOT_LE_R_OPEN_ARROW: return "ROpenArrow";
	case PDF_ANNOT_LE_R_CLOSED_ARROW: return "RClosedArrow";
	case PDF_ANNOT_LE_SLASH: return "Slash";
	}
}

static pdf_obj *line_ending_subtypes[] = {
	PDF_NAME_FreeText,
	PDF_NAME_Line,
	PDF_NAME_PolyLine,
	PDF_NAME_Polygon,
	NULL,
};

int
pdf_annot_has_line_ending_styles(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_LE, line_ending_subtypes);
}

void
pdf_annot_line_ending_styles(fz_context *ctx, pdf_annot *annot,
		enum pdf_line_ending *start_style,
		enum pdf_line_ending *end_style)
{
	pdf_obj *style;
	check_allowed_subtypes(ctx, annot, PDF_NAME_LE, line_ending_subtypes);
	style = pdf_dict_get(ctx, annot->obj, PDF_NAME_LE);
	*start_style = pdf_line_ending_from_name(ctx, pdf_array_get(ctx, style, 0));
	*end_style = pdf_line_ending_from_name(ctx, pdf_array_get(ctx, style, 1));
}

enum pdf_line_ending
pdf_annot_line_start_style(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *le = pdf_dict_get(ctx, annot->obj, PDF_NAME_LE);
	return pdf_line_ending_from_name(ctx, pdf_array_get(ctx, le, 0));
}

enum pdf_line_ending
pdf_annot_line_end_style(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *le = pdf_dict_get(ctx, annot->obj, PDF_NAME_LE);
	return pdf_line_ending_from_name(ctx, pdf_array_get(ctx, le, 1));
}

void
pdf_set_annot_line_ending_styles(fz_context *ctx, pdf_annot *annot,
		enum pdf_line_ending start_style,
		enum pdf_line_ending end_style)
{
	pdf_document *doc = annot->page->doc;
	pdf_obj *style;
	check_allowed_subtypes(ctx, annot, PDF_NAME_LE, line_ending_subtypes);
	style = pdf_new_array(ctx, doc, 2);
	pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_LE, style);
	pdf_array_put_drop(ctx, style, 0, pdf_name_from_line_ending(ctx, start_style));
	pdf_array_put_drop(ctx, style, 1, pdf_name_from_line_ending(ctx, end_style));
	pdf_dirty_annot(ctx, annot);
}

void
pdf_set_annot_line_start_style(fz_context *ctx, pdf_annot *annot, enum pdf_line_ending s)
{
	enum pdf_line_ending e = pdf_annot_line_end_style(ctx, annot);
	pdf_set_annot_line_ending_styles(ctx, annot, s, e);
}

void
pdf_set_annot_line_end_style(fz_context *ctx, pdf_annot *annot, enum pdf_line_ending e)
{
	enum pdf_line_ending s = pdf_annot_line_start_style(ctx, annot);
	pdf_set_annot_line_ending_styles(ctx, annot, s, e);
}

float
pdf_annot_border(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *bs, *bs_w;
	bs = pdf_dict_get(ctx, annot->obj, PDF_NAME_BS);
	bs_w = pdf_dict_get(ctx, bs, PDF_NAME_W);
	if (pdf_is_number(ctx, bs_w))
		return pdf_to_real(ctx, bs_w);
	return 1;
}

void
pdf_set_annot_border(fz_context *ctx, pdf_annot *annot, float w)
{
	pdf_obj *bs = pdf_dict_get(ctx, annot->obj, PDF_NAME_BS);
	if (!pdf_is_dict(ctx, bs))
		bs = pdf_dict_put_dict(ctx, annot->obj, PDF_NAME_BS, 1);
	pdf_dict_put_real(ctx, bs, PDF_NAME_W, w);
	pdf_dirty_annot(ctx, annot);
}

int
pdf_annot_quadding(fz_context *ctx, pdf_annot *annot)
{
	int q = pdf_to_int(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Q));
	return (q < 0 || q > 2) ? 0 : q;
}

void
pdf_set_annot_quadding(fz_context *ctx, pdf_annot *annot, int q)
{
	q = (q < 0 || q > 2) ? 0 : q;
	pdf_dict_put_int(ctx, annot->obj, PDF_NAME_Q, q);
	pdf_dirty_annot(ctx, annot);
}

float pdf_annot_opacity(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *ca = pdf_dict_get(ctx, annot->obj, PDF_NAME_CA);
	if (pdf_is_number(ctx, ca))
		return pdf_to_real(ctx, ca);
	return 1;
}

void pdf_set_annot_opacity(fz_context *ctx, pdf_annot *annot, float opacity)
{
	if (opacity != 1)
		pdf_dict_put_real(ctx, annot->obj, PDF_NAME_CA, opacity);
	else
		pdf_dict_del(ctx, annot->obj, PDF_NAME_CA);
	pdf_dirty_annot(ctx, annot);
}

static void pdf_annot_color_imp(fz_context *ctx, pdf_annot *annot, pdf_obj *key, int *n, float color[4], pdf_obj **allowed)
{
	pdf_obj *arr;
	int len;

	if (allowed)
		check_allowed_subtypes(ctx, annot, key, allowed);

	arr = pdf_dict_get(ctx, annot->obj, key);
	len = pdf_array_len(ctx, arr);

	switch (len)
	{
	case 0:
		if (n)
			*n = 0;
		break;
	case 1:
	case 2:
		if (n)
			*n = 1;
		if (color)
			color[0] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 0));
		break;
	case 3:
		if (n)
			*n = 3;
		if (color)
		{
			color[0] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 0));
			color[1] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 1));
			color[2] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 2));
		}
		break;
	case 4:
	default:
		if (n)
			*n = 4;
		if (color)
		{
			color[0] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 0));
			color[1] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 1));
			color[2] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 2));
			color[3] = pdf_to_real(ctx, pdf_array_get(ctx, arr, 3));
		}
		break;
	}
}

static void pdf_set_annot_color_imp(fz_context *ctx, pdf_annot *annot, pdf_obj *key, int n, const float color[4], pdf_obj **allowed)
{
	pdf_document *doc = annot->page->doc;
	pdf_obj *arr;

	if (allowed)
		check_allowed_subtypes(ctx, annot, key, allowed);
	if (n != 0 && n != 1 && n != 3 && n != 4)
		fz_throw(ctx, FZ_ERROR_GENERIC, "color must be 0, 1, 3 or 4 components");
	if (!color)
		fz_throw(ctx, FZ_ERROR_GENERIC, "no color given");

	arr = pdf_new_array(ctx, doc, n);
	fz_try(ctx)
	{
		switch (n)
		{
		case 1:
			pdf_array_push_real(ctx, arr, color[0]);
			break;
		case 3:
			pdf_array_push_real(ctx, arr, color[0]);
			pdf_array_push_real(ctx, arr, color[1]);
			pdf_array_push_real(ctx, arr, color[2]);
			break;
		case 4:
			pdf_array_push_real(ctx, arr, color[0]);
			pdf_array_push_real(ctx, arr, color[1]);
			pdf_array_push_real(ctx, arr, color[2]);
			pdf_array_push_real(ctx, arr, color[3]);
			break;
		}
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, arr);
		fz_rethrow(ctx);
	}

	pdf_dict_put_drop(ctx, annot->obj, key, arr);
	pdf_dirty_annot(ctx, annot);
}

void
pdf_annot_color(fz_context *ctx, pdf_annot *annot, int *n, float color[4])
{
	pdf_annot_color_imp(ctx, annot, PDF_NAME_C, n, color, NULL);
}

void
pdf_set_annot_color(fz_context *ctx, pdf_annot *annot, int n, const float color[4])
{
	pdf_set_annot_color_imp(ctx, annot, PDF_NAME_C, n, color, NULL);
}

static pdf_obj *interior_color_subtypes[] = {
	PDF_NAME_Circle,
	PDF_NAME_Line,
	PDF_NAME_PolyLine,
	PDF_NAME_Polygon,
	PDF_NAME_Square,
	NULL,
};

int
pdf_annot_has_interior_color(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_IC, interior_color_subtypes);
}

void
pdf_annot_interior_color(fz_context *ctx, pdf_annot *annot, int *n, float color[4])
{
	pdf_annot_color_imp(ctx, annot, PDF_NAME_IC, n, color, interior_color_subtypes);
}

void
pdf_set_annot_interior_color(fz_context *ctx, pdf_annot *annot, int n, const float color[4])
{
	pdf_set_annot_color_imp(ctx, annot, PDF_NAME_IC, n, color, interior_color_subtypes);
}

static pdf_obj *line_subtypes[] = {
	PDF_NAME_Line,
	NULL,
};

int
pdf_annot_has_line(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_L, line_subtypes);
}

void
pdf_annot_line(fz_context *ctx, pdf_annot *annot, fz_point *a, fz_point *b)
{
	fz_matrix page_ctm;
	pdf_obj *line;

	check_allowed_subtypes(ctx, annot, PDF_NAME_L, line_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	line = pdf_dict_get(ctx, annot->obj, PDF_NAME_L);
	a->x = pdf_to_real(ctx, pdf_array_get(ctx, line, 0));
	a->y = pdf_to_real(ctx, pdf_array_get(ctx, line, 1));
	b->x = pdf_to_real(ctx, pdf_array_get(ctx, line, 2));
	b->y = pdf_to_real(ctx, pdf_array_get(ctx, line, 3));
	fz_transform_point(a, &page_ctm);
	fz_transform_point(b, &page_ctm);
}

void
pdf_set_annot_line(fz_context *ctx, pdf_annot *annot, fz_point a, fz_point b)
{
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *line;

	check_allowed_subtypes(ctx, annot, PDF_NAME_L, line_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	fz_transform_point(&a, &inv_page_ctm);
	fz_transform_point(&b, &inv_page_ctm);

	line = pdf_new_array(ctx, annot->page->doc, 4);
	pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_L, line);
	pdf_array_push_real(ctx, line, a.x);
	pdf_array_push_real(ctx, line, a.y);
	pdf_array_push_real(ctx, line, b.x);
	pdf_array_push_real(ctx, line, b.y);

	pdf_dirty_annot(ctx, annot);
}

static pdf_obj *vertices_subtypes[] = {
	PDF_NAME_PolyLine,
	PDF_NAME_Polygon,
	NULL,
};

int
pdf_annot_has_vertices(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);
}

int
pdf_annot_vertex_count(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *vertices;
	check_allowed_subtypes(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);
	vertices = pdf_dict_get(ctx, annot->obj, PDF_NAME_Vertices);
	return pdf_array_len(ctx, vertices) / 2;
}

fz_point
pdf_annot_vertex(fz_context *ctx, pdf_annot *annot, int i)
{
	pdf_obj *vertices;
	fz_matrix page_ctm;
	fz_point point;

	check_allowed_subtypes(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);

	vertices = pdf_dict_get(ctx, annot->obj, PDF_NAME_Vertices);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	point.x = pdf_to_real(ctx, pdf_array_get(ctx, vertices, i * 2));
	point.y = pdf_to_real(ctx, pdf_array_get(ctx, vertices, i * 2 + 1));
	fz_transform_point(&point, &page_ctm);

	return point;
}

void
pdf_set_annot_vertices(fz_context *ctx, pdf_annot *annot, int n, const fz_point *v)
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *vertices;
	fz_point point;
	int i;

	check_allowed_subtypes(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);
	if (n <= 0 || !v)
		fz_throw(ctx, FZ_ERROR_GENERIC, "invalid number of vertices");

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	vertices = pdf_new_array(ctx, doc, n * 2);
	for (i = 0; i < n; ++i)
	{
		point = v[i];
		fz_transform_point(&point, &inv_page_ctm);
		pdf_array_push_real(ctx, vertices, point.x);
		pdf_array_push_real(ctx, vertices, point.y);
	}
	pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_Vertices, vertices);
	pdf_dirty_annot(ctx, annot);
}

void pdf_clear_annot_vertices(fz_context *ctx, pdf_annot *annot)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);
	pdf_dict_del(ctx, annot->obj, PDF_NAME_Vertices);
	pdf_dirty_annot(ctx, annot);
}

void pdf_add_annot_vertex(fz_context *ctx, pdf_annot *annot, fz_point p)
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *vertices;

	check_allowed_subtypes(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	vertices = pdf_dict_get(ctx, annot->obj, PDF_NAME_Vertices);
	if (!pdf_is_array(ctx, vertices))
	{
		vertices = pdf_new_array(ctx, doc, 32);
		pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_Vertices, vertices);
	}

	fz_transform_point(&p, &inv_page_ctm);
	pdf_array_push_real(ctx, vertices, p.x);
	pdf_array_push_real(ctx, vertices, p.y);

	pdf_dirty_annot(ctx, annot);
}

void pdf_set_annot_vertex(fz_context *ctx, pdf_annot *annot, int i, fz_point p)
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *vertices;

	check_allowed_subtypes(ctx, annot, PDF_NAME_Vertices, vertices_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	fz_transform_point(&p, &inv_page_ctm);

	vertices = pdf_dict_get(ctx, annot->obj, PDF_NAME_Vertices);
	pdf_array_put_drop(ctx, vertices, i * 2 + 0, pdf_new_real(ctx, doc, p.x));
	pdf_array_put_drop(ctx, vertices, i * 2 + 1, pdf_new_real(ctx, doc, p.y));
}

static pdf_obj *quad_point_subtypes[] = {
	PDF_NAME_Highlight,
	PDF_NAME_Link,
	PDF_NAME_Squiggly,
	PDF_NAME_StrikeOut,
	PDF_NAME_Underline,
	NULL,
};

int
pdf_annot_has_quad_points(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_QuadPoints, quad_point_subtypes);
}

int
pdf_annot_quad_point_count(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *quad_points;
	check_allowed_subtypes(ctx, annot, PDF_NAME_QuadPoints, quad_point_subtypes);
	quad_points = pdf_dict_get(ctx, annot->obj, PDF_NAME_QuadPoints);
	return pdf_array_len(ctx, quad_points) / 8;
}

void
pdf_annot_quad_point(fz_context *ctx, pdf_annot *annot, int idx, float v[8])
{
	pdf_obj *quad_points;
	fz_matrix page_ctm;
	int i;

	check_allowed_subtypes(ctx, annot, PDF_NAME_QuadPoints, quad_point_subtypes);
	quad_points = pdf_dict_get(ctx, annot->obj, PDF_NAME_QuadPoints);
	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	for (i = 0; i < 8; i += 2)
	{
		fz_point point;
		point.x = pdf_to_real(ctx, pdf_array_get(ctx, quad_points, idx * 8 + i + 0));
		point.y = pdf_to_real(ctx, pdf_array_get(ctx, quad_points, idx * 8 + i + 1));
		fz_transform_point(&point, &page_ctm);
		v[i+0] = point.x;
		v[i+1] = point.y;
	}
}

void
pdf_set_annot_quad_points(fz_context *ctx, pdf_annot *annot, int n, const float *v)
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *quad_points;
	fz_point point;
	int i, k;

	check_allowed_subtypes(ctx, annot, PDF_NAME_QuadPoints, quad_point_subtypes);
	if (n <= 0 || !v)
		fz_throw(ctx, FZ_ERROR_GENERIC, "invalid number of quadrilaterals");

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	quad_points = pdf_new_array(ctx, doc, n * 8);
	for (i = 0; i < n; ++i)
	{
		for (k = 0; k < 4; ++k)
		{
			point.x = v[i * 8 + k * 2 + 0];
			point.y = v[i * 8 + k * 2 + 1];
			fz_transform_point(&point, &inv_page_ctm);
			pdf_array_push_real(ctx, quad_points, point.x);
			pdf_array_push_real(ctx, quad_points, point.y);
		}
	}
	pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_QuadPoints, quad_points);
	pdf_dirty_annot(ctx, annot);
}

void
pdf_clear_annot_quad_points(fz_context *ctx, pdf_annot *annot)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_QuadPoints, quad_point_subtypes);
	pdf_dict_del(ctx, annot->obj, PDF_NAME_QuadPoints);
	pdf_dirty_annot(ctx, annot);
}

void
pdf_add_annot_quad_point(fz_context *ctx, pdf_annot *annot, fz_rect bbox)
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *quad_points;

	check_allowed_subtypes(ctx, annot, PDF_NAME_QuadPoints, quad_point_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	quad_points = pdf_dict_get(ctx, annot->obj, PDF_NAME_QuadPoints);
	if (!pdf_is_array(ctx, quad_points))
	{
		quad_points = pdf_new_array(ctx, doc, 8);
		pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_QuadPoints, quad_points);
	}

	/* Contrary to the specification, the points within a QuadPoint are NOT ordered
	 * in a counterclockwise fashion. Experiments with Adobe's implementation
	 * indicates a cross-wise ordering is intended: ul, ur, ll, lr.
	 */
	fz_transform_rect(&bbox, &inv_page_ctm);
	pdf_array_push_real(ctx, quad_points, bbox.x0); /* ul */
	pdf_array_push_real(ctx, quad_points, bbox.y1);
	pdf_array_push_real(ctx, quad_points, bbox.x1); /* ur */
	pdf_array_push_real(ctx, quad_points, bbox.y1);
	pdf_array_push_real(ctx, quad_points, bbox.x0); /* ll */
	pdf_array_push_real(ctx, quad_points, bbox.y0);
	pdf_array_push_real(ctx, quad_points, bbox.x1); /* lr */
	pdf_array_push_real(ctx, quad_points, bbox.y0);

	pdf_dirty_annot(ctx, annot);
}

static pdf_obj *ink_list_subtypes[] = {
	PDF_NAME_Ink,
	NULL,
};

int
pdf_annot_has_ink_list(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);
}

int
pdf_annot_ink_list_count(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *ink_list;
	check_allowed_subtypes(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);
	ink_list = pdf_dict_get(ctx, annot->obj, PDF_NAME_InkList);
	return pdf_array_len(ctx, ink_list);
}

int
pdf_annot_ink_list_stroke_count(fz_context *ctx, pdf_annot *annot, int i)
{
	pdf_obj *ink_list;
	pdf_obj *stroke;
	check_allowed_subtypes(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);
	ink_list = pdf_dict_get(ctx, annot->obj, PDF_NAME_InkList);
	stroke = pdf_array_get(ctx, ink_list, i);
	return pdf_array_len(ctx, stroke) / 2;
}

fz_point
pdf_annot_ink_list_stroke_vertex(fz_context *ctx, pdf_annot *annot, int i, int k)
{
	pdf_obj *ink_list;
	pdf_obj *stroke;
	fz_matrix page_ctm;
	fz_point point;

	check_allowed_subtypes(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);

	ink_list = pdf_dict_get(ctx, annot->obj, PDF_NAME_InkList);
	stroke = pdf_array_get(ctx, ink_list, i);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	point.x = pdf_to_real(ctx, pdf_array_get(ctx, stroke, k * 2 + 0));
	point.y = pdf_to_real(ctx, pdf_array_get(ctx, stroke, k * 2 + 1));
	fz_transform_point(&point, &page_ctm);

	return point;
}

void
pdf_set_annot_ink_list(fz_context *ctx, pdf_annot *annot, int n, const int *count, const fz_point *v)
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *ink_list, *stroke;
	fz_point point;
	int i, k;

	check_allowed_subtypes(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	// TODO: update Rect (in update appearance perhaps?)

	ink_list = pdf_new_array(ctx, doc, n);
	for (i = 0; i < n; ++i)
	{
		stroke = pdf_new_array(ctx, doc, count[i] * 2);
		for (k = 0; k < count[i]; ++k)
		{
			point = *v++;
			fz_transform_point(&point, &inv_page_ctm);
			pdf_array_push_real(ctx, stroke, point.x);
			pdf_array_push_real(ctx, stroke, point.y);
		}
		pdf_array_push_drop(ctx, ink_list, stroke);
	}
	pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_InkList, ink_list);
	pdf_dirty_annot(ctx, annot);
}

void
pdf_clear_annot_ink_list(fz_context *ctx, pdf_annot *annot)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);
	pdf_dict_del(ctx, annot->obj, PDF_NAME_InkList);
	pdf_dirty_annot(ctx, annot);
}

void
pdf_add_annot_ink_list(fz_context *ctx, pdf_annot *annot, int n, fz_point p[])
{
	pdf_document *doc = annot->page->doc;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_obj *ink_list, *stroke;
	int i;

	check_allowed_subtypes(ctx, annot, PDF_NAME_InkList, ink_list_subtypes);

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	ink_list = pdf_dict_get(ctx, annot->obj, PDF_NAME_InkList);
	if (!pdf_is_array(ctx, ink_list))
	{
		ink_list = pdf_new_array(ctx, doc, 10);
		pdf_dict_put_drop(ctx, annot->obj, PDF_NAME_InkList, ink_list);
	}

	stroke = pdf_new_array(ctx, doc, n * 2);
	fz_try(ctx)
	{
		for (i = 0; i < n; ++i)
		{
			fz_point tp = p[i];
			fz_transform_point(&tp, &inv_page_ctm);
			pdf_array_push_real(ctx, stroke, tp.x);
			pdf_array_push_real(ctx, stroke, tp.y);
		}
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, stroke);
		fz_rethrow(ctx);
	}

	pdf_array_push_drop(ctx, ink_list, stroke);

	pdf_dirty_annot(ctx, annot);
}

void
pdf_set_text_annot_position(fz_context *ctx, pdf_annot *annot, fz_point pt)
{
	fz_matrix page_ctm, inv_page_ctm;
	fz_rect rect;
	int flags;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	rect.x0 = pt.x;
	rect.x1 = pt.x + TEXT_ANNOT_SIZE;
	rect.y0 = pt.y;
	rect.y1 = pt.y + TEXT_ANNOT_SIZE;
	fz_transform_rect(&rect, &inv_page_ctm);

	pdf_dict_put_rect(ctx, annot->obj, PDF_NAME_Rect, &rect);

	flags = pdf_to_int(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_F));
	flags |= (PDF_ANNOT_IS_NO_ZOOM|PDF_ANNOT_IS_NO_ROTATE);
	pdf_dict_put_int(ctx, annot->obj, PDF_NAME_F, flags);
}

static void
pdf_format_date(fz_context *ctx, char *s, int n, time_t secs)
{
#ifdef _POSIX_SOURCE
	struct tm tmbuf, *tm = gmtime_r(&secs, &tmbuf);
#else
	struct tm *tm = gmtime(&secs);
#endif
	if (!tm)
		fz_strlcpy(s, "D:19700101000000Z", n);
	else
		strftime(s, n, "D:%Y%m%d%H%M%SZ", tm);
}

static int64_t
pdf_parse_date(fz_context *ctx, const char *s)
{
	int tz_sign, tz_hour, tz_min, tz_adj;
	struct tm tm;
	time_t utc;

	if (!s)
		return 0;

	memset(&tm, 0, sizeof tm);
	tm.tm_mday = 1;

	tz_sign = 1;
	tz_hour = 0;
	tz_min = 0;

	if (s[0] == 'D' && s[1] == ':')
		s += 2;

	if (!isdigit(s[0]) || !isdigit(s[1]) || !isdigit(s[2]) || !isdigit(s[3]))
	{
		fz_warn(ctx, "invalid date format (missing year)");
		return 0;
	}
	tm.tm_year = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0') - 1900;
	s += 4;

	if (isdigit(s[0]) && isdigit(s[1]))
	{
		tm.tm_mon = (s[0]-'0')*10 + (s[1]-'0') - 1; /* month is 0-11 in struct tm */
		s += 2;
		if (isdigit(s[0]) && isdigit(s[1]))
		{
			tm.tm_mday = (s[0]-'0')*10 + (s[1]-'0');
			s += 2;
			if (isdigit(s[0]) && isdigit(s[1]))
			{
				tm.tm_hour = (s[0]-'0')*10 + (s[1]-'0');
				s += 2;
				if (isdigit(s[0]) && isdigit(s[1]))
				{
					tm.tm_min = (s[0]-'0')*10 + (s[1]-'0');
					s += 2;
					if (isdigit(s[0]) && isdigit(s[1]))
					{
						tm.tm_sec = (s[0]-'0')*10 + (s[1]-'0');
						s += 2;
					}
				}
			}
		}
	}

	if (s[0] == 'Z')
	{
		s += 1;
	}
	else if ((s[0] == '-' || s[0] == '+') && isdigit(s[1]) && isdigit(s[2]))
	{
		tz_sign = (s[0] == '-') ? -1 : 1;
		tz_hour = (s[1]-'0')*10 + (s[2]-'0');
		s += 3;
		if (s[0] == '\'' && isdigit(s[1]) && isdigit(s[2]))
		{
			tz_min = (s[1]-'0')*10 + (s[2]-'0');
			s += 3;
			if (s[0] == '\'')
				s += 1;
		}
	}

	if (s[0] != 0)
		fz_warn(ctx, "invalid date format (garbage at end)");

	utc = timegm(&tm);
	if (utc == (time_t)-1)
	{
		fz_warn(ctx, "date overflow error");
		return 0;
	}

	tz_adj = tz_sign * (tz_hour * 3600 + tz_min * 60);
	return utc - tz_adj;
}

static pdf_obj *markup_subtypes[] = {
	PDF_NAME_Text,
	PDF_NAME_FreeText,
	PDF_NAME_Line,
	PDF_NAME_Square,
	PDF_NAME_Circle,
	PDF_NAME_Polygon,
	PDF_NAME_PolyLine,
	PDF_NAME_Highlight,
	PDF_NAME_Underline,
	PDF_NAME_Squiggly,
	PDF_NAME_StrikeOut,
	PDF_NAME_Stamp,
	PDF_NAME_Caret,
	PDF_NAME_Ink,
	PDF_NAME_FileAttachment,
	PDF_NAME_Sound,
	NULL,
};

int64_t
pdf_annot_modification_date(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *date = pdf_dict_get(ctx, annot->obj, PDF_NAME_M);
	return date ? pdf_parse_date(ctx, pdf_to_str_buf(ctx, date)) : 0;
}

void
pdf_set_annot_modification_date(fz_context *ctx, pdf_annot *annot, int64_t secs)
{
	char s[40];

	check_allowed_subtypes(ctx, annot, PDF_NAME_M, markup_subtypes);

	pdf_format_date(ctx, s, sizeof s, secs);
	pdf_dict_put_string(ctx, annot->obj, PDF_NAME_M, s, strlen(s));
	pdf_dirty_annot(ctx, annot);
}

int
pdf_annot_has_author(fz_context *ctx, pdf_annot *annot)
{
	return is_allowed_subtype(ctx, annot, PDF_NAME_T, markup_subtypes);
}

char *
pdf_copy_annot_author(fz_context *ctx, pdf_annot *annot)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_T, markup_subtypes);
	return pdf_to_utf8(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_T));
}

void
pdf_set_annot_author(fz_context *ctx, pdf_annot *annot, const char *author)
{
	check_allowed_subtypes(ctx, annot, PDF_NAME_T, markup_subtypes);
	pdf_dict_put_text_string(ctx, annot->obj, PDF_NAME_T, author);
	pdf_dirty_annot(ctx, annot);
}

static void find_free_font_name(fz_context *ctx, pdf_obj *fdict, char *buf, int buf_size)
{
	int i;

	/* Find a number X such that /FX doesn't occur as a key in fdict */
	for (i = 0; 1; i++)
	{
		fz_snprintf(buf, buf_size, "F%d", i);

		if (!pdf_dict_gets(ctx, fdict, buf))
			break;
	}
}

void
pdf_set_free_text_details(fz_context *ctx, pdf_annot *annot, fz_point *pos, char *text, char *font_name, float font_size, float color[3])
{
	pdf_document *doc = annot->page->doc;
	char nbuf[32];
	pdf_obj *dr;
	pdf_obj *form_fonts;
	pdf_obj *font = NULL;
	pdf_obj *ref;
	pdf_font_desc *font_desc = NULL;
	fz_matrix page_ctm, inv_page_ctm;
	pdf_da_info da_info;
	fz_buffer *fzbuf = NULL;
	fz_point page_pos;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	dr = pdf_dict_get(ctx, annot->page->obj, PDF_NAME_Resources);
	if (!dr)
	{
		dr = pdf_new_dict(ctx, doc, 1);
		pdf_dict_put_drop(ctx, annot->page->obj, PDF_NAME_Resources, dr);
	}

	/* Ensure the resource dictionary includes a font dict */
	form_fonts = pdf_dict_get(ctx, dr, PDF_NAME_Font);
	if (!form_fonts)
	{
		form_fonts = pdf_new_dict(ctx, doc, 1);
		pdf_dict_put_drop(ctx, dr, PDF_NAME_Font, form_fonts);
		/* form_fonts is still valid if execution continues past the above call */
	}

	fz_var(fzbuf);
	fz_var(font);
	fz_try(ctx)
	{
		unsigned char *da_str;
		size_t da_len;
		fz_rect bounds;

		find_free_font_name(ctx, form_fonts, nbuf, sizeof(nbuf));

		font = pdf_new_dict(ctx, doc, 5);
		ref = pdf_add_object(ctx, doc, font);
		pdf_dict_puts_drop(ctx, form_fonts, nbuf, ref);

		pdf_dict_put(ctx, font, PDF_NAME_Type, PDF_NAME_Font);
		pdf_dict_put(ctx, font, PDF_NAME_Subtype, PDF_NAME_Type1);
		pdf_dict_put_name(ctx, font, PDF_NAME_BaseFont, font_name);
		pdf_dict_put(ctx, font, PDF_NAME_Encoding, PDF_NAME_WinAnsiEncoding);

		memcpy(da_info.col, color, sizeof(float)*3);
		da_info.col_size = 3;
		da_info.font_name = nbuf;
		da_info.font_size = font_size;

		fzbuf = fz_new_buffer(ctx, 0);
		pdf_fzbuf_print_da(ctx, fzbuf, &da_info);

		da_len = fz_buffer_storage(ctx, fzbuf, &da_str);
		pdf_dict_put_string(ctx, annot->obj, PDF_NAME_DA, (char *)da_str, da_len);

		/* FIXME: should convert to WinAnsiEncoding */
		pdf_dict_put_text_string(ctx, annot->obj, PDF_NAME_Contents, text);

		font_desc = pdf_load_font(ctx, doc, NULL, font, 0);
		pdf_measure_text(ctx, font_desc, (unsigned char *)text, strlen(text), &bounds);

		page_pos = *pos;
		fz_transform_point(&page_pos, &inv_page_ctm);

		bounds.x0 *= font_size;
		bounds.x1 *= font_size;
		bounds.y0 *= font_size;
		bounds.y1 *= font_size;

		bounds.x0 += page_pos.x;
		bounds.x1 += page_pos.x;
		bounds.y0 += page_pos.y;
		bounds.y1 += page_pos.y;

		pdf_dict_put_rect(ctx, annot->obj, PDF_NAME_Rect, &bounds);
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, font);
		fz_drop_buffer(ctx, fzbuf);
		pdf_drop_font(ctx, font_desc);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}
