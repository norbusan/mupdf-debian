/*
 * Information tool.
 * Print information about the input pdf.
 */

#include "mupdf/pdf.h"

enum
{
	DIMENSIONS = 0x01,
	FONTS = 0x02,
	IMAGES = 0x04,
	SHADINGS = 0x08,
	PATTERNS = 0x10,
	XOBJS = 0x20,
	ALL = DIMENSIONS | FONTS | IMAGES | SHADINGS | PATTERNS | XOBJS
};

struct info
{
	int page;
	pdf_obj *pageref;
	pdf_obj *pageobj;
	union {
		struct {
			pdf_obj *obj;
		} info;
		struct {
			pdf_obj *obj;
		} crypt;
		struct {
			pdf_obj *obj;
			fz_rect *bbox;
		} dim;
		struct {
			pdf_obj *obj;
			pdf_obj *subtype;
			pdf_obj *name;
		} font;
		struct {
			pdf_obj *obj;
			pdf_obj *width;
			pdf_obj *height;
			pdf_obj *bpc;
			pdf_obj *filter;
			pdf_obj *cs;
			pdf_obj *altcs;
		} image;
		struct {
			pdf_obj *obj;
			pdf_obj *type;
		} shading;
		struct {
			pdf_obj *obj;
			pdf_obj *type;
			pdf_obj *paint;
			pdf_obj *tiling;
			pdf_obj *shading;
		} pattern;
		struct {
			pdf_obj *obj;
			pdf_obj *groupsubtype;
			pdf_obj *reference;
		} form;
	} u;
};

typedef struct globals_s
{
	pdf_document *doc;
	fz_context *ctx;
	fz_output *out;
	int pagecount;
	struct info *dim;
	int dims;
	struct info *font;
	int fonts;
	struct info *image;
	int images;
	struct info *shading;
	int shadings;
	struct info *pattern;
	int patterns;
	struct info *form;
	int forms;
	struct info *psobj;
	int psobjs;
} globals;

static void clearinfo(fz_context *ctx, globals *glo)
{
	int i;

	if (glo->dim)
	{
		for (i = 0; i < glo->dims; i++)
			fz_free(ctx, glo->dim[i].u.dim.bbox);
		fz_free(ctx, glo->dim);
		glo->dim = NULL;
		glo->dims = 0;
	}

	if (glo->font)
	{
		fz_free(ctx, glo->font);
		glo->font = NULL;
		glo->fonts = 0;
	}

	if (glo->image)
	{
		fz_free(ctx, glo->image);
		glo->image = NULL;
		glo->images = 0;
	}

	if (glo->shading)
	{
		fz_free(ctx, glo->shading);
		glo->shading = NULL;
		glo->shadings = 0;
	}

	if (glo->pattern)
	{
		fz_free(ctx, glo->pattern);
		glo->pattern = NULL;
		glo->patterns = 0;
	}

	if (glo->form)
	{
		fz_free(ctx, glo->form);
		glo->form = NULL;
		glo->forms = 0;
	}

	if (glo->psobj)
	{
		fz_free(ctx, glo->psobj);
		glo->psobj = NULL;
		glo->psobjs = 0;
	}
}

static void closexref(fz_context *ctx, globals *glo)
{
	if (glo->doc)
	{
		pdf_close_document(ctx, glo->doc);
		glo->doc = NULL;
	}

	clearinfo(ctx, glo);
}

static void
infousage(void)
{
	fprintf(stderr,
		"usage: mutool info [options] file.pdf [pages]\n"
		"\t-p -\tpassword for decryption\n"
		"\t-F\tlist fonts\n"
		"\t-I\tlist images\n"
		"\t-M\tlist dimensions\n"
		"\t-P\tlist patterns\n"
		"\t-S\tlist shadings\n"
		"\t-X\tlist form and postscript xobjects\n"
		"\tpages\tcomma separated list of page numbers and ranges\n"
		);
	exit(1);
}

static void
showglobalinfo(fz_context *ctx, globals *glo)
{
	pdf_obj *obj;
	fz_output *out = glo->out;
	pdf_document *doc = glo->doc;

	fz_printf(ctx, out, "\nPDF-%d.%d\n", doc->version / 10, doc->version % 10);

	obj = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME_Info);
	if (obj)
	{
		fz_printf(ctx, out, "Info object (%d %d R):\n", pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj));
		pdf_output_obj(ctx, out, pdf_resolve_indirect(ctx, obj), 1);
	}

	obj = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME_Encrypt);
	if (obj)
	{
		fz_printf(ctx, out, "\nEncryption object (%d %d R):\n", pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj));
		pdf_output_obj(ctx, out, pdf_resolve_indirect(ctx, obj), 1);
	}

	fz_printf(ctx, out, "\nPages: %d\n\n", glo->pagecount);
}

