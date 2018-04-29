#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <float.h>
#include <limits.h>
#include <math.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H

#define MATRIX_COEFS (6)
#define STRIKE_HEIGHT (0.375f)
#define UNDERLINE_HEIGHT (0.075f)
#define LINE_THICKNESS (0.07f)
#define SMALL_FLOAT (0.00001f)
#define LIST_SEL_COLOR_R (0.6f)
#define LIST_SEL_COLOR_G (0.75f)
#define LIST_SEL_COLOR_B (0.85f)

enum
{
	Q_Left = 0,
	Q_Cent = 1,
	Q_Right = 2
};

enum
{
	BS_Solid,
	BS_Dashed,
	BS_Beveled,
	BS_Inset,
	BS_Underline
};

typedef struct font_info_s
{
	pdf_da_info da_rec;
	pdf_font_desc *font;
	float lineheight;
} font_info;

typedef struct text_widget_info_s
{
	pdf_obj *dr;
	pdf_obj *col;
	font_info font_rec;
	int q;
	int multiline;
	int comb;
	int max_len;
} text_widget_info;

static const char *fmt_re = "%g %g %g %g re\n";
static const char *fmt_f = "f\n";
static const char *fmt_s = "s\n";
static const char *fmt_g = "%g g\n";
static const char *fmt_m = "%g %g m\n";
static const char *fmt_l = "%g %g l\n";
static const char *fmt_w = "%g w\n";
static const char *fmt_Tx_BMC = "/Tx BMC\n";
static const char *fmt_q = "q\n";
static const char *fmt_W = "W\n";
static const char *fmt_n = "n\n";
static const char *fmt_BT = "BT\n";
static const char *fmt_Tm = "%g %g %g %g %g %g Tm\n";
static const char *fmt_Td = "%g %g Td\n";
static const char *fmt_Tj = " Tj\n";
static const char *fmt_ET = "ET\n";
static const char *fmt_Q = "Q\n";
static const char *fmt_EMC = "EMC\n";

void pdf_da_info_fin(fz_context *ctx, pdf_da_info *di)
{
	fz_free(ctx, di->font_name);
	di->font_name = NULL;
}

static void da_check_stack(float *stack, int *top)
{
	if (*top == 32)
	{
		memmove(stack, stack + 1, 31 * sizeof(stack[0]));
		*top = 31;
	}
}

void pdf_parse_da(fz_context *ctx, char *da, pdf_da_info *di)
{
	float stack[32] = { 0.0f };
	int top = 0;
	pdf_token tok;
	char *name = NULL;
	pdf_lexbuf lbuf;
	fz_stream *str = fz_open_memory(ctx, (unsigned char *)da, strlen(da));

	pdf_lexbuf_init(ctx, &lbuf, PDF_LEXBUF_SMALL);

	fz_var(str);
	fz_var(name);
	fz_try(ctx)
	{
		for (tok = pdf_lex(ctx, str, &lbuf); tok != PDF_TOK_EOF; tok = pdf_lex(ctx, str, &lbuf))
		{
			switch (tok)
			{
			case PDF_TOK_NAME:
				fz_free(ctx, name);
				name = fz_strdup(ctx, lbuf.scratch);
				break;

			case PDF_TOK_INT:
				da_check_stack(stack, &top);
				stack[top] = lbuf.i;
				top ++;
				break;

			case PDF_TOK_REAL:
				da_check_stack(stack, &top);
				stack[top] = lbuf.f;
				top ++;
				break;

			case PDF_TOK_KEYWORD:
				if (!strcmp(lbuf.scratch, "Tf"))
				{
					di->font_size = stack[0];
					fz_free(ctx, di->font_name);
					di->font_name = name;
					name = NULL;
				}
				else if (!strcmp(lbuf.scratch, "rg"))
				{
					di->col[0] = stack[0];
					di->col[1] = stack[1];
					di->col[2] = stack[2];
					di->col_size = 3;
				}
				else if (!strcmp(lbuf.scratch, "g"))
				{
					di->col[0] = stack[0];
					di->col_size = 1;
				}

				fz_free(ctx, name);
				name = NULL;
				top = 0;
				break;

			default:
				break;
			}
		}
	}
	fz_always(ctx)
	{
		fz_free(ctx, name);
		fz_drop_stream(ctx, str);
		pdf_lexbuf_fin(ctx, &lbuf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void get_font_info(fz_context *ctx, pdf_document *doc, pdf_obj *dr, char *da, font_info *font_rec)
{
	pdf_font_desc *font;
	pdf_obj *fontobj;

	pdf_parse_da(ctx, da, &font_rec->da_rec);
	if (font_rec->da_rec.font_name == NULL)
		fz_throw(ctx, FZ_ERROR_GENERIC, "No font name in default appearance");

	fontobj = pdf_dict_gets(ctx, pdf_dict_get(ctx, dr, PDF_NAME_Font), font_rec->da_rec.font_name);
	if (!fontobj)
	{
		fz_font *helv = fz_new_base14_font(ctx, "Helvetica");
		fz_warn(ctx, "form resource dictionary is missing the default appearance font");
		fontobj = pdf_add_simple_font(ctx, doc, helv, PDF_SIMPLE_ENCODING_LATIN);
		pdf_dict_puts_drop(ctx, pdf_dict_get(ctx, dr, PDF_NAME_Font), font_rec->da_rec.font_name, fontobj);
		fz_drop_font(ctx, helv);
	}
	font_rec->font = font = pdf_load_font(ctx, doc, dr, fontobj, 0);
	font_rec->lineheight = 1.0f;
	if (font && font->ascent != 0.0f && font->descent != 0.0f)
		font_rec->lineheight = (font->ascent - font->descent) / 1000.0f;
}

static void font_info_fin(fz_context *ctx, font_info *font_rec)
{
	pdf_drop_font(ctx, font_rec->font);
	font_rec->font = NULL;
	pdf_da_info_fin(ctx, &font_rec->da_rec);
}

static void get_text_widget_info(fz_context *ctx, pdf_document *doc, pdf_obj *widget, text_widget_info *info)
{
	char *da = pdf_to_str_buf(ctx, pdf_get_inheritable(ctx, doc, widget, PDF_NAME_DA));
	int ff = pdf_get_field_flags(ctx, doc, widget);
	pdf_obj *ml = pdf_get_inheritable(ctx, doc, widget, PDF_NAME_MaxLen);

	info->dr = pdf_get_inheritable(ctx, doc, widget, PDF_NAME_DR);
	info->col = pdf_dict_getl(ctx, widget, PDF_NAME_MK, PDF_NAME_BG, NULL);
	info->q = pdf_to_int(ctx, pdf_get_inheritable(ctx, doc, widget, PDF_NAME_Q));
	info->multiline = (ff & Ff_Multiline) != 0;
	info->comb = (ff & (Ff_Multiline|Ff_Password|Ff_FileSelect|Ff_Comb)) == Ff_Comb;

	if (ml == NULL)
		info->comb = 0;
	else
		info->max_len = pdf_to_int(ctx, ml);

	get_font_info(ctx, doc, info->dr, da, &info->font_rec);
}

void pdf_fzbuf_print_da(fz_context *ctx, fz_buffer *fzbuf, pdf_da_info *di)
{
	if (di->font_name != NULL && di->font_size != 0)
		fz_append_printf(ctx, fzbuf, "/%s %d Tf", di->font_name, di->font_size);

	switch (di->col_size)
	{
	case 1:
		fz_append_printf(ctx, fzbuf, " %g g", di->col[0]);
		break;

	case 3:
		fz_append_printf(ctx, fzbuf, " %g %g %g rg", di->col[0], di->col[1], di->col[2]);
		break;

	case 4:
		fz_append_printf(ctx, fzbuf, " %g %g %g %g k", di->col[0], di->col[1], di->col[2], di->col[3]);
		break;

	default:
		fz_append_string(ctx, fzbuf, " 0 g");
		break;
	}
}

static fz_rect *measure_text(fz_context *ctx, pdf_document *doc, font_info *font_rec, const fz_matrix *tm, char *text, fz_rect *bbox)
{
	pdf_measure_text(ctx, font_rec->font, (unsigned char *)text, strlen(text), bbox);

	bbox->x0 *= font_rec->da_rec.font_size * tm->a;
	bbox->y0 *= font_rec->da_rec.font_size * tm->d;
	bbox->x1 *= font_rec->da_rec.font_size * tm->a;
	bbox->y1 *= font_rec->da_rec.font_size * tm->d;

	return bbox;
}

static void fzbuf_print_color(fz_context *ctx, fz_buffer *fzbuf, pdf_obj *arr, int stroke, float adj)
{
	switch (pdf_array_len(ctx, arr))
	{
	case 1:
		fz_append_printf(ctx, fzbuf, stroke?"%g G\n":"%g g\n",
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 0)) + adj);
		break;
	case 3:
		fz_append_printf(ctx, fzbuf, stroke?"%g %g %g RG\n":"%g %g %g rg\n",
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 0)) + adj,
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 1)) + adj,
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 2)) + adj);
		break;
	case 4:
		fz_append_printf(ctx, fzbuf, stroke?"%g %g %g %g K\n":"%g %g %g %g k\n",
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 0)),
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 1)),
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 2)),
			pdf_to_real(ctx, pdf_array_get(ctx, arr, 3)));
		break;
	}
}

static void fzbuf_print_rect_fill(fz_context *ctx, fz_buffer *fzbuf, const fz_rect *clip,
	float col[4], int col_size, const fz_matrix *tm, const fz_rect *rect)
{
	if (clip)
	{
		fz_append_printf(ctx, fzbuf, fmt_re, clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0);
		fz_append_printf(ctx, fzbuf, fmt_W);
	}
	if (col_size > 0)
	{
		switch (col_size)
		{
		case 1:
			fz_append_printf(ctx, fzbuf, "%g g\n", col[0]);
			break;
		case 3:
			fz_append_printf(ctx, fzbuf, "%g %g %g rg\n", col[0], col[1], col[2]);
			break;
		case 4:
			fz_append_printf(ctx, fzbuf, "%g %g %g %g k\n", col[0], col[1], col[2], col[3]);
			break;
		default:
			break;
		}
	}
	if (tm)
		fz_append_printf(ctx, fzbuf, fmt_Tm, tm->a, tm->b, tm->c, tm->d, tm->e, tm->f);

	fz_append_printf(ctx, fzbuf, fmt_re, rect->x0, rect->y0, rect->x1 - rect->x0, rect->y1 - rect->y0);
	fz_append_printf(ctx, fzbuf, fmt_f);
}

static void fzbuf_print_text(fz_context *ctx, fz_buffer *fzbuf, const fz_rect *clip, pdf_obj *col, font_info *font_rec, const fz_matrix *tm, char *text)
{
	fz_append_printf(ctx, fzbuf, fmt_q);
	if (clip)
	{
		fz_append_printf(ctx, fzbuf, fmt_re, clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0);
		fz_append_printf(ctx, fzbuf, fmt_W);
		if (col)
		{
			fzbuf_print_color(ctx, fzbuf, col, 0, 0.0f);
			fz_append_printf(ctx, fzbuf, fmt_f);
		}
		else
		{
			fz_append_printf(ctx, fzbuf, fmt_n);
		}
	}

	fz_append_printf(ctx, fzbuf, fmt_BT);

	pdf_fzbuf_print_da(ctx, fzbuf, &font_rec->da_rec);

	fz_append_printf(ctx, fzbuf, "\n");
	if (tm)
		fz_append_printf(ctx, fzbuf, fmt_Tm, tm->a, tm->b, tm->c, tm->d, tm->e, tm->f);

	fz_append_pdf_string(ctx, fzbuf, text);
	fz_append_printf(ctx, fzbuf, fmt_Tj);
	fz_append_printf(ctx, fzbuf, fmt_ET);
	fz_append_printf(ctx, fzbuf, fmt_Q);
}

