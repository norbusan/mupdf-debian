#include "mupdf/fitz.h"

fz_display_list *
fz_new_display_list_from_page(fz_context *ctx, fz_page *page)
{
	fz_display_list *list;
	fz_device *dev;

	list = fz_new_display_list(ctx);

	fz_try(ctx)
	{
		dev = fz_new_list_device(ctx, list);
		fz_run_page(ctx, page, dev, &fz_identity, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_display_list(ctx, list);
		fz_rethrow(ctx);
	}

	return list;
}

fz_display_list *
fz_new_display_list_from_page_number(fz_context *ctx, fz_document *doc, int number)
{
	fz_page *page;
	fz_display_list *list;

	page = fz_load_page(ctx, doc, number);
	fz_try(ctx)
		list = fz_new_display_list_from_page(ctx, page);
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return list;
}

fz_pixmap *
fz_new_pixmap_from_display_list(fz_context *ctx, fz_display_list *list, const fz_matrix *ctm, fz_colorspace *cs, int background)
{
	fz_rect rect;
	fz_irect irect;
	fz_pixmap *pix;
	fz_device *dev;

	fz_bound_display_list(ctx, list, &rect);
	fz_transform_rect(&rect, ctm);
	fz_round_rect(&irect, &rect);

	pix = fz_new_pixmap_with_bbox(ctx, cs, &irect);
	if (background)
		fz_clear_pixmap_with_value(ctx, pix, 0xFF);
	else
		fz_clear_pixmap(ctx, pix);

	fz_try(ctx)
	{
		dev = fz_new_draw_device(ctx, pix);
		fz_run_display_list(ctx, list, dev, ctm, NULL, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_pixmap(ctx, pix);
		fz_rethrow(ctx);
	}

	return pix;
}

fz_pixmap *
fz_new_pixmap_from_page_contents(fz_context *ctx, fz_page *page, const fz_matrix *ctm, fz_colorspace *cs)
{
	fz_rect rect;
	fz_irect irect;
	fz_pixmap *pix;
	fz_device *dev;

	fz_bound_page(ctx, page, &rect);
	fz_transform_rect(&rect, ctm);
	fz_round_rect(&irect, &rect);

	pix = fz_new_pixmap_with_bbox(ctx, cs, &irect);
	fz_clear_pixmap_with_value(ctx, pix, 0xFF);

	fz_try(ctx)
	{
		dev = fz_new_draw_device(ctx, pix);
		fz_run_page_contents(ctx, page, dev, ctm, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_pixmap(ctx, pix);
		fz_rethrow(ctx);
	}

	return pix;
}

fz_pixmap *
fz_new_pixmap_from_annot(fz_context *ctx, fz_annot *annot, const fz_matrix *ctm, fz_colorspace *cs)
{
	fz_rect rect;
	fz_irect irect;
	fz_pixmap *pix;
	fz_device *dev;

	fz_bound_annot(ctx, annot, &rect);
	fz_transform_rect(&rect, ctm);
	fz_round_rect(&irect, &rect);

	pix = fz_new_pixmap_with_bbox(ctx, cs, &irect);
	fz_clear_pixmap(ctx, pix);

	fz_try(ctx)
	{
		dev = fz_new_draw_device(ctx, pix);
		fz_run_annot(ctx, annot, dev, ctm, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_pixmap(ctx, pix);
		fz_rethrow(ctx);
	}

	return pix;
}

fz_pixmap *
fz_new_pixmap_from_page(fz_context *ctx, fz_page *page, const fz_matrix *ctm, fz_colorspace *cs)
{
	fz_rect rect;
	fz_irect irect;
	fz_pixmap *pix;
	fz_device *dev;

	fz_bound_page(ctx, page, &rect);
	fz_transform_rect(&rect, ctm);
	fz_round_rect(&irect, &rect);

	pix = fz_new_pixmap_with_bbox(ctx, cs, &irect);
	fz_clear_pixmap_with_value(ctx, pix, 0xFF);

	fz_try(ctx)
	{
		dev = fz_new_draw_device(ctx, pix);
		fz_run_page(ctx, page, dev, ctm, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_pixmap(ctx, pix);
		fz_rethrow(ctx);
	}

	return pix;
}

fz_pixmap *
fz_new_pixmap_from_page_number(fz_context *ctx, fz_document *doc, int number, const fz_matrix *ctm, fz_colorspace *cs)
{
	fz_page *page;
	fz_pixmap *pix;

	page = fz_load_page(ctx, doc, number);
	fz_try(ctx)
		pix = fz_new_pixmap_from_page(ctx, page, ctm, cs);
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return pix;
}

fz_stext_page *
fz_new_stext_page_from_display_list(fz_context *ctx, fz_display_list *list, fz_stext_sheet *sheet)
{
	fz_stext_page *text;
	fz_device *dev;

	text = fz_new_stext_page(ctx);
	fz_try(ctx)
	{
		dev = fz_new_stext_device(ctx, sheet, text);
		fz_run_display_list(ctx, list, dev, &fz_identity, NULL, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_stext_page(ctx, text);
		fz_rethrow(ctx);
	}

	return text;
}

fz_stext_page *
fz_new_stext_page_from_page(fz_context *ctx, fz_page *page, fz_stext_sheet *sheet)
{
	fz_stext_page *text;
	fz_device *dev;

	text = fz_new_stext_page(ctx);
	fz_try(ctx)
	{
		dev = fz_new_stext_device(ctx, sheet, text);
		fz_run_page(ctx, page, dev, &fz_identity, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_drop_stext_page(ctx, text);
		fz_rethrow(ctx);
	}

	return text;
}

fz_stext_page *
fz_new_stext_page_from_page_number(fz_context *ctx, fz_document *doc, int number, fz_stext_sheet *sheet)
{
	fz_page *page;
	fz_stext_page *text;

	page = fz_load_page(ctx, doc, number);
	fz_try(ctx)
		text = fz_new_stext_page_from_page(ctx, page, sheet);
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return text;
}

int
fz_search_display_list(fz_context *ctx, fz_display_list *list, const char *needle, fz_rect *hit_bbox, int hit_max)
{
	fz_stext_sheet *sheet;
	fz_stext_page *text;
	int count;

	sheet = fz_new_stext_sheet(ctx);
	fz_try(ctx)
	{
		text = fz_new_stext_page_from_display_list(ctx, list, sheet);
		count = fz_search_stext_page(ctx, text, needle, hit_bbox, hit_max);
	}
	fz_always(ctx)
		fz_drop_stext_sheet(ctx, sheet);
	fz_catch(ctx)
		fz_rethrow(ctx);
	fz_drop_stext_page(ctx, text);
	return count;
}

int
fz_search_page(fz_context *ctx, fz_page *page, const char *needle, fz_rect *hit_bbox, int hit_max)
{
	fz_stext_sheet *sheet;
	fz_stext_page *text;
	int count;

	sheet = fz_new_stext_sheet(ctx);
	fz_try(ctx)
	{
		text = fz_new_stext_page_from_page(ctx, page, sheet);
		count = fz_search_stext_page(ctx, text, needle, hit_bbox, hit_max);
	}
	fz_always(ctx)
		fz_drop_stext_sheet(ctx, sheet);
	fz_catch(ctx)
		fz_rethrow(ctx);
	fz_drop_stext_page(ctx, text);
	return count;
}

int
fz_search_page_number(fz_context *ctx, fz_document *doc, int number, const char *needle, fz_rect *hit_bbox, int hit_max)
{
	fz_page *page;
	int count;

	page = fz_load_page(ctx, doc, number);
	fz_try(ctx)
		count = fz_search_page(ctx, page, needle, hit_bbox, hit_max);
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return count;
}

fz_buffer *
fz_new_buffer_from_stext_page(fz_context *ctx, fz_stext_page *text, const fz_rect *sel, int crlf)
{
	fz_buffer *buf;
	fz_rect hitbox;
	float x0, y0, x1, y1;
	int block_num;
	int need_newline;
	int i;

	need_newline = 0;

	if (fz_is_infinite_rect(sel))
	{
		x0 = y0 = -FLT_MAX;
		x1 = y1 = FLT_MAX;
	}
	else
	{
		x0 = sel->x0;
		y0 = sel->y0;
		x1 = sel->x1;
		y1 = sel->y1;
	}

	buf = fz_new_buffer(ctx, 256);
	fz_try(ctx)
	{
		for (block_num = 0; block_num < text->len; block_num++)
		{
			fz_stext_line *line;
			fz_stext_block *block;
			fz_stext_span *span;

			if (text->blocks[block_num].type != FZ_PAGE_BLOCK_TEXT)
				continue;

			block = text->blocks[block_num].u.text;
			for (line = block->lines; line < block->lines + block->len; line++)
			{
				int saw_text = 0;
				for (span = line->first_span; span; span = span->next)
				{
					for (i = 0; i < span->len; i++)
					{
						int c;
						fz_stext_char_bbox(ctx, &hitbox, span, i);
						c = span->text[i].c;
						if (c < 32)
							c = '?';
						if (hitbox.x1 >= x0 && hitbox.x0 <= x1 && hitbox.y1 >= y0 && hitbox.y0 <= y1)
						{
							saw_text = 1;
							if (need_newline)
							{
								if (crlf)
									fz_write_buffer_rune(ctx, buf, '\r');
								fz_write_buffer_rune(ctx, buf, '\n');
								need_newline = 0;
							}
							fz_write_buffer_rune(ctx, buf, c);
						}
					}
				}

				if (saw_text)
					need_newline = 1;
			}
		}
	}
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, buf);
		fz_rethrow(ctx);
	}

	return buf;
}

fz_buffer *
fz_new_buffer_from_display_list(fz_context *ctx, fz_display_list *list, const fz_rect *sel, int crlf)
{
	fz_stext_sheet *sheet;
	fz_stext_page *text;
	fz_buffer *buf;

	sheet = fz_new_stext_sheet(ctx);
	fz_try(ctx)
	{
		text = fz_new_stext_page_from_display_list(ctx, list, sheet);
		buf = fz_new_buffer_from_stext_page(ctx, text, sel, crlf);
	}
	fz_always(ctx)
		fz_drop_stext_sheet(ctx, sheet);
	fz_catch(ctx)
		fz_rethrow(ctx);
	fz_drop_stext_page(ctx, text);
	return buf;
}

fz_buffer *
fz_new_buffer_from_page(fz_context *ctx, fz_page *page, const fz_rect *sel, int crlf)
{
	fz_stext_sheet *sheet;
	fz_stext_page *text;
	fz_buffer *buf;

	sheet = fz_new_stext_sheet(ctx);
	fz_try(ctx)
	{
		text = fz_new_stext_page_from_page(ctx, page, sheet);
		buf = fz_new_buffer_from_stext_page(ctx, text, sel, crlf);
	}
	fz_always(ctx)
		fz_drop_stext_sheet(ctx, sheet);
	fz_catch(ctx)
		fz_rethrow(ctx);
	fz_drop_stext_page(ctx, text);
	return buf;
}

fz_buffer *
fz_new_buffer_from_page_number(fz_context *ctx, fz_document *doc, int number, const fz_rect *sel, int crlf)
{
	fz_page *page;
	fz_buffer *buf;

	page = fz_load_page(ctx, doc, number);
	fz_try(ctx)
		buf = fz_new_buffer_from_page(ctx, page, sel, crlf);
	fz_always(ctx)
		fz_drop_page(ctx, page);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return buf;
}