static void
gatherdimensions(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj)
{
	fz_rect bbox;
	pdf_obj *obj;
	int j;

	obj = pdf_dict_get(ctx, pageobj, PDF_NAME_MediaBox);
	if (!pdf_is_array(ctx, obj))
		return;

	pdf_to_rect(ctx, obj, &bbox);

	obj = pdf_dict_get(ctx, pageobj, PDF_NAME_UserUnit);
	if (pdf_is_real(ctx, obj))
	{
		float unit = pdf_to_real(ctx, obj);
		bbox.x0 *= unit;
		bbox.y0 *= unit;
		bbox.x1 *= unit;
		bbox.y1 *= unit;
	}

	for (j = 0; j < glo->dims; j++)
		if (!memcmp(glo->dim[j].u.dim.bbox, &bbox, sizeof (fz_rect)))
			break;

	if (j < glo->dims)
		return;

	glo->dim = fz_resize_array(ctx, glo->dim, glo->dims+1, sizeof(struct info));
	glo->dims++;

	glo->dim[glo->dims - 1].page = page;
	glo->dim[glo->dims - 1].pageref = pageref;
	glo->dim[glo->dims - 1].pageobj = pageobj;
	glo->dim[glo->dims - 1].u.dim.bbox = fz_malloc(ctx, sizeof(fz_rect));
	memcpy(glo->dim[glo->dims - 1].u.dim.bbox, &bbox, sizeof (fz_rect));

	return;
}

static void
gatherfonts(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj, pdf_obj *dict)
{
	int i, n;

	n = pdf_dict_len(ctx, dict);
	for (i = 0; i < n; i++)
	{
		pdf_obj *fontdict = NULL;
		pdf_obj *subtype = NULL;
		pdf_obj *basefont = NULL;
		pdf_obj *name = NULL;
		int k;

		fontdict = pdf_dict_get_val(ctx, dict, i);
		if (!pdf_is_dict(ctx, fontdict))
		{
			fz_warn(ctx, "not a font dict (%d %d R)", pdf_to_num(ctx, fontdict), pdf_to_gen(ctx, fontdict));
			continue;
		}

		subtype = pdf_dict_get(ctx, fontdict, PDF_NAME_Subtype);
		basefont = pdf_dict_get(ctx, fontdict, PDF_NAME_BaseFont);
		if (!basefont || pdf_is_null(ctx, basefont))
			name = pdf_dict_get(ctx, fontdict, PDF_NAME_Name);

		for (k = 0; k < glo->fonts; k++)
			if (!pdf_objcmp(ctx, glo->font[k].u.font.obj, fontdict))
				break;

		if (k < glo->fonts)
			continue;

		glo->font = fz_resize_array(ctx, glo->font, glo->fonts+1, sizeof(struct info));
		glo->fonts++;

		glo->font[glo->fonts - 1].page = page;
		glo->font[glo->fonts - 1].pageref = pageref;
		glo->font[glo->fonts - 1].pageobj = pageobj;
		glo->font[glo->fonts - 1].u.font.obj = fontdict;
		glo->font[glo->fonts - 1].u.font.subtype = subtype;
		glo->font[glo->fonts - 1].u.font.name = basefont ? basefont : name;
	}
}

static void
gatherimages(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj, pdf_obj *dict)
{
	int i, n;

	n = pdf_dict_len(ctx, dict);
	for (i = 0; i < n; i++)
	{
		pdf_obj *imagedict;
		pdf_obj *type;
		pdf_obj *width;
		pdf_obj *height;
		pdf_obj *bpc = NULL;
		pdf_obj *filter = NULL;
		pdf_obj *cs = NULL;
		pdf_obj *altcs;
		int k;

		imagedict = pdf_dict_get_val(ctx, dict, i);
		if (!pdf_is_dict(ctx, imagedict))
		{
			fz_warn(ctx, "not an image dict (%d %d R)", pdf_to_num(ctx, imagedict), pdf_to_gen(ctx, imagedict));
			continue;
		}

		type = pdf_dict_get(ctx, imagedict, PDF_NAME_Subtype);
		if (!pdf_name_eq(ctx, type, PDF_NAME_Image))
			continue;

		filter = pdf_dict_get(ctx, imagedict, PDF_NAME_Filter);

		altcs = NULL;
		cs = pdf_dict_get(ctx, imagedict, PDF_NAME_ColorSpace);
		if (pdf_is_array(ctx, cs))
		{
			pdf_obj *cses = cs;

			cs = pdf_array_get(ctx, cses, 0);
			if (pdf_name_eq(ctx, cs, PDF_NAME_DeviceN) || pdf_name_eq(ctx, cs, PDF_NAME_Separation))
			{
				altcs = pdf_array_get(ctx, cses, 2);
				if (pdf_is_array(ctx, altcs))
					altcs = pdf_array_get(ctx, altcs, 0);
			}
		}

		width = pdf_dict_get(ctx, imagedict, PDF_NAME_Width);
		height = pdf_dict_get(ctx, imagedict, PDF_NAME_Height);
		bpc = pdf_dict_get(ctx, imagedict, PDF_NAME_BitsPerComponent);

		for (k = 0; k < glo->images; k++)
			if (!pdf_objcmp(ctx, glo->image[k].u.image.obj, imagedict))
				break;

		if (k < glo->images)
			continue;

		glo->image = fz_resize_array(ctx, glo->image, glo->images+1, sizeof(struct info));
		glo->images++;

		glo->image[glo->images - 1].page = page;
		glo->image[glo->images - 1].pageref = pageref;
		glo->image[glo->images - 1].pageobj = pageobj;
		glo->image[glo->images - 1].u.image.obj = imagedict;
		glo->image[glo->images - 1].u.image.width = width;
		glo->image[glo->images - 1].u.image.height = height;
		glo->image[glo->images - 1].u.image.bpc = bpc;
		glo->image[glo->images - 1].u.image.filter = filter;
		glo->image[glo->images - 1].u.image.cs = cs;
		glo->image[glo->images - 1].u.image.altcs = altcs;
	}
}