static fz_buffer *create_text_buffer(fz_context *ctx, const fz_rect *clip, text_widget_info *info, const fz_matrix *tm, char *text)
{
	fz_buffer *fzbuf = fz_new_buffer(ctx, 0);

	fz_try(ctx)
	{
		fz_append_printf(ctx, fzbuf, fmt_Tx_BMC);
		fzbuf_print_text(ctx, fzbuf, clip, info->col, &info->font_rec, tm, text);
		fz_append_printf(ctx, fzbuf, fmt_EMC);
	}
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, fzbuf);
		fz_rethrow(ctx);
	}

	return fzbuf;
}

static fz_buffer *create_aligned_text_buffer(fz_context *ctx, pdf_document *doc, const fz_rect *clip, text_widget_info *info, const fz_matrix *tm, char *text)
{
	fz_matrix atm = *tm;

	if (info->q != Q_Left)
	{
		fz_rect rect;

		measure_text(ctx, doc, &info->font_rec, tm, text, &rect);
		atm.e -= info->q == Q_Right ? rect.x1 : (rect.x1 - rect.x0) / 2;
	}

	return create_text_buffer(ctx, clip, info, &atm, text);
}

static void measure_ascent_descent(fz_context *ctx, pdf_document *doc, font_info *finf, char *text, float *ascent, float *descent)
{
	char *testtext = NULL;
	fz_rect bbox;
	font_info tinf = *finf;

	fz_var(testtext);
	fz_try(ctx)
	{
		/* Heuristic: adding "My" to text will in most cases
		 * produce a measurement that will encompass all chars */
		testtext = fz_malloc(ctx, strlen(text) + 3);
		strcpy(testtext, "My");
		strcat(testtext, text);
		tinf.da_rec.font_size = 1;
		measure_text(ctx, doc, &tinf, &fz_identity, testtext, &bbox);
		*descent = -bbox.y0;
		*ascent = bbox.y1;
	}
	fz_always(ctx)
	{
		fz_free(ctx, testtext);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

typedef struct text_splitter_s
{
	font_info *info;
	float width;
	float height;
	float scale;
	float unscaled_width;
	float fontsize;
	float lineheight;
	const char *text;
	int done;
	float x_orig;
	float y_orig;
	float x;
	float x_end;
	size_t text_start;
	size_t text_end;
	int max_lines;
	int retry;
} text_splitter;

static void text_splitter_init(text_splitter *splitter, font_info *info, const char *text, float width, float height, int variable)
{
	float fontsize = info->da_rec.font_size;

	memset(splitter, 0, sizeof(*splitter));
	splitter->info = info;
	splitter->text = text;
	splitter->width = width;
	splitter->unscaled_width = width;
	splitter->height = height;
	splitter->fontsize = fontsize;
	splitter->scale = 1.0f;
	splitter->lineheight = fontsize * info->lineheight ;
	/* RJW: The cast in the following line is important, as otherwise
	 * under MSVC in the variable = 0 case, splitter->max_lines becomes
	 * INT_MIN. */
	splitter->max_lines = variable ? (int)(height/splitter->lineheight) : INT_MAX;
}

static void text_splitter_start_pass(text_splitter *splitter)
{
	splitter->text_end = 0;
	splitter->x_orig = 0;
	splitter->y_orig = 0;
}

static void text_splitter_start_line(text_splitter *splitter)
{
	splitter->x_end = 0;
}

static int text_splitter_layout(fz_context *ctx, text_splitter *splitter)
{
	const char *text;
	float room;
	float stride;
	size_t count;
	size_t len;
	float fontsize = splitter->info->da_rec.font_size;

	splitter->x = splitter->x_end;
	splitter->text_start = splitter->text_end;

	text = splitter->text + splitter->text_start;
	room = splitter->unscaled_width - splitter->x;

	if (strchr("\r\n", text[0]))
	{
		/* Consume return chars and report end of line */
		splitter->text_end += strspn(text, "\r\n");
		splitter->text_start = splitter->text_end;
		splitter->done = (splitter->text[splitter->text_end] == '\0');
		return 0;
	}
	else if (text[0] == ' ')
	{
		/* Treat each space as a word */
		len = 1;
	}
	else
	{
		len = 0;
		while (text[len] != '\0' && !strchr(" \r\n", text[len]))
			len ++;
	}

	stride = pdf_text_stride(ctx, splitter->info->font, fontsize, (unsigned char *)text, len, room, &count);

	/* If not a single char fits although the line is empty, then force one char */
	if (count == 0 && splitter->x == 0.0f)
		stride = pdf_text_stride(ctx, splitter->info->font, fontsize, (unsigned char *)text, 1, FLT_MAX, &count);

	if (count < len && splitter->retry)
	{
		/* The word didn't fit and we are in retry mode. Work out the
		 * least additional scaling that may help */
		float fitwidth; /* width if we force the word in */
		float hstretchwidth; /* width if we just bump by 10% */
		float vstretchwidth; /* width resulting from forcing in another line */
		float bestwidth;

		fitwidth = splitter->x +
			pdf_text_stride(ctx, splitter->info->font, fontsize, (unsigned char *)text, len, FLT_MAX, &count);
		/* FIXME: temporary fiddle factor. Would be better to work in integers */
		fitwidth *= 1.001f;

		/* Stretching by 10% is worth trying only if processing the first word on the line */
		hstretchwidth = splitter->x == 0.0f
			? splitter->width * 1.1f / splitter->scale
			: FLT_MAX;

		vstretchwidth = splitter->width * (splitter->max_lines + 1) * splitter->lineheight
			/ splitter->height;

		bestwidth = fz_min(fitwidth, fz_min(hstretchwidth, vstretchwidth));

		if (bestwidth == vstretchwidth)
			splitter->max_lines ++;

		splitter->scale = splitter->width / bestwidth;
		splitter->unscaled_width = bestwidth;

		splitter->retry = 0;

		/* Try again */
		room = splitter->unscaled_width - splitter->x;
		stride = pdf_text_stride(ctx, splitter->info->font, fontsize, (unsigned char *)text, len, room, &count);
	}

	/* This is not the first word on the line. Best to give up on this line and push
	 * the word onto the next */
	if (count < len && splitter->x > 0.0f)
		return 0;

	splitter->text_end = splitter->text_start + count;
	splitter->x_end = splitter->x + stride;
	splitter->done = (splitter->text[splitter->text_end] == '\0');
	return 1;
}

static void text_splitter_move(text_splitter *splitter, float newy, float *relx, float *rely)
{
	*relx = splitter->x - splitter->x_orig;
	*rely = newy * splitter->lineheight - splitter->y_orig;

	splitter->x_orig = splitter->x;
	splitter->y_orig = newy * splitter->lineheight;
}

static void text_splitter_retry(text_splitter *splitter)
{
	if (splitter->retry)
	{
		/* Already tried expanding lines. Overflow must
		 * be caused by carriage control */
		splitter->max_lines ++;
		splitter->retry = 0;
		splitter->unscaled_width = splitter->width * splitter->max_lines * splitter->lineheight
			/ splitter->height;
		splitter->scale = splitter->width / splitter->unscaled_width;
	}
	else
	{
		splitter->retry = 1;
	}
}

static void fzbuf_print_text_start1(fz_context *ctx, fz_buffer *fzbuf, const fz_rect *clip, pdf_obj *col)
{
	fz_append_printf(ctx, fzbuf, fmt_Tx_BMC);
	fz_append_printf(ctx, fzbuf, fmt_q);

	if (clip)
	{
		fz_append_printf(ctx, fzbuf, fmt_re, clip->x0, clip->y0, clip->x1 - clip->x0, clip->y1 - clip->y0);
		fz_append_printf(ctx, fzbuf, fmt_W);
		if (col)
		{
			fzbuf_print_color(ctx, fzbuf, col, 0, 0.0f);
			fz_append_printf(ctx, fzbuf, fmt_f);
		}
		else
		{
			fz_append_printf(ctx, fzbuf, fmt_n);
		}
	}
}

static void fzbuf_print_text_start2(fz_context *ctx, fz_buffer *fzbuf, font_info *font, const fz_matrix *tm)
{
	fz_append_printf(ctx, fzbuf, fmt_BT);

	pdf_fzbuf_print_da(ctx, fzbuf, &font->da_rec);
	fz_append_printf(ctx, fzbuf, "\n");

	fz_append_printf(ctx, fzbuf, fmt_Tm, tm->a, tm->b, tm->c, tm->d, tm->e, tm->f);
}

static void fzbuf_print_text_end(fz_context *ctx, fz_buffer *fzbuf)
{
	fz_append_printf(ctx, fzbuf, fmt_ET);
	fz_append_printf(ctx, fzbuf, fmt_Q);
	fz_append_printf(ctx, fzbuf, fmt_EMC);
}

static void fzbuf_print_text_word(fz_context *ctx, fz_buffer *fzbuf, float x, float y, char *text, size_t count)
{
	size_t i;

	fz_append_printf(ctx, fzbuf, fmt_Td, x, y);
	fz_append_printf(ctx, fzbuf, "(");

	for (i = 0; i < count; i++)
		fz_append_printf(ctx, fzbuf, "%c", text[i]);

	fz_append_printf(ctx, fzbuf, ") Tj\n");
}

static fz_buffer *create_text_appearance(fz_context *ctx, pdf_document *doc, const fz_rect *bbox, const fz_matrix *oldtm, text_widget_info *info, char *text)
{
	int fontsize;
	int variable;
	float height, width, full_width;
	fz_buffer *fzbuf = NULL;
	fz_buffer *fztmp = NULL;
	fz_rect rect;
	fz_rect tbox;
	rect = *bbox;

	if (rect.x1 - rect.x0 > 3.0f && rect.y1 - rect.y0 > 3.0f)
	{
		rect.x0 += 1.0f;
		rect.x1 -= 1.0f;
		rect.y0 += 1.0f;
		rect.y1 -= 1.0f;
	}

	height = rect.y1 - rect.y0;
	width = rect.x1 - rect.x0;
	full_width = bbox->x1 - bbox->x0;

	fz_var(fzbuf);
	fz_var(fztmp);
	fz_try(ctx)
	{
		float ascent, descent;
		fz_matrix tm;

		variable = (info->font_rec.da_rec.font_size == 0);
		fontsize = variable
			? (info->multiline ? 14.0f : height / info->font_rec.lineheight)
			: info->font_rec.da_rec.font_size;

		info->font_rec.da_rec.font_size = fontsize;

		measure_ascent_descent(ctx, doc, &info->font_rec, text, &ascent, &descent);

		if (info->multiline)
		{
			text_splitter splitter;

			text_splitter_init(&splitter, &info->font_rec, text, width, height, variable);

			while (!splitter.done)
			{
				/* Try a layout pass */
				int line = 0;

				fz_drop_buffer(ctx, fztmp);
				fztmp = NULL;
				fztmp = fz_new_buffer(ctx, 0);

				text_splitter_start_pass(&splitter);

				/* Layout unscaled text to a scaled-up width, so that
				 * the scaled-down text will fit the unscaled width */

				while (!splitter.done && line < splitter.max_lines)
				{
					/* Layout a line */
					text_splitter_start_line(&splitter);

					while (!splitter.done && text_splitter_layout(ctx, &splitter))
					{
						if (splitter.text[splitter.text_start] != ' ')
						{
							float x, y;
							char *word = text+splitter.text_start;
							size_t wordlen = splitter.text_end-splitter.text_start;

							text_splitter_move(&splitter, -line, &x, &y);
							fzbuf_print_text_word(ctx, fztmp, x, y, word, wordlen);
						}
					}

					line ++;
				}

				if (!splitter.done)
					text_splitter_retry(&splitter);
			}

			fzbuf = fz_new_buffer(ctx, 0);

			tm.a = splitter.scale;
			tm.b = 0.0f;
			tm.c = 0.0f;
			tm.d = splitter.scale;
			tm.e = rect.x0;
			tm.f = rect.y1 - (1.0f+ascent-descent)*fontsize*splitter.scale/2.0f;

			fzbuf_print_text_start1(ctx, fzbuf, &rect, info->col);
			fzbuf_print_text_start2(ctx, fzbuf, &info->font_rec, &tm);

			fz_append_buffer(ctx, fzbuf, fztmp);

			fzbuf_print_text_end(ctx, fzbuf);
		}
		else if (info->comb)
		{
			int i, n = fz_mini((int)strlen(text), info->max_len);
			float comb_width = full_width/info->max_len;
			float char_width = pdf_text_stride(ctx, info->font_rec.font, fontsize, (unsigned char *)"M", 1, FLT_MAX, NULL);
			float init_skip = (comb_width - char_width)/2.0f;

			fz_translate(&tm, rect.x0, rect.y1 - (height+(ascent-descent)*fontsize)/2.0f);

			fzbuf = fz_new_buffer(ctx, 0);

			fzbuf_print_text_start1(ctx, fzbuf, &rect, info->col);
			fzbuf_print_text_start2(ctx, fzbuf, &info->font_rec, &tm);

			for (i = 0; i < n; i++)
				fzbuf_print_text_word(ctx, fzbuf, i == 0 ? init_skip : comb_width, 0.0f, text+i, 1);

			fzbuf_print_text_end(ctx, fzbuf);
		}
		else
		{
			if (oldtm)
			{
				tm = *oldtm;
			}
			else
			{
				fz_translate(&tm, rect.x0, rect.y1 - (height+(ascent-descent)*fontsize)/2.0f);

				switch (info->q)
				{
				case Q_Right: tm.e += width; break;
				case Q_Cent: tm.e += width/2; break;
				}
			}

			if (variable)
			{
				measure_text(ctx, doc, &info->font_rec, &tm, text, &tbox);

				if (tbox.x1 - tbox.x0 > width)
				{
					/* Scale the text to fit but use the same offset
					* to keep the baseline constant */
					tm.a *= width / (tbox.x1 - tbox.x0);
					tm.d *= width / (tbox.x1 - tbox.x0);
				}
			}

			fzbuf = create_aligned_text_buffer(ctx, doc, &rect, info, &tm, text);
		}
	}
	fz_always(ctx)
	{
		fz_drop_buffer(ctx, fztmp);
	}
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, fzbuf);
		fz_rethrow(ctx);
	}

	return fzbuf;
}

