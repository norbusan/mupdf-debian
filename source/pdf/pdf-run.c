#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

static void
pdf_run_annot_with_usage(fz_context *ctx, pdf_document *doc, pdf_page *page, pdf_annot *annot, fz_device *dev, const fz_matrix *ctm, const char *usage, fz_cookie *cookie)
{
	fz_matrix local_ctm, page_ctm;
	fz_rect mediabox;
	pdf_processor *proc = NULL;
	fz_default_colorspaces *default_cs;

	fz_var(proc);

	default_cs = pdf_load_default_colorspaces(ctx, doc, page);
	if (default_cs)
		fz_set_default_colorspaces(ctx, dev, default_cs);

	pdf_page_transform(ctx, page, &mediabox, &page_ctm);
	fz_concat(&local_ctm, &page_ctm, ctm);

	fz_try(ctx)
	{
		proc = pdf_new_run_processor(ctx, dev, &local_ctm, usage, NULL, 0, default_cs);
		pdf_process_annot(ctx, proc, doc, page, annot, cookie);
		pdf_close_processor(ctx, proc);
	}
	fz_always(ctx)
	{
		pdf_drop_processor(ctx, proc);
		fz_drop_default_colorspaces(ctx, default_cs);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);
}

static void
pdf_run_page_contents_with_usage(fz_context *ctx, pdf_document *doc, pdf_page *page, fz_device *dev, const fz_matrix *ctm, const char *usage, fz_cookie *cookie)
{
	fz_matrix local_ctm, page_ctm;
	pdf_obj *resources;
	pdf_obj *contents;
	fz_rect mediabox;
	pdf_processor *proc = NULL;
	fz_default_colorspaces *default_cs;
	fz_colorspace *colorspace = NULL;

	fz_var(proc);
	fz_var(colorspace);

	default_cs = pdf_load_default_colorspaces(ctx, doc, page);
	if (default_cs)
		fz_set_default_colorspaces(ctx, dev, default_cs);

	fz_try(ctx)
	{
		pdf_page_transform(ctx, page, &mediabox, &page_ctm);
		fz_concat(&local_ctm, &page_ctm, ctm);

		resources = pdf_page_resources(ctx, page);
		contents = pdf_page_contents(ctx, page);


		if (page->transparency)
		{
			pdf_obj *group = pdf_page_group(ctx, page);

			if (group)
			{
				pdf_obj *cs = pdf_dict_get(ctx, group, PDF_NAME_CS);
				if (cs)
				{
					fz_try(ctx)
						colorspace = pdf_load_colorspace(ctx, cs);
					fz_catch(ctx)
						colorspace = NULL;
				}
			}
			else
				colorspace = fz_keep_colorspace(ctx, fz_default_output_intent(ctx, default_cs));

			fz_begin_group(ctx, dev, fz_transform_rect(&mediabox, &local_ctm), colorspace, 1, 0, 0, 1);
			fz_drop_colorspace(ctx, colorspace);
			colorspace = NULL;
		}

		proc = pdf_new_run_processor(ctx, dev, &local_ctm, usage, NULL, 0, default_cs);
		pdf_process_contents(ctx, proc, doc, resources, contents, cookie);
		pdf_close_processor(ctx, proc);
	}
	fz_always(ctx)
	{
		fz_drop_default_colorspaces(ctx, default_cs);
		pdf_drop_processor(ctx, proc);
	}
	fz_catch(ctx)
	{
		fz_drop_colorspace(ctx, colorspace);
		fz_rethrow(ctx);
	}

	if (page->transparency)
		fz_end_group(ctx, dev);
}