static void
gatherforms(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj, pdf_obj *dict)
{
	int i, n;

	n = pdf_dict_len(ctx, dict);
	for (i = 0; i < n; i++)
	{
		pdf_obj *xobjdict;
		pdf_obj *type;
		pdf_obj *subtype;
		pdf_obj *group;
		pdf_obj *groupsubtype;
		pdf_obj *reference;
		int k;

		xobjdict = pdf_dict_get_val(ctx, dict, i);
		if (!pdf_is_dict(ctx, xobjdict))
		{
			fz_warn(ctx, "not a xobject dict (%d %d R)", pdf_to_num(ctx, xobjdict), pdf_to_gen(ctx, xobjdict));
			continue;
		}

		type = pdf_dict_get(ctx, xobjdict, PDF_NAME_Subtype);
		if (!pdf_name_eq(ctx, type, PDF_NAME_Form))
			continue;

		subtype = pdf_dict_get(ctx, xobjdict, PDF_NAME_Subtype2);
		if (!pdf_name_eq(ctx, subtype, PDF_NAME_PS))
			continue;

		group = pdf_dict_get(ctx, xobjdict, PDF_NAME_Group);
		groupsubtype = pdf_dict_get(ctx, group, PDF_NAME_S);
		reference = pdf_dict_get(ctx, xobjdict, PDF_NAME_Ref);

		for (k = 0; k < glo->forms; k++)
			if (!pdf_objcmp(ctx, glo->form[k].u.form.obj, xobjdict))
				break;

		if (k < glo->forms)
			continue;

		glo->form = fz_resize_array(ctx, glo->form, glo->forms+1, sizeof(struct info));
		glo->forms++;

		glo->form[glo->forms - 1].page = page;
		glo->form[glo->forms - 1].pageref = pageref;
		glo->form[glo->forms - 1].pageobj = pageobj;
		glo->form[glo->forms - 1].u.form.obj = xobjdict;
		glo->form[glo->forms - 1].u.form.groupsubtype = groupsubtype;
		glo->form[glo->forms - 1].u.form.reference = reference;
	}
}

static void
gatherpsobjs(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj, pdf_obj *dict)
{
	int i, n;

	n = pdf_dict_len(ctx, dict);
	for (i = 0; i < n; i++)
	{
		pdf_obj *xobjdict;
		pdf_obj *type;
		pdf_obj *subtype;
		int k;

		xobjdict = pdf_dict_get_val(ctx, dict, i);
		if (!pdf_is_dict(ctx, xobjdict))
		{
			fz_warn(ctx, "not a xobject dict (%d %d R)", pdf_to_num(ctx, xobjdict), pdf_to_gen(ctx, xobjdict));
			continue;
		}

		type = pdf_dict_get(ctx, xobjdict, PDF_NAME_Subtype);
		subtype = pdf_dict_get(ctx, xobjdict, PDF_NAME_Subtype2);
		if (!pdf_name_eq(ctx, type, PDF_NAME_PS) &&
			(!pdf_name_eq(ctx, type, PDF_NAME_Form) || !pdf_name_eq(ctx, subtype, PDF_NAME_PS)))
			continue;

		for (k = 0; k < glo->psobjs; k++)
			if (!pdf_objcmp(ctx, glo->psobj[k].u.form.obj, xobjdict))
				break;

		if (k < glo->psobjs)
			continue;

		glo->psobj = fz_resize_array(ctx, glo->psobj, glo->psobjs+1, sizeof(struct info));
		glo->psobjs++;

		glo->psobj[glo->psobjs - 1].page = page;
		glo->psobj[glo->psobjs - 1].pageref = pageref;
		glo->psobj[glo->psobjs - 1].pageobj = pageobj;
		glo->psobj[glo->psobjs - 1].u.form.obj = xobjdict;
	}
}