static int get_matrix(fz_context *ctx, pdf_document *doc, pdf_obj *form, int q, fz_matrix *mt)
{
	int found = 0;
	pdf_lexbuf lbuf;
	fz_stream *str;

	str = pdf_open_stream(ctx, form);

	pdf_lexbuf_init(ctx, &lbuf, PDF_LEXBUF_SMALL);

	fz_try(ctx)
	{
		int tok;
		float coefs[MATRIX_COEFS];
		int coef_i = 0;

		/* Look for the text matrix Tm in the stream */
		for (tok = pdf_lex(ctx, str, &lbuf); tok != PDF_TOK_EOF; tok = pdf_lex(ctx, str, &lbuf))
		{
			if (tok == PDF_TOK_INT || tok == PDF_TOK_REAL)
			{
				if (coef_i >= MATRIX_COEFS)
				{
					int i;
					for (i = 0; i < MATRIX_COEFS-1; i++)
						coefs[i] = coefs[i+1];

					coef_i = MATRIX_COEFS-1;
				}

				coefs[coef_i++] = tok == PDF_TOK_INT ? lbuf.i : lbuf.f;
			}
			else
			{
				if (tok == PDF_TOK_KEYWORD && !strcmp(lbuf.scratch, "Tm") && coef_i == MATRIX_COEFS)
				{
					found = 1;
					mt->a = coefs[0];
					mt->b = coefs[1];
					mt->c = coefs[2];
					mt->d = coefs[3];
					mt->e = coefs[4];
					mt->f = coefs[5];
				}

				coef_i = 0;
			}
		}

		if (found)
		{
			fz_rect bbox;
			pdf_to_rect(ctx, pdf_dict_get(ctx, form, PDF_NAME_BBox), &bbox);

			switch (q)
			{
			case Q_Left:
				mt->e = bbox.x0 + 1;
				break;

			case Q_Cent:
				mt->e = (bbox.x1 - bbox.x0) / 2;
				break;

			case Q_Right:
				mt->e = bbox.x1 - 1;
				break;
			}
		}
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, str);
		pdf_lexbuf_fin(ctx, &lbuf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return found;
}

static char *to_font_encoding(fz_context *ctx, pdf_font_desc *font, char *utf8)
{
	size_t i;
	int needs_converting = 0;

	/* Temporary partial solution. We are using a slow lookup in the conversion
	 * below, so we avoid performing the conversion unnecessarily. We check for
	 * top-bit-set chars, and convert only if they are present. We should also
	 * check that the font encoding is one that agrees with utf8 from 0 to 7f,
	 * but for now we get away without doing so. This is after all an improvement
	 * on just strdup */
	for (i = 0; utf8[i] != '\0'; i++)
	{
		if (utf8[i] & 0x80)
			needs_converting = 1;
	}

	/* Even if we need to convert, we cannot do so if the font has no cid_to_ucs mapping */
	if (needs_converting && font->cid_to_ucs)
	{
		char *buf = fz_malloc(ctx, strlen(utf8) + 1);
		char *bufp = buf;

		fz_try(ctx)
		{
			while(*utf8)
			{
				if (*utf8 & 0x80)
				{
					int rune;

					utf8 += fz_chartorune(&rune, utf8);

					/* Slow search for the cid that maps to the unicode value held in 'rune" */
					for (i = 0; i < font->cid_to_ucs_len && font->cid_to_ucs[i] != rune; i++)
						;

					/* If found store the cid */
					if (i < font->cid_to_ucs_len)
						*bufp++ = (char)i;
				}
				else
				{
					*bufp++ = *utf8++;
				}
			}

			*bufp = '\0';
		}
		fz_catch(ctx)
		{
			fz_free(ctx, buf);
			fz_rethrow(ctx);
		}

		return buf;
	}
	else
	{
		/* If either no conversion is needed or the font has no cid_to_ucs
		 * mapping then leave unconverted, although in the latter case the result
		 * is likely incorrect */
		return fz_strdup(ctx, utf8);
	}
}

static void account_for_rot(fz_rect *rect, fz_matrix *mat, int rot)
{
	float width = rect->x1;
	float height = rect->y1;

	switch (rot)
	{
	default:
		*mat = fz_identity;
		break;
	case 90:
		fz_pre_rotate(fz_translate(mat, width, 0), rot);
		rect->x1 = height;
		rect->y1 = width;
		break;
	case 180:
		fz_pre_rotate(fz_translate(mat, width, height), rot);
		break;
	case 270:
		fz_pre_rotate(fz_translate(mat, 0, height), rot);
		rect->x1 = height;
		rect->y1 = width;
		break;
	}
}

static void copy_resources(fz_context *ctx, pdf_obj *dst, pdf_obj *src)
{
	int i, len;

	len = pdf_dict_len(ctx, src);
	for (i = 0; i < len; i++)
	{
		pdf_obj *key = pdf_dict_get_key(ctx, src, i);

		if (!pdf_dict_get(ctx, dst, key))
			pdf_dict_put(ctx, dst, key, pdf_dict_get_val(ctx, src, i));
	}
}

static pdf_obj *load_or_create_form(fz_context *ctx, pdf_document *doc, pdf_obj *obj, fz_rect *rect)
{
	pdf_obj *ap = NULL;
	fz_matrix mat;
	int rot;
	pdf_obj *formobj = NULL;
	fz_buffer *fzbuf = NULL;
	int create_form = 0;

	fz_var(formobj);
	fz_var(fzbuf);
	fz_try(ctx)
	{
		rot = pdf_to_int(ctx, pdf_dict_getl(ctx, obj, PDF_NAME_MK, PDF_NAME_R, NULL));
		pdf_to_rect(ctx, pdf_dict_get(ctx, obj, PDF_NAME_Rect), rect);
		rect->x1 -= rect->x0;
		rect->y1 -= rect->y0;
		rect->x0 = rect->y0 = 0;
		account_for_rot(rect, &mat, rot);

		ap = pdf_dict_get(ctx, obj, PDF_NAME_AP);
		if (ap == NULL)
		{
			ap = pdf_new_dict(ctx, doc, 1);
			pdf_dict_put_drop(ctx, obj, PDF_NAME_AP, ap);
		}

		formobj = pdf_dict_get(ctx, ap, PDF_NAME_N);
		if (formobj == NULL)
		{
			formobj = pdf_new_xobject(ctx, doc, rect, &mat);
			pdf_dict_put_drop(ctx, ap, PDF_NAME_N, formobj);
			create_form = 1;
		}

		if (create_form)
		{
			fzbuf = fz_new_buffer(ctx, 1);
			pdf_update_stream(ctx, doc, formobj, fzbuf, 0);
		}

		copy_resources(ctx, pdf_xobject_resources(ctx, formobj), pdf_get_inheritable(ctx, doc, obj, PDF_NAME_DR));
	}
	fz_always(ctx)
	{
		fz_drop_buffer(ctx, fzbuf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return pdf_keep_obj(ctx, formobj);
}

static void update_marked_content(fz_context *ctx, pdf_document *doc, pdf_obj *form, fz_buffer *fzbuf)
{
	pdf_token tok;
	pdf_lexbuf lbuf;
	fz_stream *str_outer = NULL;
	fz_stream *str_inner = NULL;
	unsigned char *buf;
	size_t len;
	fz_buffer *newbuf = NULL;

	pdf_lexbuf_init(ctx, &lbuf, PDF_LEXBUF_SMALL);

	fz_var(str_outer);
	fz_var(str_inner);
	fz_var(newbuf);
	fz_try(ctx)
	{
		int bmc_found;
		int first = 1;

		newbuf = fz_new_buffer(ctx, 0);
		str_outer = pdf_open_stream(ctx, form);
		len = fz_buffer_storage(ctx, fzbuf, &buf);
		str_inner = fz_open_memory(ctx, buf, len);

		/* Copy the existing appearance stream to newbuf while looking for BMC */
		for (tok = pdf_lex(ctx, str_outer, &lbuf); tok != PDF_TOK_EOF; tok = pdf_lex(ctx, str_outer, &lbuf))
		{
			if (first)
				first = 0;
			else
				fz_append_printf(ctx, newbuf, " ");

			pdf_append_token(ctx, newbuf, tok, &lbuf);
			if (tok == PDF_TOK_KEYWORD && !strcmp(lbuf.scratch, "BMC"))
				break;
		}

		bmc_found = (tok != PDF_TOK_EOF);

		if (bmc_found)
		{
			/* Drop Tx BMC from the replacement appearance stream */
			(void)pdf_lex(ctx, str_inner, &lbuf);
			(void)pdf_lex(ctx, str_inner, &lbuf);
		}

		/* Copy the replacement appearance stream to newbuf */
		for (tok = pdf_lex(ctx, str_inner, &lbuf); tok != PDF_TOK_EOF; tok = pdf_lex(ctx, str_inner, &lbuf))
		{
			fz_append_printf(ctx, newbuf, " ");
			pdf_append_token(ctx, newbuf, tok, &lbuf);
		}

		if (bmc_found)
		{
			/* Drop the rest of the existing appearance stream until EMC found */
			for (tok = pdf_lex(ctx, str_outer, &lbuf); tok != PDF_TOK_EOF; tok = pdf_lex(ctx, str_outer, &lbuf))
			{
				if (tok == PDF_TOK_KEYWORD && !strcmp(lbuf.scratch, "EMC"))
					break;
			}

			/* Copy the rest of the existing appearance stream to newbuf */
			for (tok = pdf_lex(ctx, str_outer, &lbuf); tok != PDF_TOK_EOF; tok = pdf_lex(ctx, str_outer, &lbuf))
			{
				fz_append_printf(ctx, newbuf, " ");
				pdf_append_token(ctx, newbuf, tok, &lbuf);
			}
		}

		/* Use newbuf in place of the existing appearance stream */
		pdf_update_xobject_contents(ctx, doc, form, newbuf);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, str_outer);
		fz_drop_stream(ctx, str_inner);
		fz_drop_buffer(ctx, newbuf);
		pdf_lexbuf_fin(ctx, &lbuf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static int get_border_style(fz_context *ctx, pdf_obj *obj)
{
	pdf_obj *sname = pdf_dict_getl(ctx, obj, PDF_NAME_BS, PDF_NAME_S, NULL);

	if (pdf_name_eq(ctx, PDF_NAME_D, sname))
		return BS_Dashed;
	else if (pdf_name_eq(ctx, PDF_NAME_B, sname))
		return BS_Beveled;
	else if (pdf_name_eq(ctx, PDF_NAME_I, sname))
		return BS_Inset;
	else if (pdf_name_eq(ctx, PDF_NAME_U, sname))
		return BS_Underline;
	else
		return BS_Solid;
}

static float get_border_width(fz_context *ctx, pdf_obj *obj)
{
	float w = pdf_to_real(ctx, pdf_dict_getl(ctx, obj, PDF_NAME_BS, PDF_NAME_W, NULL));
	return w == 0.0f ? 1.0f : w;
}

void pdf_update_text_appearance(fz_context *ctx, pdf_document *doc, pdf_obj *obj, char *eventValue)
{
	text_widget_info info;
	fz_buffer *fzbuf = NULL;
	pdf_obj *form = NULL;
	fz_matrix tm;
	fz_rect rect, form_bbox;
	int has_tm;
	char *text = NULL;

	memset(&info, 0, sizeof(info));

	fz_var(info);
	fz_var(fzbuf);
	fz_var(form);
	fz_var(text);
	fz_try(ctx)
	{
		get_text_widget_info(ctx, doc, obj, &info);

		if (eventValue)
			text = to_font_encoding(ctx, info.font_rec.font, eventValue);
		else
			text = pdf_field_value(ctx, doc, obj);

		form = load_or_create_form(ctx, doc, obj, &rect);

		pdf_xobject_bbox(ctx, form, &form_bbox);
		has_tm = get_matrix(ctx, doc, form, info.q, &tm);
		fzbuf = create_text_appearance(ctx, doc, &form_bbox, has_tm ? &tm : NULL, &info, text?text:"");
		update_marked_content(ctx, doc, form, fzbuf);
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, form);
		fz_free(ctx, text);
		fz_drop_buffer(ctx, fzbuf);
		font_info_fin(ctx, &info.font_rec);
	}
	fz_catch(ctx)
	{
		fz_warn(ctx, "update_text_appearance failed");
	}
}

void pdf_update_listbox_appearance(fz_context *ctx, pdf_document *doc, pdf_obj *obj)
{
	text_widget_info info;
	pdf_obj *form = NULL;
	fz_buffer *fzbuf = NULL;
	fz_matrix tm;
	fz_rect clip_rect;
	fz_rect fill_rect;
	pdf_obj *valarr;
	pdf_obj *optarr;
	char *text;
	int n, m, i, j;
	char **opts = NULL;
	char **vals = NULL;
	int *sel_indices = NULL;
	char **pos;
	int index = -1;
	float height, width;
	float ascent, descent, lineheight;
	int variable;
	int fontsize;
	float items_height;
	fz_rect bbox;
	int num_sel = 0;
	int found_count;
	int val_opt_ok = 1;
	float color[4];

	memset(&info, 0, sizeof(info));

	fz_var(info);
	fz_var(form);
	fz_var(fzbuf);
	fz_var(opts);
	fz_var(vals);
	fz_var(sel_indices);
	fz_try(ctx)
	{
		optarr = pdf_dict_get(ctx, obj, PDF_NAME_Opt);
		n = pdf_array_len(ctx, optarr);
		opts = (char **)fz_malloc(ctx, n * sizeof(*opts));
		vals = (char **)fz_malloc(ctx, n * sizeof(*vals));
		sel_indices = (int*)fz_malloc(ctx, n * sizeof(int));
		for (i = 0; i < n; i++)
		{
			m = pdf_array_len(ctx, pdf_array_get(ctx, optarr, i));
			if (m == 2)
			{
				vals[i] = pdf_to_str_buf(ctx, pdf_array_get(ctx, pdf_array_get(ctx, optarr, i), 0));
				opts[i] = pdf_to_str_buf(ctx, pdf_array_get(ctx, pdf_array_get(ctx, optarr, i), 1));
			}
			else
			{
				opts[i] = pdf_to_str_buf(ctx, pdf_array_get(ctx, optarr, i));
				val_opt_ok = 0;
			}
		}

		/* If ANY of the entries are not an array then just use the opts not the vals. */
		if (val_opt_ok)
			pos = vals;
		else
			pos = opts;

		get_text_widget_info(ctx, doc, obj, &info);
		form = load_or_create_form(ctx, doc, obj, &clip_rect);

		/* See which ones are selected */
		valarr = pdf_get_inheritable(ctx, doc, obj, PDF_NAME_V);
		if (pdf_is_array(ctx, valarr))
		{
			num_sel = pdf_array_len(ctx, valarr);
			/* Multiple selected */
			found_count = 0;
			for (i = 0; i < num_sel; i++)
			{
				text = pdf_to_str_buf(ctx, pdf_array_get(ctx, valarr, i));
				/* See if we can find it. */
				index = -1;
				for (j = 0; j < n; j++)
				{
					if (!strcmp(text, pos[j]))
					{
						index = j;
						break;
					}
				}
				if (index > -1)
				{
					sel_indices[found_count] = index;
					found_count += 1;
				}
			}
			num_sel = found_count;
		}
		else
		{
			/* One or none selected */
			text = pdf_to_str_buf(ctx, valarr);
			num_sel = 0;
			if (text)
			{
				index = -1;
				for (i = 0; i < n; i++)
				{
					if (!strcmp(text, pos[i]))
					{
						index = i;
						break;
					}
				}
				if (index > -1)
				{
					num_sel = 1;
					sel_indices[0] = index;
				}
			}
		}

		if (clip_rect.x1 - clip_rect.x0 > 3.0f && clip_rect.y1 - clip_rect.y0 > 3.0f)
		{
			clip_rect.x0 += 1.0f;
			clip_rect.x1 -= 1.0f;
			clip_rect.y0 += 1.0f;
			clip_rect.y1 -= 1.0f;
		}
		height = clip_rect.y1 - clip_rect.y0;
		width = clip_rect.x1 - clip_rect.x0;
		variable = (info.font_rec.da_rec.font_size == 0);
		fontsize = variable
			? (info.multiline ? 14.0f : height / info.font_rec.lineheight)
			: info.font_rec.da_rec.font_size;

		/* Get the ascent, descent across the items list. */
		ascent = 0;
		descent = 0;
		info.font_rec.da_rec.font_size = 1;
		for (i = 0; i < n; i++)
		{
			measure_text(ctx, doc, &(info.font_rec), &fz_identity, opts[i], &bbox);
			descent = fz_min(-bbox.y0, descent);
			ascent = fz_max(bbox.y1, ascent);
		}
		info.font_rec.da_rec.font_size = fontsize;
		lineheight = ascent - descent;

		/* Check if all items will fit. If not, then place the "selected" item at the top of our widget rect. */
		items_height = n * fontsize * lineheight;
		if (items_height <= height || !num_sel)
			fz_translate(&tm, clip_rect.x0, clip_rect.y1 - lineheight * fontsize);
		else
			fz_translate(&tm, clip_rect.x0, clip_rect.y1 + (sel_indices[0] - 1) * lineheight * fontsize);

		fzbuf = fz_new_buffer(ctx, 0);
		/* Start the marked content */
		fzbuf_print_text_start1(ctx, fzbuf, &clip_rect, NULL);
		/* Add the selection rects */
		if (num_sel > 0)
		{
			color[0] = LIST_SEL_COLOR_R;
			color[1] = LIST_SEL_COLOR_G;
			color[2] = LIST_SEL_COLOR_B;
			fill_rect.x0 = 0.0f;
			fill_rect.x1 = width;
			for (i = 0; i < num_sel; i++)
			{
				fill_rect.y0 = height - fontsize * lineheight * (sel_indices[i] + 1);
				fill_rect.y1 = fill_rect.y0 + lineheight * fontsize;
				fzbuf_print_rect_fill(ctx, fzbuf, NULL, color, 3, NULL, &fill_rect);
			}
		}
		/* Continue with text related details */
		fzbuf_print_text_start2(ctx, fzbuf, &info.font_rec, &tm);
		/* And now finish with the text content */
		for (i = 0; i < n; i++)
		{
			fzbuf_print_text_word(ctx, fzbuf, 0.0f, i == 0 ? 0 : -fontsize *
				lineheight, opts[i], (int)strlen(opts[i]));
		}
		fzbuf_print_text_end(ctx, fzbuf);
		update_marked_content(ctx, doc, form, fzbuf);
	}
	fz_always(ctx)
	{
		fz_free(ctx, opts);
		fz_free(ctx, vals);
		fz_free(ctx, sel_indices);
		pdf_drop_obj(ctx, form);
		fz_drop_buffer(ctx, fzbuf);
		font_info_fin(ctx, &info.font_rec);
	}
	fz_catch(ctx)
	{
		fz_warn(ctx, "update_text_appearance failed");
	}
}

void pdf_update_combobox_appearance(fz_context *ctx, pdf_document *doc, pdf_obj *obj)
{
	text_widget_info info;
	pdf_obj *form = NULL;
	fz_buffer *fzbuf = NULL;
	fz_matrix tm;
	fz_rect rect, form_bbox;
	int has_tm;
	pdf_obj *val;
	char *text;

	memset(&info, 0, sizeof(info));

	fz_var(info);
	fz_var(form);
	fz_var(fzbuf);
	fz_try(ctx)
	{
		get_text_widget_info(ctx, doc, obj, &info);

		val = pdf_get_inheritable(ctx, doc, obj, PDF_NAME_V);

		if (pdf_is_array(ctx, val))
			val = pdf_array_get(ctx, val, 0);

		text = pdf_to_str_buf(ctx, val);

		if (!text)
			text = "";

		form = load_or_create_form(ctx, doc, obj, &rect);

		pdf_xobject_bbox(ctx, form, &form_bbox);
		has_tm = get_matrix(ctx, doc, form, info.q, &tm);
		fzbuf = create_text_appearance(ctx, doc, &form_bbox, has_tm ? &tm : NULL, &info,
			text?text:"");
		update_marked_content(ctx, doc, form, fzbuf);
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, form);
		fz_drop_buffer(ctx, fzbuf);
		font_info_fin(ctx, &info.font_rec);
	}
	fz_catch(ctx)
	{
		fz_warn(ctx, "update_text_appearance failed");
	}
}

void pdf_update_pushbutton_appearance(fz_context *ctx, pdf_document *doc, pdf_obj *obj)
{
	fz_rect rect = fz_empty_rect;
	pdf_obj *form = NULL;
	fz_buffer *fzbuf = NULL;
	pdf_obj *tobj = NULL;
	font_info font_rec;
	int bstyle;
	float bwidth;
	float btotal;

	memset(&font_rec, 0, sizeof(font_rec));

	fz_var(font_rec);
	fz_var(form);
	fz_var(fzbuf);
	fz_try(ctx)
	{
		form = load_or_create_form(ctx, doc, obj, &rect);
		fzbuf = fz_new_buffer(ctx, 0);
		tobj = pdf_dict_getl(ctx, obj, PDF_NAME_MK, PDF_NAME_BG, NULL);
		if (pdf_is_array(ctx, tobj))
		{
			fzbuf_print_color(ctx, fzbuf, tobj, 0, 0.0f);
			fz_append_printf(ctx, fzbuf, fmt_re,
				rect.x0, rect.y0, rect.x1, rect.y1);
			fz_append_printf(ctx, fzbuf, fmt_f);
		}
		bstyle = get_border_style(ctx, obj);
		bwidth = get_border_width(ctx, obj);
		btotal = bwidth;
		if (bstyle == BS_Beveled || bstyle == BS_Inset)
		{
			btotal += bwidth;

			if (bstyle == BS_Beveled)
				fz_append_printf(ctx, fzbuf, fmt_g, 1.0f);
			else
				fz_append_printf(ctx, fzbuf, fmt_g, 0.33f);
			fz_append_printf(ctx, fzbuf, fmt_m, bwidth, bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, bwidth, rect.y1 - bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, rect.x1 - bwidth, rect.y1 - bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, rect.x1 - 2 * bwidth, rect.y1 - 2 * bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, 2 * bwidth, rect.y1 - 2 * bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, 2 * bwidth, 2 * bwidth);
			fz_append_printf(ctx, fzbuf, fmt_f);
			if (bstyle == BS_Beveled)
				fzbuf_print_color(ctx, fzbuf, tobj, 0, -0.25f);
			else
				fz_append_printf(ctx, fzbuf, fmt_g, 0.66f);
			fz_append_printf(ctx, fzbuf, fmt_m, rect.x1 - bwidth, rect.y1 - bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, rect.x1 - bwidth, bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, bwidth, bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, 2 * bwidth, 2 * bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, rect.x1 - 2 * bwidth, 2 * bwidth);
			fz_append_printf(ctx, fzbuf, fmt_l, rect.x1 - 2 * bwidth, rect.y1 - 2 * bwidth);
			fz_append_printf(ctx, fzbuf, fmt_f);
		}

		tobj = pdf_dict_getl(ctx, obj, PDF_NAME_MK, PDF_NAME_BC, NULL);
		if (tobj)
		{
			fzbuf_print_color(ctx, fzbuf, tobj, 1, 0.0f);
			fz_append_printf(ctx, fzbuf, fmt_w, bwidth);
			fz_append_printf(ctx, fzbuf, fmt_re,
				bwidth/2, bwidth/2,
				rect.x1 -bwidth/2, rect.y1 - bwidth/2);
			fz_append_printf(ctx, fzbuf, fmt_s);
		}

		tobj = pdf_dict_getl(ctx, obj, PDF_NAME_MK, PDF_NAME_CA, NULL);
		if (tobj)
		{
			fz_rect clip = rect;
			fz_rect bounds;
			fz_matrix mat;
			char *da = pdf_to_str_buf(ctx, pdf_get_inheritable(ctx, doc, obj, PDF_NAME_DA));
			char *text = pdf_to_str_buf(ctx, tobj);

			clip.x0 += btotal;
			clip.y0 += btotal;
			clip.x1 -= btotal;
			clip.y1 -= btotal;

			get_font_info(ctx, doc, pdf_xobject_resources(ctx, form), da, &font_rec);
			measure_text(ctx, doc, &font_rec, &fz_identity, text, &bounds);
			fz_translate(&mat, (rect.x1 - bounds.x1)/2, (rect.y1 - bounds.y1)/2);
			fzbuf_print_text(ctx, fzbuf, &clip, NULL, &font_rec, &mat, text);
		}

		pdf_update_xobject_contents(ctx, doc, form, fzbuf);
	}
	fz_always(ctx)
	{
		font_info_fin(ctx, &font_rec);
		fz_drop_buffer(ctx, fzbuf);
		pdf_drop_obj(ctx, form);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

void pdf_update_text_markup_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot, enum pdf_annot_type type)
{
	float color[3];
	float alpha;
	float line_height;
	float line_thickness;
	pdf_obj *annotcolor;

	switch (type)
	{
		case PDF_ANNOT_HIGHLIGHT:
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 0.0f;
			alpha = 0.5f;
			line_thickness = 1.0f;
			line_height = 0.5f;
			break;
		case PDF_ANNOT_UNDERLINE:
			color[0] = 0.0f;
			color[1] = 0.0f;
			color[2] = 1.0f;
			alpha = 1.0f;
			line_thickness = LINE_THICKNESS;
			line_height = UNDERLINE_HEIGHT;
			break;
		case PDF_ANNOT_STRIKE_OUT:
			color[0] = 1.0f;
			color[1] = 0.0f;
			color[2] = 0.0f;
			alpha = 1.0f;
			line_thickness = LINE_THICKNESS;
			line_height = STRIKE_HEIGHT;
			break;
		default:
			return;
	}

	annotcolor = pdf_dict_get(ctx, annot->obj, PDF_NAME_C);

	if (pdf_is_array(ctx, annotcolor))
	{
		color[0] = pdf_to_int(ctx, pdf_array_get(ctx, annotcolor, 0));
		color[1] = pdf_to_int(ctx, pdf_array_get(ctx, annotcolor, 1));
		color[2] = pdf_to_int(ctx, pdf_array_get(ctx, annotcolor, 2));
	}

	pdf_set_markup_appearance(ctx, doc, annot, color, alpha, line_thickness, line_height);
}

void pdf_set_annot_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot, fz_rect *rect, fz_display_list *disp_list)
{
	pdf_obj *obj = annot->obj;
	fz_device *dev = NULL;
	fz_matrix page_ctm, inv_page_ctm;

	pdf_obj *resources;
	fz_buffer *contents;

	pdf_obj *ap_obj;
	fz_rect trect = *rect;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);
	fz_invert_matrix(&inv_page_ctm, &page_ctm);

	fz_transform_rect(&trect, &inv_page_ctm);

	pdf_dict_put_drop(ctx, obj, PDF_NAME_Rect, pdf_new_rect(ctx, doc, &trect));

	/* See if there is a current normal appearance */
	ap_obj = pdf_dict_getl(ctx, obj, PDF_NAME_AP, PDF_NAME_N, NULL);
	if (!pdf_is_stream(ctx, ap_obj))
		ap_obj = NULL;

	if (ap_obj == NULL)
	{
		ap_obj = pdf_new_xobject(ctx, doc, &trect, &fz_identity);
		pdf_dict_putl_drop(ctx, obj, ap_obj, PDF_NAME_AP, PDF_NAME_N, NULL);
	}
	else
	{
		pdf_xref_ensure_incremental_object(ctx, doc, pdf_to_num(ctx, ap_obj));
		/* Update bounding box and matrix in reused xobject obj */
		pdf_dict_put_drop(ctx, ap_obj, PDF_NAME_BBox, pdf_new_rect(ctx, doc, &trect));
		pdf_dict_put_drop(ctx, ap_obj, PDF_NAME_Matrix, pdf_new_matrix(ctx, doc, &fz_identity));
	}

	resources = pdf_dict_get(ctx, ap_obj, PDF_NAME_Resources);

	contents = fz_new_buffer(ctx, 0);

	fz_var(dev);
	fz_try(ctx)
	{
		dev = pdf_new_pdf_device(ctx, doc, &fz_identity, &trect, resources, contents);
		fz_run_display_list(ctx, disp_list, dev, &inv_page_ctm, &fz_infinite_rect, NULL);
		fz_close_device(ctx, dev);
		pdf_update_stream(ctx, doc, ap_obj, contents, 0);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
		fz_drop_buffer(ctx, contents);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);

	/* Mark the appearance as changed - required for partial update */
	annot->has_new_ap = 1;
	annot->needs_new_ap = 0;
}

static fz_point *
quadpoints(fz_context *ctx, pdf_document *doc, pdf_obj *annot, int *nout)
{
	pdf_obj *quad = pdf_dict_get(ctx, annot, PDF_NAME_QuadPoints);
	fz_point *qp = NULL;
	int i, n;

	if (!quad)
		return NULL;

	n = pdf_array_len(ctx, quad);

	if (n%8 != 0)
		return NULL;

	fz_var(qp);
	fz_try(ctx)
	{
		qp = fz_malloc_array(ctx, n/2, sizeof(fz_point));

		for (i = 0; i < n; i += 2)
		{
			qp[i/2].x = pdf_to_real(ctx, pdf_array_get(ctx, quad, i));
			qp[i/2].y = pdf_to_real(ctx, pdf_array_get(ctx, quad, i+1));
		}
	}
	fz_catch(ctx)
	{
		fz_free(ctx, qp);
		fz_rethrow(ctx);
	}

	*nout = n/2;

	return qp;
}

void pdf_set_markup_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot, float color[3], float alpha, float line_thickness, float line_height)
{
	fz_path *path = NULL;
	fz_stroke_state *stroke = NULL;
	fz_device *dev = NULL;
	fz_display_list *strike_list = NULL;
	int i, n;
	fz_point *qp = quadpoints(ctx, doc, annot->obj, &n);
	fz_matrix page_ctm;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	if (!qp || n <= 0)
		return;

	fz_var(path);
	fz_var(stroke);
	fz_var(dev);
	fz_var(strike_list);
	fz_try(ctx)
	{
		fz_rect rect = fz_empty_rect;

		rect.x0 = rect.x1 = qp[0].x;
		rect.y0 = rect.y1 = qp[0].y;
		for (i = 0; i < n; i++)
			fz_include_point_in_rect(&rect, &qp[i]);

		strike_list = fz_new_display_list(ctx, NULL);
		dev = fz_new_list_device(ctx, strike_list);

		for (i = 0; i < n; i += 4)
		{
			/* Contrary to the specification, the points within a QuadPoint are NOT ordered
			 * in a counterclockwise fashion. Experiments with Adobe's implementation
			 * indicates a cross-wise ordering is intended: ll, lr, ul, ur. */
			fz_point ll = qp[i];
			fz_point lr = qp[i+1];
			fz_point up;
			float thickness;

			up.x = qp[i+2].x - qp[i].x; /* ul - ll vector */
			up.y = qp[i+2].y - qp[i].y;

			ll.x += line_height * up.x;
			ll.y += line_height * up.y;
			lr.x += line_height * up.x;
			lr.y += line_height * up.y;

			thickness = sqrtf(up.x * up.x + up.y * up.y) * line_thickness;

			if (!stroke || fz_abs(stroke->linewidth - thickness) < SMALL_FLOAT)
			{
				if (stroke)
				{
					// assert(path)
					fz_stroke_path(ctx, dev, path, stroke, &page_ctm, fz_device_rgb(ctx), color, alpha, NULL);
					fz_drop_stroke_state(ctx, stroke);
					stroke = NULL;
					fz_drop_path(ctx, path);
					path = NULL;
				}

				stroke = fz_new_stroke_state(ctx);
				stroke->linewidth = thickness;
				path = fz_new_path(ctx);
			}

			fz_moveto(ctx, path, ll.x, ll.y);
			fz_lineto(ctx, path, lr.x, lr.y);
		}

		if (stroke)
		{
			fz_stroke_path(ctx, dev, path, stroke, &page_ctm, fz_device_rgb(ctx), color, alpha, NULL);
		}

		fz_close_device(ctx, dev);

		fz_transform_rect(&rect, &page_ctm);
		pdf_set_annot_appearance(ctx, doc, annot, &rect, strike_list);
	}
	fz_always(ctx)
	{
		fz_free(ctx, qp);
		fz_drop_device(ctx, dev);
		fz_drop_stroke_state(ctx, stroke);
		fz_drop_path(ctx, path);
		fz_drop_display_list(ctx, strike_list);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

/* Returns borrowed colorspace reference */
static fz_colorspace *pdf_to_color(fz_context *ctx, pdf_document *doc, pdf_obj *col, float color[4])
{
	fz_colorspace *cs;
	int i, ncol = pdf_array_len(ctx, col);

	switch (ncol)
	{
	case 1: cs = fz_device_gray(ctx); break;
	case 3: cs = fz_device_rgb(ctx); break;
	case 4: cs = fz_device_cmyk(ctx); break;
	default: return NULL;
	}

	for (i = 0; i < ncol; i++)
		color[i] = pdf_to_real(ctx, pdf_array_get(ctx, col, i));

	return cs;
}

void pdf_update_ink_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot)
{
	fz_path *path = NULL;
	fz_stroke_state *stroke = NULL;
	fz_device *dev = NULL;
	fz_display_list *strike_list = NULL;
	fz_colorspace *cs = NULL;
	fz_matrix page_ctm;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	fz_var(path);
	fz_var(stroke);
	fz_var(dev);
	fz_var(strike_list);
	fz_var(cs);
	fz_try(ctx)
	{
		fz_rect rect = fz_empty_rect;
		float color[4];
		float width;
		pdf_obj *list;
		int n, m, i, j;
		int empty = 1;

		width = pdf_annot_border(ctx, annot);
		if (width == 0.0f)
			width = 1.0f;

		list = pdf_dict_get(ctx, annot->obj, PDF_NAME_InkList);

		n = pdf_array_len(ctx, list);

		strike_list = fz_new_display_list(ctx, NULL);
		dev = fz_new_list_device(ctx, strike_list);
		path = fz_new_path(ctx);
		stroke = fz_new_stroke_state(ctx);
		stroke->linewidth = width;
		stroke->start_cap = stroke->end_cap = FZ_LINECAP_ROUND;
		stroke->linejoin = FZ_LINEJOIN_ROUND;

		for (i = 0; i < n; i ++)
		{
			fz_point pt_last;
			pdf_obj *arc = pdf_array_get(ctx, list, i);
			m = pdf_array_len(ctx, arc);

			for (j = 0; j < m-1; j += 2)
			{
				fz_point pt;
				pt.x = pdf_to_real(ctx, pdf_array_get(ctx, arc, j));
				pt.y = pdf_to_real(ctx, pdf_array_get(ctx, arc, j+1));

				if (i == 0 && j == 0)
				{
					rect.x0 = rect.x1 = pt.x;
					rect.y0 = rect.y1 = pt.y;
					empty = 0;
				}
				else
				{
					fz_include_point_in_rect(&rect, &pt);
				}

				if (j == 0)
					fz_moveto(ctx, path, pt.x, pt.y);
				else
					fz_curvetov(ctx, path, pt_last.x, pt_last.y, (pt.x + pt_last.x) / 2, (pt.y + pt_last.y) / 2);
				pt_last = pt;
			}
			fz_lineto(ctx, path, pt_last.x, pt_last.y);
		}

		cs = pdf_to_color(ctx, doc, pdf_dict_get(ctx, annot->obj, PDF_NAME_C), color);
		fz_stroke_path(ctx, dev, path, stroke, &page_ctm, cs, color, 1.0f, NULL);

		fz_expand_rect(&rect, width);
		/*
			Expand the rectangle by width all around. We cannot use
			fz_expand_rect because the rectangle might be empty in the
			single point case
		*/
		if (!empty)
		{
			rect.x0 -= width;
			rect.y0 -= width;
			rect.x1 += width;
			rect.y1 += width;
		}

		fz_close_device(ctx, dev);

		fz_transform_rect(&rect, &page_ctm);
		pdf_set_annot_appearance(ctx, doc, annot, &rect, strike_list);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
		fz_drop_stroke_state(ctx, stroke);
		fz_drop_path(ctx, path);
		fz_drop_display_list(ctx, strike_list);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void add_text(fz_context *ctx, font_info *font_rec, fz_text *text, const char *str, size_t str_len, const fz_matrix *tm_)
{
	fz_font *font = font_rec->font->font;
	fz_matrix tm = *tm_;
	int ucs, gid, n;

	while (str_len > 0)
	{
		n = fz_chartorune(&ucs, str);
		str += n;
		str_len -= n;
		gid = fz_encode_character(ctx, font, ucs);
		fz_show_glyph(ctx, text, font, &tm, gid, ucs, 0, 0, FZ_BIDI_NEUTRAL, FZ_LANG_UNSET);
		tm.e += fz_advance_glyph(ctx, font, gid, 0) * font_rec->da_rec.font_size;
	}
}

static fz_text *layout_text(fz_context *ctx, font_info *font_rec, char *str, float x, float y)
{
	fz_text *text;
	fz_matrix tm;

	fz_scale(&tm, font_rec->da_rec.font_size, font_rec->da_rec.font_size);
	tm.e = x;
	tm.f = y;

	text = fz_new_text(ctx);

	fz_try(ctx)
	{
		add_text(ctx, font_rec, text, str, (int)strlen(str), &tm);
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, text);
		fz_rethrow(ctx);
	}

	return text;
}

static fz_text *fit_text(fz_context *ctx, font_info *font_rec, const char *str, fz_rect *bounds)
{
	float width = bounds->x1 - bounds->x0;
	float height = bounds->y1 - bounds->y0;
	fz_matrix tm;
	fz_text *text = NULL;
	fz_text_span *span;
	text_splitter splitter;
	float ascender;

	/* Initially aim for one-line of text */
	font_rec->da_rec.font_size = height / font_rec->lineheight;

	text_splitter_init(&splitter, font_rec, str, width, height, 1);

	fz_var(text);
	fz_try(ctx)
	{
		int i;
		while (!splitter.done)
		{
			/* Try a layout pass */
			int line = 0;
			float font_size;

			fz_drop_text(ctx, text);
			text = NULL;
			font_size = font_rec->da_rec.font_size;
			fz_scale(&tm, font_size, font_size);
			tm.e = 0;
			tm.f = 0;

			text = fz_new_text(ctx);

			text_splitter_start_pass(&splitter);

			/* Layout unscaled text to a scaled-up width, so that
			* the scaled-down text will fit the unscaled width */

			while (!splitter.done && line < splitter.max_lines)
			{
				/* Layout a line */
				text_splitter_start_line(&splitter);

				while (!splitter.done && text_splitter_layout(ctx, &splitter))
				{
					if (splitter.text[splitter.text_start] != ' ')
					{
						float dx, dy;
						const char *word = str+splitter.text_start;
						size_t wordlen = splitter.text_end-splitter.text_start;

						text_splitter_move(&splitter, -line, &dx, &dy);
						tm.e += dx;
						tm.f += dy;
						add_text(ctx, font_rec, text, word, wordlen, &tm);
					}
				}

				line ++;
			}

			if (!splitter.done)
				text_splitter_retry(&splitter);
		}

		/* Post process text with the scale determined by the splitter
		 * and with the required offset */
		for (span = text->head; span; span = span->next)
		{
			fz_pre_scale(&span->trm, splitter.scale, splitter.scale);
			ascender = font_rec->font->ascent * font_rec->da_rec.font_size * splitter.scale / 1000.0f;
			for (i = 0; i < span->len; i++)
			{
				span->items[i].x = span->items[i].x * splitter.scale + bounds->x0;
				span->items[i].y = span->items[i].y * splitter.scale + bounds->y1 - ascender;
			}
		}
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, text);
		fz_rethrow(ctx);
	}

	return text;
}

static void rect_center(const fz_rect *rect, fz_point *c)
{
	c->x = (rect->x0 + rect->x1) / 2.0f;
	c->y = (rect->y0 + rect->y1) / 2.0f;
}

static void center_rect_within_rect(const fz_rect *tofit, const fz_rect *within, fz_matrix *mat)
{
	float xscale = (within->x1 - within->x0) / (tofit->x1 - tofit->x0);
	float yscale = (within->y1 - within->y0) / (tofit->y1 - tofit->y0);
	float scale = fz_min(xscale, yscale);
	fz_point tofit_center;
	fz_point within_center;

	rect_center(within, &within_center);
	rect_center(tofit, &tofit_center);

	/* Translate "tofit" to be centered on the origin
	 * Scale "tofit" to a size that fits within "within"
	 * Translate "tofit" to "within's" center
	 * Do all the above in reverse order so that we can use the fz_pre_xx functions */
	fz_translate(mat, within_center.x, within_center.y);
	fz_pre_scale(mat, scale, scale);
	fz_pre_translate(mat, -tofit_center.x, -tofit_center.y);
}

static const float outline_thickness = 15.0f;

static void draw_rounded_rect(fz_context *ctx, fz_path *path)
{
	fz_moveto(ctx, path, 20.0f, 60.0f);
	fz_curveto(ctx, path, 20.0f, 30.0f, 30.0f, 20.0f, 60.0f, 20.0f);
	fz_lineto(ctx, path, 340.0f, 20.0f);
	fz_curveto(ctx, path, 370.0f, 20.0f, 380.0f, 30.0f, 380.0f, 60.0f);
	fz_lineto(ctx, path, 380.0f, 340.0f);
	fz_curveto(ctx, path, 380.0f, 370.0f, 370.0f, 380.0f, 340.0f, 380.0f);
	fz_lineto(ctx, path, 60.0f, 380.0f);
	fz_curveto(ctx, path, 30.0f, 380.0f, 20.0f, 370.0f, 20.0f, 340.0f);
	fz_closepath(ctx, path);
}

static void draw_speech_bubble(fz_context *ctx, fz_path *path)
{
	fz_moveto(ctx, path, 199.0f, 315.6f);
	fz_curveto(ctx, path, 35.6f, 315.6f, 27.0f, 160.8f, 130.2f, 131.77f);
	fz_curveto(ctx, path, 130.2f, 93.07f, 113.0f, 83.4f, 113.0f, 83.4f);
	fz_curveto(ctx, path, 138.8f, 73.72f, 173.2f, 83.4f, 190.4f, 122.1f);
	fz_curveto(ctx, path, 391.64f, 122.1f, 362.4f, 315.6f, 199.0f, 315.6f);
	fz_closepath(ctx, path);
}

void pdf_update_text_annot_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot)
{
	static float white[3] = {1.0f, 1.0f, 1.0f};
	static float yellow[3] = {1.0f, 1.0f, 0.0f};
	static float black[3] = {0.0f, 0.0f, 0.0f};

	fz_display_list *dlist = NULL;
	fz_device *dev = NULL;
	fz_colorspace *cs = NULL;
	fz_path *path = NULL;
	fz_stroke_state *stroke = NULL;
	fz_matrix page_ctm;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	fz_var(path);
	fz_var(stroke);
	fz_var(dlist);
	fz_var(dev);
	fz_var(cs);
	fz_try(ctx)
	{
		fz_rect rect;
		fz_rect bounds;
		fz_matrix tm;

		pdf_to_rect(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Rect), &rect);
		dlist = fz_new_display_list(ctx, NULL);
		dev = fz_new_list_device(ctx, dlist);
		stroke = fz_new_stroke_state(ctx);
		stroke->linewidth = outline_thickness;
		stroke->linejoin = FZ_LINEJOIN_ROUND;

		path = fz_new_path(ctx);
		draw_rounded_rect(ctx, path);
		fz_bound_path(ctx, path, NULL, &fz_identity, &bounds);
		fz_expand_rect(&bounds, outline_thickness);
		center_rect_within_rect(&bounds, &rect, &tm);
		fz_concat(&tm, &tm, &page_ctm);
		cs = fz_device_rgb(ctx); /* Borrowed reference */
		fz_fill_path(ctx, dev, path, 0, &tm, cs, yellow, 1.0f, NULL);
		fz_stroke_path(ctx, dev, path, stroke, &tm, cs, black, 1.0f, NULL);
		fz_drop_path(ctx, path);
		path = NULL;

		path = fz_new_path(ctx);
		draw_speech_bubble(ctx, path);
		fz_fill_path(ctx, dev, path, 0, &tm, cs, white, 1.0f, NULL);
		fz_stroke_path(ctx, dev, path, stroke, &tm, cs, black, 1.0f, NULL);

		fz_close_device(ctx, dev);

		fz_transform_rect(&rect, &page_ctm);
		pdf_set_annot_appearance(ctx, doc, annot, &rect, dlist);

		/* Drop the cached xobject from the annotation structure to
		 * force a redraw on next pdf_update_page call */
		pdf_drop_obj(ctx, annot->ap);
		annot->ap = NULL;
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
		fz_drop_display_list(ctx, dlist);
		fz_drop_stroke_state(ctx, stroke);
		fz_drop_path(ctx, path);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

void pdf_update_free_text_annot_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot)
{
	pdf_obj *obj = annot->obj;
	pdf_obj *dr = pdf_dict_get(ctx, annot->page->obj, PDF_NAME_Resources);
	fz_display_list *dlist = NULL;
	fz_device *dev = NULL;
	font_info font_rec;
	fz_text *text = NULL;
	fz_matrix page_ctm;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	memset(&font_rec, 0, sizeof(font_rec));

	/* Set some sane defaults in case the parsing of the font_rec fails */
	font_rec.da_rec.col_size = 1; /* Default to greyscale */
	font_rec.da_rec.font_size = 12; /* Default to 12 point */

	fz_var(dlist);
	fz_var(dev);
	fz_var(text);
	fz_try(ctx)
	{
		char *contents = pdf_to_str_buf(ctx, pdf_dict_get(ctx, obj, PDF_NAME_Contents));
		char *da = pdf_to_str_buf(ctx, pdf_dict_get(ctx, obj, PDF_NAME_DA));
		fz_colorspace *cs;
		fz_point pos;
		fz_rect rect;

		pdf_to_rect(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Rect), &rect);

		get_font_info(ctx, doc, dr, da, &font_rec);

		switch (font_rec.da_rec.col_size)
		{
		default: cs = fz_device_gray(ctx); break;
		case 3: cs = fz_device_rgb(ctx); break;
		case 4: cs = fz_device_cmyk(ctx); break;
		}

		/* Adjust for the descender */
		pos.x = rect.x0;
		pos.y = rect.y0 - font_rec.font->descent * font_rec.da_rec.font_size / 1000.0f;

		text = layout_text(ctx, &font_rec, contents, pos.x, pos.y);

		dlist = fz_new_display_list(ctx, NULL);
		dev = fz_new_list_device(ctx, dlist);
		fz_fill_text(ctx, dev, text, &page_ctm, cs, font_rec.da_rec.col, 1.0f, NULL);
		fz_close_device(ctx, dev);

		fz_transform_rect(&rect, &page_ctm);
		pdf_set_annot_appearance(ctx, doc, annot, &rect, dlist);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
		fz_drop_display_list(ctx, dlist);
		font_info_fin(ctx, &font_rec);
		fz_drop_text(ctx, text);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void draw_logo(fz_context *ctx, fz_path *path)
{
	fz_moveto(ctx, path, 122.25f, 0.0f);
	fz_lineto(ctx, path, 122.25f, 14.249f);
	fz_curveto(ctx, path, 125.98f, 13.842f, 129.73f, 13.518f, 133.5f, 13.277f);
	fz_lineto(ctx, path, 133.5f, 0.0f);
	fz_lineto(ctx, path, 122.25f, 0.0f);
	fz_closepath(ctx, path);
	fz_moveto(ctx, path, 140.251f, 0.0f);
	fz_lineto(ctx, path, 140.251f, 12.935f);
	fz_curveto(ctx, path, 152.534f, 12.477f, 165.03f, 12.899f, 177.75f, 14.249f);
	fz_lineto(ctx, path, 177.75f, 21.749f);
	fz_curveto(ctx, path, 165.304f, 20.413f, 152.809f, 19.871f, 140.251f, 20.348f);
	fz_lineto(ctx, path, 140.251f, 39.0f);
	fz_lineto(ctx, path, 133.5f, 39.0f);
	fz_lineto(ctx, path, 133.5f, 20.704f);
	fz_curveto(ctx, path, 129.756f, 20.956f, 126.006f, 21.302f, 122.25f, 21.749f);
	fz_lineto(ctx, path, 122.25f, 50.999f);
	fz_lineto(ctx, path, 177.751f, 50.999f);
	fz_lineto(ctx, path, 177.751f, 0.0f);
	fz_lineto(ctx, path, 140.251f, 0.0f);
	fz_closepath(ctx, path);
	fz_moveto(ctx, path, 23.482f, 129.419f);
	fz_curveto(ctx, path, -20.999f, 199.258f, -0.418f, 292.039f, 69.42f, 336.519f);
	fz_curveto(ctx, path, 139.259f, 381.0f, 232.04f, 360.419f, 276.52f, 290.581f);
	fz_curveto(ctx, path, 321.001f, 220.742f, 300.42f, 127.961f, 230.582f, 83.481f);
	fz_curveto(ctx, path, 160.743f, 39.0f, 67.962f, 59.581f, 23.482f, 129.419f);
	fz_closepath(ctx, path);
	fz_moveto(ctx, path, 254.751f, 128.492f);
	fz_curveto(ctx, path, 303.074f, 182.82f, 295.364f, 263.762f, 237.541f, 309.165f);
	fz_curveto(ctx, path, 179.718f, 354.568f, 93.57f, 347.324f, 45.247f, 292.996f);
	fz_curveto(ctx, path, -3.076f, 238.668f, 4.634f, 157.726f, 62.457f, 112.323f);
	fz_curveto(ctx, path, 120.28f, 66.92f, 206.428f, 74.164f, 254.751f, 128.492f);
	fz_closepath(ctx, path);
	fz_moveto(ctx, path, 111.0f, 98.999f);
	fz_curveto(ctx, path, 87.424f, 106.253f, 68.25f, 122.249f, 51.75f, 144.749f);
	fz_lineto(ctx, path, 103.5f, 297.749f);
	fz_lineto(ctx, path, 213.75f, 298.499f);
	fz_curveto(ctx, path, 206.25f, 306.749f, 195.744f, 311.478f, 185.25f, 314.249f);
	fz_curveto(ctx, path, 164.22f, 319.802f, 141.22f, 319.775f, 120.0f, 314.999f);
	fz_curveto(ctx, path, 96.658f, 309.745f, 77.25f, 298.499f, 55.5f, 283.499f);
	fz_curveto(ctx, path, 69.75f, 299.249f, 84.617f, 311.546f, 102.75f, 319.499f);
	fz_curveto(ctx, path, 117.166f, 325.822f, 133.509f, 327.689f, 149.25f, 327.749f);
	fz_curveto(ctx, path, 164.21f, 327.806f, 179.924f, 326.532f, 193.5f, 320.249f);
	fz_curveto(ctx, path, 213.95f, 310.785f, 232.5f, 294.749f, 245.25f, 276.749f);
	fz_lineto(ctx, path, 227.25f, 276.749f);
	fz_curveto(ctx, path, 213.963f, 276.749f, 197.25f, 263.786f, 197.25f, 250.499f);
	fz_lineto(ctx, path, 197.25f, 112.499f);
	fz_curveto(ctx, path, 213.75f, 114.749f, 228.0f, 127.499f, 241.5f, 140.999f);
	fz_curveto(ctx, path, 231.75f, 121.499f, 215.175f, 109.723f, 197.25f, 101.249f);
	fz_curveto(ctx, path, 181.5f, 95.249f, 168.412f, 94.775f, 153.0f, 94.499f);
	fz_curveto(ctx, path, 139.42f, 94.256f, 120.75f, 95.999f, 111.0f, 98.999f);
	fz_closepath(ctx, path);
	fz_moveto(ctx, path, 125.25f, 105.749f);
	fz_lineto(ctx, path, 125.25f, 202.499f);
	fz_lineto(ctx, path, 95.25f, 117.749f);
	fz_curveto(ctx, path, 105.75f, 108.749f, 114.0f, 105.749f, 125.25f, 105.749f);
	fz_closepath(ctx, path);
}

static void insert_signature_appearance_layers(fz_context *ctx, pdf_document *doc, pdf_annot *annot)
{
	pdf_obj *ap = pdf_dict_getl(ctx, annot->obj, PDF_NAME_AP, PDF_NAME_N, NULL);
	pdf_obj *main_ap = NULL;
	pdf_obj *frm = NULL;
	pdf_obj *n0 = NULL;
	fz_rect bbox;
	fz_buffer *fzbuf = NULL;

	pdf_to_rect(ctx, pdf_dict_get(ctx, ap, PDF_NAME_BBox), &bbox);

	fz_var(main_ap);
	fz_var(frm);
	fz_var(n0);
	fz_var(fzbuf);
	fz_try(ctx)
	{
		main_ap = pdf_new_xobject(ctx, doc, &bbox, &fz_identity);
		frm = pdf_new_xobject(ctx, doc, &bbox, &fz_identity);
		n0 = pdf_new_xobject(ctx, doc, &bbox, &fz_identity);

		pdf_dict_putl(ctx, main_ap, frm, PDF_NAME_Resources, PDF_NAME_XObject, PDF_NAME_FRM, NULL);
		fzbuf = fz_new_buffer(ctx, 8);
		fz_append_printf(ctx, fzbuf, "/FRM Do");
		pdf_update_stream(ctx, doc, main_ap, fzbuf, 0);
		fz_drop_buffer(ctx, fzbuf);
		fzbuf = NULL;

		pdf_dict_putl(ctx, frm, n0, PDF_NAME_Resources, PDF_NAME_XObject, PDF_NAME_n0, NULL);
		pdf_dict_putl(ctx, frm, ap, PDF_NAME_Resources, PDF_NAME_XObject, PDF_NAME_n2, NULL);
		fzbuf = fz_new_buffer(ctx, 8);
		fz_append_printf(ctx, fzbuf, "q 1 0 0 1 0 0 cm /n0 Do Q q 1 0 0 1 0 0 cm /n2 Do Q");
		pdf_update_stream(ctx, doc, frm, fzbuf, 0);
		fz_drop_buffer(ctx, fzbuf);
		fzbuf = NULL;

		fzbuf = fz_new_buffer(ctx, 8);
		fz_append_printf(ctx, fzbuf, "%% DSBlank");
		pdf_update_stream(ctx, doc, n0, fzbuf, 0);
		fz_drop_buffer(ctx, fzbuf);
		fzbuf = NULL;

		pdf_dict_putl(ctx, annot->obj, main_ap, PDF_NAME_AP, PDF_NAME_N, NULL);
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, main_ap);
		pdf_drop_obj(ctx, frm);
		pdf_drop_obj(ctx, n0);
	}
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, fzbuf);
		fz_rethrow(ctx);
	}
}