void pdf_run_page_contents(fz_context *ctx, pdf_page *page, fz_device *dev, const fz_matrix *ctm, fz_cookie *cookie)
{
	pdf_document *doc = page->doc;
	int nocache;

	nocache = !!(dev->hints & FZ_NO_CACHE);
	if (nocache)
		pdf_mark_xref(ctx, doc);

	fz_try(ctx)
	{
		pdf_run_page_contents_with_usage(ctx, doc, page, dev, ctm, "View", cookie);
	}
	fz_always(ctx)
	{
		if (nocache)
			pdf_clear_xref_to_mark(ctx, doc);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
	if (page->incomplete & PDF_PAGE_INCOMPLETE_CONTENTS)
		fz_throw(ctx, FZ_ERROR_TRYLATER, "incomplete rendering");
}

void pdf_run_annot(fz_context *ctx, pdf_annot *annot, fz_device *dev, const fz_matrix *ctm, fz_cookie *cookie)
{
	pdf_page *page = annot->page;
	pdf_document *doc = page->doc;
	int nocache;

	nocache = !!(dev->hints & FZ_NO_CACHE);
	if (nocache)
		pdf_mark_xref(ctx, doc);
	fz_try(ctx)
	{
		pdf_run_annot_with_usage(ctx, doc, page, annot, dev, ctm, "View", cookie);
	}
	fz_always(ctx)
	{
		if (nocache)
			pdf_clear_xref_to_mark(ctx, doc);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
	if (page->incomplete & PDF_PAGE_INCOMPLETE_ANNOTS)
		fz_throw(ctx, FZ_ERROR_TRYLATER, "incomplete rendering");
}

static void
pdf_run_page_annots_with_usage(fz_context *ctx, pdf_document *doc, pdf_page *page, fz_device *dev, const fz_matrix *ctm, const char *usage, fz_cookie *cookie)
{
	pdf_annot *annot;

	if (cookie && cookie->progress_max != -1)
	{
		int count = 1;
		for (annot = page->annots; annot; annot = annot->next)
			count++;
		cookie->progress_max += count;
	}

	for (annot = page->annots; annot; annot = annot->next)
	{
		/* Check the cookie for aborting */
		if (cookie)
		{
			if (cookie->abort)
				break;
			cookie->progress++;
		}

		pdf_run_annot_with_usage(ctx, doc, page, annot, dev, ctm, usage, cookie);
	}
}

void
pdf_run_page_with_usage(fz_context *ctx, pdf_document *doc, pdf_page *page, fz_device *dev, const fz_matrix *ctm, const char *usage, fz_cookie *cookie)
{
	int nocache = !!(dev->hints & FZ_NO_CACHE);

	if (nocache)
		pdf_mark_xref(ctx, doc);
	fz_try(ctx)
	{
		pdf_run_page_contents_with_usage(ctx, doc, page, dev, ctm, usage, cookie);
		pdf_run_page_annots_with_usage(ctx, doc, page, dev, ctm, usage, cookie);
	}
	fz_always(ctx)
	{
		if (nocache)
			pdf_clear_xref_to_mark(ctx, doc);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
	if (page->incomplete)
		fz_throw(ctx, FZ_ERROR_TRYLATER, "incomplete rendering");
}

void
pdf_run_page(fz_context *ctx, pdf_page *page, fz_device *dev, const fz_matrix *ctm, fz_cookie *cookie)
{
	pdf_document *doc = page->doc;
	pdf_run_page_with_usage(ctx, doc, page, dev, ctm, "View", cookie);
}

void
pdf_run_glyph(fz_context *ctx, pdf_document *doc, pdf_obj *resources, fz_buffer *contents, fz_device *dev, const fz_matrix *ctm, void *gstate, int nested_depth, fz_default_colorspaces *default_cs)
{
	pdf_processor *proc;

	if (nested_depth > 10)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Too many nestings of Type3 glyphs");

	proc = pdf_new_run_processor(ctx, dev, ctm, "View", gstate, nested_depth+1, default_cs);
	fz_try(ctx)
	{
		pdf_process_glyph(ctx, proc, doc, resources, contents);
		pdf_close_processor(ctx, proc);
	}
	fz_always(ctx)
		pdf_drop_processor(ctx, proc);
	fz_catch(ctx)
		fz_rethrow(ctx);
}