static void
gathershadings(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj, pdf_obj *dict)
{
	int i, n;

	n = pdf_dict_len(ctx, dict);
	for (i = 0; i < n; i++)
	{
		pdf_obj *shade;
		pdf_obj *type;
		int k;

		shade = pdf_dict_get_val(ctx, dict, i);
		if (!pdf_is_dict(ctx, shade))
		{
			fz_warn(ctx, "not a shading dict (%d %d R)", pdf_to_num(ctx, shade), pdf_to_gen(ctx, shade));
			continue;
		}

		type = pdf_dict_get(ctx, shade, PDF_NAME_ShadingType);
		if (!pdf_is_int(ctx, type) || pdf_to_int(ctx, type) < 1 || pdf_to_int(ctx, type) > 7)
		{
			fz_warn(ctx, "not a shading type (%d %d R)", pdf_to_num(ctx, shade), pdf_to_gen(ctx, shade));
			type = NULL;
		}

		for (k = 0; k < glo->shadings; k++)
			if (!pdf_objcmp(ctx, glo->shading[k].u.shading.obj, shade))
				break;

		if (k < glo->shadings)
			continue;

		glo->shading = fz_resize_array(ctx, glo->shading, glo->shadings+1, sizeof(struct info));
		glo->shadings++;

		glo->shading[glo->shadings - 1].page = page;
		glo->shading[glo->shadings - 1].pageref = pageref;
		glo->shading[glo->shadings - 1].pageobj = pageobj;
		glo->shading[glo->shadings - 1].u.shading.obj = shade;
		glo->shading[glo->shadings - 1].u.shading.type = type;
	}
}

static void
gatherpatterns(fz_context *ctx, globals *glo, int page, pdf_obj *pageref, pdf_obj *pageobj, pdf_obj *dict)
{
	int i, n;

	n = pdf_dict_len(ctx, dict);
	for (i = 0; i < n; i++)
	{
		pdf_obj *patterndict;
		pdf_obj *type;
		pdf_obj *paint = NULL;
		pdf_obj *tiling = NULL;
		pdf_obj *shading = NULL;
		int k;

		patterndict = pdf_dict_get_val(ctx, dict, i);
		if (!pdf_is_dict(ctx, patterndict))
		{
			fz_warn(ctx, "not a pattern dict (%d %d R)", pdf_to_num(ctx, patterndict), pdf_to_gen(ctx, patterndict));
			continue;
		}

		type = pdf_dict_get(ctx, patterndict, PDF_NAME_PatternType);
		if (!pdf_is_int(ctx, type) || pdf_to_int(ctx, type) < 1 || pdf_to_int(ctx, type) > 2)
		{
			fz_warn(ctx, "not a pattern type (%d %d R)", pdf_to_num(ctx, patterndict), pdf_to_gen(ctx, patterndict));
			type = NULL;
		}

		if (pdf_to_int(ctx, type) == 1)
		{
			paint = pdf_dict_get(ctx, patterndict, PDF_NAME_PaintType);
			if (!pdf_is_int(ctx, paint) || pdf_to_int(ctx, paint) < 1 || pdf_to_int(ctx, paint) > 2)
			{
				fz_warn(ctx, "not a pattern paint type (%d %d R)", pdf_to_num(ctx, patterndict), pdf_to_gen(ctx, patterndict));
				paint = NULL;
			}

			tiling = pdf_dict_get(ctx, patterndict, PDF_NAME_TilingType);
			if (!pdf_is_int(ctx, tiling) || pdf_to_int(ctx, tiling) < 1 || pdf_to_int(ctx, tiling) > 3)
			{
				fz_warn(ctx, "not a pattern tiling type (%d %d R)", pdf_to_num(ctx, patterndict), pdf_to_gen(ctx, patterndict));
				tiling = NULL;
			}
		}
		else
		{
			shading = pdf_dict_get(ctx, patterndict, PDF_NAME_Shading);
		}

		for (k = 0; k < glo->patterns; k++)
			if (!pdf_objcmp(ctx, glo->pattern[k].u.pattern.obj, patterndict))
				break;

		if (k < glo->patterns)
			continue;

		glo->pattern = fz_resize_array(ctx, glo->pattern, glo->patterns+1, sizeof(struct info));
		glo->patterns++;

		glo->pattern[glo->patterns - 1].page = page;
		glo->pattern[glo->patterns - 1].pageref = pageref;
		glo->pattern[glo->patterns - 1].pageobj = pageobj;
		glo->pattern[glo->patterns - 1].u.pattern.obj = patterndict;
		glo->pattern[glo->patterns - 1].u.pattern.type = type;
		glo->pattern[glo->patterns - 1].u.pattern.paint = paint;
		glo->pattern[glo->patterns - 1].u.pattern.tiling = tiling;
		glo->pattern[glo->patterns - 1].u.pattern.shading = shading;
	}
}