/* MuPDF blue */
static float logo_color[3] = {(float)0x25/(float)0xFF, (float)0x72/(float)0xFF, (float)0xAC/(float)0xFF};

void pdf_set_signature_appearance(fz_context *ctx, pdf_document *doc, pdf_annot *annot, char *name, const char *dn, char *date)
{
	pdf_obj *obj = annot->obj;
	pdf_obj *dr = pdf_dict_getl(ctx, pdf_trailer(ctx, doc), PDF_NAME_Root, PDF_NAME_AcroForm, PDF_NAME_DR, NULL);
	fz_display_list *dlist = NULL;
	fz_device *dev = NULL;
	font_info font_rec;
	fz_text *text = NULL;
	fz_path *path = NULL;
	fz_buffer *fzbuf = NULL;
	fz_matrix page_ctm;

	pdf_page_transform(ctx, annot->page, NULL, &page_ctm);

	if (!dr)
		pdf_dict_putl_drop(ctx, pdf_trailer(ctx, doc), pdf_new_dict(ctx, doc, 1), PDF_NAME_Root, PDF_NAME_AcroForm, PDF_NAME_DR, NULL);

	memset(&font_rec, 0, sizeof(font_rec));

	fz_var(path);
	fz_var(dlist);
	fz_var(dev);
	fz_var(text);
	fz_var(fzbuf);
	fz_try(ctx)
	{
		char *da = pdf_to_str_buf(ctx, pdf_dict_get(ctx, obj, PDF_NAME_DA));
		fz_rect annot_rect;
		fz_rect logo_bounds;
		fz_matrix logo_tm;
		fz_rect rect;
		fz_colorspace *cs = fz_device_rgb(ctx); /* Borrowed reference */

		pdf_to_rect(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_Rect), &annot_rect);
		rect = annot_rect;

		dlist = fz_new_display_list(ctx, NULL);
		dev = fz_new_list_device(ctx, dlist);

		path = fz_new_path(ctx);
		draw_logo(ctx, path);
		fz_bound_path(ctx, path, NULL, &fz_identity, &logo_bounds);
		center_rect_within_rect(&logo_bounds, &rect, &logo_tm);
		fz_concat(&logo_tm, &logo_tm, &page_ctm);
		fz_fill_path(ctx, dev, path, 0, &logo_tm, cs, logo_color, 1.0f, NULL);

		get_font_info(ctx, doc, dr, da, &font_rec);

		switch (font_rec.da_rec.col_size)
		{
		case 1: cs = fz_device_gray(ctx); break;
		case 3: cs = fz_device_rgb(ctx); break;
		case 4: cs = fz_device_cmyk(ctx); break;
		}

		/* Display the name in the left-hand half of the form field */
		rect.x1 = (rect.x0 + rect.x1)/2.0f;
		text = fit_text(ctx, &font_rec, name, &rect);
		fz_fill_text(ctx, dev, text, &page_ctm, cs, font_rec.da_rec.col, 1.0f, NULL);
		fz_drop_text(ctx, text);
		text = NULL;

		/* Display the distinguished name in the right-hand half */
		fzbuf = fz_new_buffer(ctx, 256);
		fz_append_printf(ctx, fzbuf, "Digitally signed by %s", name);
		fz_append_printf(ctx, fzbuf, "\nDN: %s", dn);
		if (date)
			fz_append_printf(ctx, fzbuf, "\nDate: %s", date);
		rect = annot_rect;
		rect.x0 = (rect.x0 + rect.x1)/2.0f;
		text = fit_text(ctx, &font_rec, fz_string_from_buffer(ctx, fzbuf), &rect);
		fz_fill_text(ctx, dev, text, &page_ctm, cs, font_rec.da_rec.col, 1.0f, NULL);

		fz_close_device(ctx, dev);

		rect = annot_rect;
		fz_transform_rect(&rect, &page_ctm);
		pdf_set_annot_appearance(ctx, doc, annot, &rect, dlist);

		/* Drop the cached xobject from the annotation structure to
		 * force a redraw on next pdf_update_page call */
		pdf_drop_obj(ctx, annot->ap);
		annot->ap = NULL;

		insert_signature_appearance_layers(ctx, doc, annot);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
		fz_drop_display_list(ctx, dlist);
		font_info_fin(ctx, &font_rec);
		fz_drop_path(ctx, path);
		fz_drop_text(ctx, text);
		fz_drop_buffer(ctx, fzbuf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

void pdf_update_appearance(fz_context *ctx, pdf_annot *annot)
{
	pdf_document *doc = annot->page->doc;
	pdf_obj *obj = annot->obj;
	pdf_obj *ap = pdf_dict_get(ctx, obj, PDF_NAME_AP);

	if (!ap || !pdf_dict_get(ctx, ap, PDF_NAME_N) || pdf_obj_is_dirty(ctx, obj) || annot->needs_new_ap)
	{
		enum pdf_annot_type type = pdf_annot_type(ctx, annot);
		switch (type)
		{
		case PDF_ANNOT_WIDGET:
			switch (pdf_field_type(ctx, doc, obj))
			{
			case PDF_WIDGET_TYPE_TEXT:
				{
					#if 0
					pdf_obj *formatting = pdf_dict_getl(ctx, obj, PDF_NAME_AA, PDF_NAME_F, NULL);
					if (formatting && doc->js)
					{
						/* Apply formatting */
						pdf_js_event e;
						e.target = obj;
						e.value = pdf_field_value(ctx, doc, obj);
						fz_try(ctx)
						{
							pdf_js_setup_event(doc->js, &e);
						}
						fz_always(ctx)
						{
							fz_free(ctx, e.value);
						}
						fz_catch(ctx)
						{
							fz_rethrow(ctx);
						}
						execute_action(ctx, doc, obj, formatting);
						/* Update appearance from JS event.value */
						pdf_update_text_appearance(ctx, doc, obj, pdf_js_get_event(doc->js)->value);
					}
					else
					#endif
					{
						/* Update appearance from field value */
						pdf_update_text_appearance(ctx, doc, obj, NULL);
					}
				}
				break;
			case PDF_WIDGET_TYPE_PUSHBUTTON:
				pdf_update_pushbutton_appearance(ctx, doc, obj);
				break;
			case PDF_WIDGET_TYPE_LISTBOX:
				pdf_update_listbox_appearance(ctx, doc, obj);
				break;
			case PDF_WIDGET_TYPE_COMBOBOX:
				pdf_update_combobox_appearance(ctx, doc, obj);
				break;
			}
			annot->has_new_ap = 1;
			break;
		case PDF_ANNOT_TEXT:
			pdf_update_text_annot_appearance(ctx, doc, annot);
			break;
		case PDF_ANNOT_FREE_TEXT:
			pdf_update_free_text_annot_appearance(ctx, doc, annot);
			break;
		case PDF_ANNOT_STRIKE_OUT:
		case PDF_ANNOT_UNDERLINE:
		case PDF_ANNOT_HIGHLIGHT:
			pdf_update_text_markup_appearance(ctx, doc, annot, type);
			break;
		case PDF_ANNOT_INK:
			pdf_update_ink_appearance(ctx, doc, annot);
			break;
		default:
			break;
		}

		pdf_clean_obj(ctx, obj);
	}
}

void
pdf_update_annot(fz_context *ctx, pdf_annot *annot)
{
	pdf_document *doc = annot->page->doc;
	pdf_obj *obj, *ap, *as, *n;

	pdf_update_appearance(ctx, annot);

	obj = annot->obj;

	ap = pdf_dict_get(ctx, obj, PDF_NAME_AP);
	as = pdf_dict_get(ctx, obj, PDF_NAME_AS);

	if (pdf_is_dict(ctx, ap))
	{
		pdf_hotspot *hp = &doc->hotspot;

		n = NULL;
		if (hp->num == pdf_to_num(ctx, obj) && (hp->state & HOTSPOT_POINTER_DOWN))
			n = pdf_dict_get(ctx, ap, PDF_NAME_D); /* down state */
		if (n == NULL)
			n = pdf_dict_get(ctx, ap, PDF_NAME_N); /* normal state */

		/* lookup current state in sub-dictionary */
		if (!pdf_is_stream(ctx, n))
			n = pdf_dict_get(ctx, n, as);

		if (annot->ap != n)
		{
			pdf_drop_obj(ctx, annot->ap);
			annot->ap = NULL;
			if (pdf_is_stream(ctx, n))
				annot->ap = pdf_keep_obj(ctx, n);
			annot->has_new_ap = 1;
		}
	}
}