static void
gatherresourceinfo(fz_context *ctx, globals *glo, int page, pdf_obj *rsrc, int show)
{
	pdf_obj *pageobj;
	pdf_obj *pageref;
	pdf_obj *font;
	pdf_obj *xobj;
	pdf_obj *shade;
	pdf_obj *pattern;
	pdf_obj *subrsrc;
	int i;

	pageref = pdf_lookup_page_obj(ctx, glo->doc, page-1);
	pageobj = pdf_resolve_indirect(ctx, pageref);

	if (!pageobj)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot retrieve info from page %d", page);

	font = pdf_dict_get(ctx, rsrc, PDF_NAME_Font);
	if (show & FONTS && font)
	{
		int n;

		gatherfonts(ctx, glo, page, pageref, pageobj, font);
		n = pdf_dict_len(ctx, font);
		for (i = 0; i < n; i++)
		{
			pdf_obj *obj = pdf_dict_get_val(ctx, font, i);

			subrsrc = pdf_dict_get(ctx, obj, PDF_NAME_Resources);
			if (subrsrc && pdf_objcmp(ctx, rsrc, subrsrc))
				gatherresourceinfo(ctx, glo, page, subrsrc, show);
		}
	}

	xobj = pdf_dict_get(ctx, rsrc, PDF_NAME_XObject);
	if (show & XOBJS && xobj)
	{
		int n;

		gatherimages(ctx, glo, page, pageref, pageobj, xobj);
		gatherforms(ctx, glo, page, pageref, pageobj, xobj);
		gatherpsobjs(ctx, glo, page, pageref, pageobj, xobj);
		n = pdf_dict_len(ctx, xobj);
		for (i = 0; i < n; i++)
		{
			pdf_obj *obj = pdf_dict_get_val(ctx, xobj, i);
			subrsrc = pdf_dict_get(ctx, obj, PDF_NAME_Resources);
			if (subrsrc && pdf_objcmp(ctx, rsrc, subrsrc))
				gatherresourceinfo(ctx, glo, page, subrsrc, show);
		}
	}

	shade = pdf_dict_get(ctx, rsrc, PDF_NAME_Shading);
	if (show & SHADINGS && shade)
		gathershadings(ctx, glo, page, pageref, pageobj, shade);

	pattern = pdf_dict_get(ctx, rsrc, PDF_NAME_Pattern);
	if (show & PATTERNS && pattern)
	{
		int n;
		gatherpatterns(ctx, glo, page, pageref, pageobj, pattern);
		n = pdf_dict_len(ctx, pattern);
		for (i = 0; i < n; i++)
		{
			pdf_obj *obj = pdf_dict_get_val(ctx, pattern, i);
			subrsrc = pdf_dict_get(ctx, obj, PDF_NAME_Resources);
			if (subrsrc && pdf_objcmp(ctx, rsrc, subrsrc))
				gatherresourceinfo(ctx, glo, page, subrsrc, show);
		}
	}
}

static void
gatherpageinfo(fz_context *ctx, globals *glo, int page, int show)
{
	pdf_obj *pageobj;
	pdf_obj *pageref;
	pdf_obj *rsrc;

	pageref = pdf_lookup_page_obj(ctx, glo->doc, page-1);
	pageobj = pdf_resolve_indirect(ctx, pageref);

	if (!pageobj)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot retrieve info from page %d", page);

	gatherdimensions(ctx, glo, page, pageref, pageobj);

	rsrc = pdf_dict_get(ctx, pageobj, PDF_NAME_Resources);
	gatherresourceinfo(ctx, glo, page, rsrc, show);
}

static void
printinfo(fz_context *ctx, globals *glo, char *filename, int show, int page)
{
	int i;
	int j;
	fz_output *out = glo->out;

#define PAGE_FMT "\t%d\t(%d %d R):\t"

	if (show & DIMENSIONS && glo->dims > 0)
	{
		fz_printf(ctx, out, "Mediaboxes (%d):\n", glo->dims);
		for (i = 0; i < glo->dims; i++)
		{
			fz_printf(ctx, out, PAGE_FMT "[ %g %g %g %g ]\n",
				glo->dim[i].page,
				pdf_to_num(ctx, glo->dim[i].pageref),
				pdf_to_gen(ctx, glo->dim[i].pageref),
				glo->dim[i].u.dim.bbox->x0,
				glo->dim[i].u.dim.bbox->y0,
				glo->dim[i].u.dim.bbox->x1,
				glo->dim[i].u.dim.bbox->y1);
		}
		fz_printf(ctx, out, "\n");
	}

	if (show & FONTS && glo->fonts > 0)
	{
		fz_printf(ctx, out, "Fonts (%d):\n", glo->fonts);
		for (i = 0; i < glo->fonts; i++)
		{
			fz_printf(ctx, out, PAGE_FMT "%s '%s' (%d %d R)\n",
				glo->font[i].page,
				pdf_to_num(ctx, glo->font[i].pageref),
				pdf_to_gen(ctx, glo->font[i].pageref),
				pdf_to_name(ctx, glo->font[i].u.font.subtype),
				pdf_to_name(ctx, glo->font[i].u.font.name),
				pdf_to_num(ctx, glo->font[i].u.font.obj),
				pdf_to_gen(ctx, glo->font[i].u.font.obj));
		}
		fz_printf(ctx, out, "\n");
	}

	if (show & IMAGES && glo->images > 0)
	{
		fz_printf(ctx, out, "Images (%d):\n", glo->images);
		for (i = 0; i < glo->images; i++)
		{
			char *cs = NULL;
			char *altcs = NULL;

			fz_printf(ctx, out, PAGE_FMT "[ ",
				glo->image[i].page,
				pdf_to_num(ctx, glo->image[i].pageref),
				pdf_to_gen(ctx, glo->image[i].pageref));

			if (pdf_is_array(ctx, glo->image[i].u.image.filter))
			{
				int n = pdf_array_len(ctx, glo->image[i].u.image.filter);
				for (j = 0; j < n; j++)
				{
					pdf_obj *obj = pdf_array_get(ctx, glo->image[i].u.image.filter, j);
					char *filter = fz_strdup(ctx, pdf_to_name(ctx, obj));

					if (strstr(filter, "Decode"))
						*(strstr(filter, "Decode")) = '\0';

					fz_printf(ctx, out, "%s%s",
						filter,
						j == pdf_array_len(ctx, glo->image[i].u.image.filter) - 1 ? "" : " ");
					fz_free(ctx, filter);
				}
			}
			else if (glo->image[i].u.image.filter)
			{
				pdf_obj *obj = glo->image[i].u.image.filter;
				char *filter = fz_strdup(ctx, pdf_to_name(ctx, obj));

				if (strstr(filter, "Decode"))
					*(strstr(filter, "Decode")) = '\0';

				fz_printf(ctx, out, "%s", filter);
				fz_free(ctx, filter);
			}
			else
				fz_printf(ctx, out, "Raw");

			if (glo->image[i].u.image.cs)
			{
				cs = fz_strdup(ctx, pdf_to_name(ctx, glo->image[i].u.image.cs));

				if (!strncmp(cs, "Device", 6))
				{
					int len = strlen(cs + 6);
					memmove(cs + 3, cs + 6, len + 1);
					cs[3 + len + 1] = '\0';
				}
				if (strstr(cs, "ICC"))
					fz_strlcpy(cs, "ICC", 4);
				if (strstr(cs, "Indexed"))
					fz_strlcpy(cs, "Idx", 4);
				if (strstr(cs, "Pattern"))
					fz_strlcpy(cs, "Pat", 4);
				if (strstr(cs, "Separation"))
					fz_strlcpy(cs, "Sep", 4);
			}
			if (glo->image[i].u.image.altcs)
			{
				altcs = fz_strdup(ctx, pdf_to_name(ctx, glo->image[i].u.image.altcs));

				if (!strncmp(altcs, "Device", 6))
				{
					int len = strlen(altcs + 6);
					memmove(altcs + 3, altcs + 6, len + 1);
					altcs[3 + len + 1] = '\0';
				}
				if (strstr(altcs, "ICC"))
					fz_strlcpy(altcs, "ICC", 4);
				if (strstr(altcs, "Indexed"))
					fz_strlcpy(altcs, "Idx", 4);
				if (strstr(altcs, "Pattern"))
					fz_strlcpy(altcs, "Pat", 4);
				if (strstr(altcs, "Separation"))
					fz_strlcpy(altcs, "Sep", 4);
			}

			fz_printf(ctx, out, " ] %dx%d %dbpc %s%s%s (%d %d R)\n",
				pdf_to_int(ctx, glo->image[i].u.image.width),
				pdf_to_int(ctx, glo->image[i].u.image.height),
				glo->image[i].u.image.bpc ? pdf_to_int(ctx, glo->image[i].u.image.bpc) : 1,
				glo->image[i].u.image.cs ? cs : "ImageMask",
				glo->image[i].u.image.altcs ? " " : "",
				glo->image[i].u.image.altcs ? altcs : "",
				pdf_to_num(ctx, glo->image[i].u.image.obj),
				pdf_to_gen(ctx, glo->image[i].u.image.obj));

			fz_free(ctx, cs);
			fz_free(ctx, altcs);
		}
		fz_printf(ctx, out, "\n");
	}

	if (show & SHADINGS && glo->shadings > 0)
	{
		fz_printf(ctx, out, "Shading patterns (%d):\n", glo->shadings);
		for (i = 0; i < glo->shadings; i++)
		{
			char *shadingtype[] =
			{
				"",
				"Function",
				"Axial",
				"Radial",
				"Triangle mesh",
				"Lattice",
				"Coons patch",
				"Tensor patch",
			};

			fz_printf(ctx, out, PAGE_FMT "%s (%d %d R)\n",
				glo->shading[i].page,
				pdf_to_num(ctx, glo->shading[i].pageref),
				pdf_to_gen(ctx, glo->shading[i].pageref),
				shadingtype[pdf_to_int(ctx, glo->shading[i].u.shading.type)],
				pdf_to_num(ctx, glo->shading[i].u.shading.obj),
				pdf_to_gen(ctx, glo->shading[i].u.shading.obj));
		}
		fz_printf(ctx, out, "\n");
	}

	if (show & PATTERNS && glo->patterns > 0)
	{
		fz_printf(ctx, out, "Patterns (%d):\n", glo->patterns);
		for (i = 0; i < glo->patterns; i++)
		{
			if (pdf_to_int(ctx, glo->pattern[i].u.pattern.type) == 1)
			{
				char *painttype[] =
				{
					"",
					"Colored",
					"Uncolored",
				};
				char *tilingtype[] =
				{
					"",
					"Constant",
					"No distortion",
					"Constant/fast tiling",
				};

				fz_printf(ctx, out, PAGE_FMT "Tiling %s %s (%d %d R)\n",
						glo->pattern[i].page,
						pdf_to_num(ctx, glo->pattern[i].pageref),
						pdf_to_gen(ctx, glo->pattern[i].pageref),
						painttype[pdf_to_int(ctx, glo->pattern[i].u.pattern.paint)],
						tilingtype[pdf_to_int(ctx, glo->pattern[i].u.pattern.tiling)],
						pdf_to_num(ctx, glo->pattern[i].u.pattern.obj),
						pdf_to_gen(ctx, glo->pattern[i].u.pattern.obj));
			}
			else
			{
				fz_printf(ctx, out, PAGE_FMT "Shading %d %d R (%d %d R)\n",
						glo->pattern[i].page,
						pdf_to_num(ctx, glo->pattern[i].pageref),
						pdf_to_gen(ctx, glo->pattern[i].pageref),
						pdf_to_num(ctx, glo->pattern[i].u.pattern.shading),
						pdf_to_gen(ctx, glo->pattern[i].u.pattern.shading),
						pdf_to_num(ctx, glo->pattern[i].u.pattern.obj),
						pdf_to_gen(ctx, glo->pattern[i].u.pattern.obj));
			}
		}
		fz_printf(ctx, out, "\n");
	}

	if (show & XOBJS && glo->forms > 0)
	{
		fz_printf(ctx, out, "Form xobjects (%d):\n", glo->forms);
		for (i = 0; i < glo->forms; i++)
		{
			fz_printf(ctx, out, PAGE_FMT "Form%s%s%s%s (%d %d R)\n",
				glo->form[i].page,
				pdf_to_num(ctx, glo->form[i].pageref),
				pdf_to_gen(ctx, glo->form[i].pageref),
				glo->form[i].u.form.groupsubtype ? " " : "",
				glo->form[i].u.form.groupsubtype ? pdf_to_name(ctx, glo->form[i].u.form.groupsubtype) : "",
				glo->form[i].u.form.groupsubtype ? " Group" : "",
				glo->form[i].u.form.reference ? " Reference" : "",
				pdf_to_num(ctx, glo->form[i].u.form.obj),
				pdf_to_gen(ctx, glo->form[i].u.form.obj));
		}
		fz_printf(ctx, out, "\n");
	}

	if (show & XOBJS && glo->psobjs > 0)
	{
		fz_printf(ctx, out, "Postscript xobjects (%d):\n", glo->psobjs);
		for (i = 0; i < glo->psobjs; i++)
		{
			fz_printf(ctx, out, PAGE_FMT "(%d %d R)\n",
				glo->psobj[i].page,
				pdf_to_num(ctx, glo->psobj[i].pageref),
				pdf_to_gen(ctx, glo->psobj[i].pageref),
				pdf_to_num(ctx, glo->psobj[i].u.form.obj),
				pdf_to_gen(ctx, glo->psobj[i].u.form.obj));
		}
		fz_printf(ctx, out, "\n");
	}
}

static void
showinfo(fz_context *ctx, globals *glo, char *filename, int show, char *pagelist)
{
	int page, spage, epage;
	char *spec, *dash;
	int allpages;
	int pagecount;
	fz_output *out = glo->out;

	if (!glo->doc)
		infousage();

	allpages = !strcmp(pagelist, "1-");

	pagecount = pdf_count_pages(ctx, glo->doc);
	spec = fz_strsep(&pagelist, ",");
	while (spec && pagecount)
	{
		dash = strchr(spec, '-');

		if (dash == spec)
			spage = epage = pagecount;
		else
			spage = epage = atoi(spec);

		if (dash)
		{
			if (strlen(dash) > 1)
				epage = atoi(dash + 1);
			else
				epage = pagecount;
		}

		if (spage > epage)
			page = spage, spage = epage, epage = page;

		spage = fz_clampi(spage, 1, pagecount);
		epage = fz_clampi(epage, 1, pagecount);

		if (allpages)
			fz_printf(ctx, out, "Retrieving info from pages %d-%d...\n", spage, epage);
		for (page = spage; page <= epage; page++)
		{
			gatherpageinfo(ctx, glo, page, show);
			if (!allpages)
			{
				fz_printf(ctx, out, "Page %d:\n", page);
				printinfo(ctx, glo, filename, show, page);
				fz_printf(ctx, out, "\n");
				clearinfo(ctx, glo);
			}
		}

		spec = fz_strsep(&pagelist, ",");
	}

	if (allpages)
		printinfo(ctx, glo, filename, show, -1);
}

static int arg_is_page_range(const char *arg)
{
	int c;

	while ((c = *arg++) != 0)
	{
		if ((c < '0' || c > '9') && (c != '-') && (c != ','))
			return 0;
	}
	return 1;
}

static void
pdfinfo_info(fz_context *ctx, fz_output *out, char *filename, char *password, int show, char *argv[], int argc)
{
	enum { NO_FILE_OPENED, NO_INFO_GATHERED, INFO_SHOWN } state;
	int argidx = 0;
	globals glo = { 0 };

	glo.out = out;
	glo.ctx = ctx;

	state = NO_FILE_OPENED;
	while (argidx < argc)
	{
		if (state == NO_FILE_OPENED || !arg_is_page_range(argv[argidx]))
		{
			if (state == NO_INFO_GATHERED)
			{
				showinfo(ctx, &glo, filename, show, "1-");
			}

			closexref(ctx, &glo);

			filename = argv[argidx];
			fz_printf(ctx, out, "%s:\n", filename);
			glo.doc = pdf_open_document(glo.ctx, filename);
			if (pdf_needs_password(ctx, glo.doc))
				if (!pdf_authenticate_password(ctx, glo.doc, password))
					fz_throw(glo.ctx, FZ_ERROR_GENERIC, "cannot authenticate password: %s", filename);
			glo.pagecount = pdf_count_pages(ctx, glo.doc);

			showglobalinfo(ctx, &glo);
			state = NO_INFO_GATHERED;
		}
		else
		{
			showinfo(ctx, &glo, filename, show, argv[argidx]);
			state = INFO_SHOWN;
		}

		argidx++;
	}

	if (state == NO_INFO_GATHERED)
		showinfo(ctx, &glo, filename, show, "1-");

	closexref(ctx, &glo);
}

int pdfinfo_main(int argc, char **argv)
{
	char *filename = "";
	char *password = "";
	int show = ALL;
	int c;
	fz_output *out = NULL;
	int ret;
	fz_context *ctx;

	while ((c = fz_getopt(argc, argv, "FISPXMp:")) != -1)
	{
		switch (c)
		{
		case 'F': if (show == ALL) show = FONTS; else show |= FONTS; break;
		case 'I': if (show == ALL) show = IMAGES; else show |= IMAGES; break;
		case 'S': if (show == ALL) show = SHADINGS; else show |= SHADINGS; break;
		case 'P': if (show == ALL) show = PATTERNS; else show |= PATTERNS; break;
		case 'X': if (show == ALL) show = XOBJS; else show |= XOBJS; break;
		case 'M': if (show == ALL) show = DIMENSIONS; else show |= DIMENSIONS; break;
		case 'p': password = fz_optarg; break;
		default:
			infousage();
			break;
		}
	}

	if (fz_optind == argc)
		infousage();

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx)
	{
		fprintf(stderr, "cannot initialise context\n");
		exit(1);
	}

	fz_var(out);

	ret = 0;
	fz_try(ctx)
	{
		out = fz_new_output_with_file(ctx, stdout, 0);
		pdfinfo_info(ctx, out, filename, password, show, &argv[fz_optind], argc-fz_optind);
	}
	fz_catch(ctx)
	{
		ret = 1;
	}
	fz_drop_output(ctx, out);
	fz_drop_context(ctx);
	return ret;
}
