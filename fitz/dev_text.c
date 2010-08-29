#include "fitz.h"

#define LINEDIST 0.9f
#define SPACEDIST 0.2f

#include <ft2build.h>
#include FT_FREETYPE_H

#if ((FREETYPE_MAJOR == 2) && (FREETYPE_MINOR == 1)) || \
	((FREETYPE_MAJOR == 2) && (FREETYPE_MINOR == 2)) || \
	((FREETYPE_MAJOR == 2) && (FREETYPE_MINOR == 3) && (FREETYPE_PATCH < 8))

int
FT_Get_Advance(FT_Face face, int gid, int masks, FT_Fixed *out)
{
	int err;
	err = FT_Load_Glyph(face, gid, masks);
	if (err)
		return err;
	if (masks & FT_LOAD_VERTICAL_LAYOUT)
		*out = face->glyph->metrics.vertAdvance << 10;
	else
		*out = face->glyph->metrics.horiAdvance << 10;
	return 0;
}

#else
#include FT_ADVANCES_H
#endif

typedef struct fz_textdevice_s fz_textdevice;

struct fz_textdevice_s
{
	fz_point point;
	fz_textspan *head;
	fz_textspan *span;
};

fz_textspan *
fz_newtextspan(void)
{
	fz_textspan *span;
	span = fz_malloc(sizeof(fz_textspan));
	span->font = nil;
	span->wmode = 0;
	span->size = 0;
	span->len = 0;
	span->cap = 0;
	span->text = nil;
	span->next = nil;
	span->eol = 0;
	return span;
}

void
fz_freetextspan(fz_textspan *span)
{
	if (span->font)
		fz_dropfont(span->font);
	if (span->next)
		fz_freetextspan(span->next);
	fz_free(span->text);
	fz_free(span);
}

static void
fz_addtextcharimp(fz_textspan *span, int c, fz_bbox bbox)
{
	if (span->len + 1 >= span->cap)
	{
		span->cap = span->cap > 1 ? (span->cap * 3) / 2 : 80;
		span->text = fz_realloc(span->text, sizeof(fz_textchar) * span->cap);
	}
	span->text[span->len].c = c;
	span->text[span->len].bbox = bbox;
	span->len ++;
}

static fz_bbox
fz_splitbbox(fz_bbox bbox, int i, int n)
{
	float w = (float)(bbox.x1 - bbox.x0) / n;
	float x0 = bbox.x0;
	bbox.x0 = x0 + i * w;
	bbox.x1 = x0 + (i + 1) * w;
	return bbox;
}

static void
fz_addtextchar(fz_textspan **last, fz_font *font, float size, int wmode, int c, fz_bbox bbox)
{
	fz_textspan *span = *last;

	if (!span->font)
	{
		span->font = fz_keepfont(font);
		span->size = size;
	}

	if (span->font != font || span->size != size || span->wmode != wmode)
	{
		span = fz_newtextspan();
		span->font = fz_keepfont(font);
		span->size = size;
		span->wmode = wmode;
		(*last)->next = span;
		*last = span;
	}

	switch (c)
	{
	case -1: /* ignore when one unicode character maps to multiple glyphs */
		break;
	case 0xFB00: /* ff */
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 0, 2));
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 1, 2));
		break;
	case 0xFB01: /* fi */
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 0, 2));
		fz_addtextcharimp(span, 'i', fz_splitbbox(bbox, 1, 2));
		break;
	case 0xFB02: /* fl */
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 0, 2));
		fz_addtextcharimp(span, 'l', fz_splitbbox(bbox, 1, 2));
		break;
	case 0xFB03: /* ffi */
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 0, 3));
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 1, 3));
		fz_addtextcharimp(span, 'i', fz_splitbbox(bbox, 2, 3));
		break;
	case 0xFB04: /* ffl */
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 0, 3));
		fz_addtextcharimp(span, 'f', fz_splitbbox(bbox, 1, 3));
		fz_addtextcharimp(span, 'l', fz_splitbbox(bbox, 2, 3));
		break;
	case 0xFB05: /* long st */
	case 0xFB06: /* st */
		fz_addtextcharimp(span, 's', fz_splitbbox(bbox, 0, 2));
		fz_addtextcharimp(span, 't', fz_splitbbox(bbox, 1, 2));
		break;
	default:
		fz_addtextcharimp(span, c, bbox);
		break;
	}
}

static void
fz_dividetextchars(fz_textspan **last, int n, fz_bbox bbox)
{
	fz_textspan *span = *last;
	int i, x;
	x = span->len - n;
	if (x >= 0)
		for (i = 0; i < n; i++)
			span->text[x + i].bbox = fz_splitbbox(bbox, i, n);
}

static void
fz_addtextnewline(fz_textspan **last, fz_font *font, float size, int wmode)
{
	fz_textspan *span;
	span = fz_newtextspan();
	span->font = fz_keepfont(font);
	span->size = size;
	span->wmode = wmode;
	(*last)->eol = 1;
	(*last)->next = span;
	*last = span;
}

void
fz_debugtextspanxml(fz_textspan *span)
{
	char buf[10];
	int c, n, k, i;

	printf("<span font=\"%s\" size=\"%g\" wmode=\"%d\" eol=\"%d\">\n",
		span->font ? span->font->name : "NULL", span->size, span->wmode, span->eol);

	for (i = 0; i < span->len; i++)
	{
		printf("\t<char ucs=\"");
		c = span->text[i].c;
		if (c < 128)
			putchar(c);
		else
		{
			n = runetochar(buf, &c);
			for (k = 0; k < n; k++)
				putchar(buf[k]);
		}
		printf("\" bbox=\"[%d %d %d %d]\">\n",
			span->text[i].bbox.x0,
			span->text[i].bbox.y0,
			span->text[i].bbox.x1,
			span->text[i].bbox.y1);
	}

	printf("</span>\n");

	if (span->next)
		fz_debugtextspanxml(span->next);
}

void
fz_debugtextspan(fz_textspan *span)
{
	char buf[10];
	int c, n, k, i;

	for (i = 0; i < span->len; i++)
	{
		c = span->text[i].c;
		if (c < 128)
			putchar(c);
		else
		{
			n = runetochar(buf, &c);
			for (k = 0; k < n; k++)
				putchar(buf[k]);
		}
	}

	if (span->eol)
		putchar('\n');

	if (span->next)
		fz_debugtextspan(span->next);
}

static void
fz_textextractspan(fz_textspan **last, fz_text *text, fz_matrix ctm, fz_point *pen)
{
	fz_font *font = text->font;
	FT_Face face = font->ftface;
	fz_matrix tm = text->trm;
	fz_matrix trm;
	float size;
	float adv;
	fz_rect rect;
	fz_point dir, ndir;
	fz_point delta, ndelta;
	float dist, dot;
	float ascender = 1;
	float descender = 0;
	int multi;
	int i, err;

	if (text->len == 0)
		return;

	if (font->ftface)
	{
		err = FT_Set_Char_Size(font->ftface, 64, 64, 72, 72);
		if (err)
			fz_warn("freetype set character size: %s", ft_errorstring(err));
		ascender = (float)face->ascender / face->units_per_EM;
		descender = (float)face->descender / face->units_per_EM;
	}

	rect = fz_emptyrect;

	if (text->wmode == 0)
	{
		dir.x = 1;
		dir.y = 0;
	}
	else
	{
		dir.x = 0;
		dir.y = 1;
	}

	tm.e = 0;
	tm.f = 0;
	trm = fz_concat(tm, ctm);
	dir = fz_transformvector(trm, dir);
	dist = sqrtf(dir.x * dir.x + dir.y * dir.y);
	ndir.x = dir.x / dist;
	ndir.y = dir.y / dist;

	size = fz_matrixexpansion(trm);

	multi = 1;

	for (i = 0; i < text->len; i++)
	{
		if (text->els[i].gid < 0)
		{
			fz_addtextchar(last, font, size, text->wmode, text->els[i].ucs, fz_roundrect(rect));
			multi ++;
			fz_dividetextchars(last, multi, fz_roundrect(rect));
			continue;
		}
		multi = 1;

		/* Calculate new pen location and delta */
		tm.e = text->els[i].x;
		tm.f = text->els[i].y;
		trm = fz_concat(tm, ctm);

		delta.x = pen->x - trm.e;
		delta.y = pen->y - trm.f;
		if (pen->x == -1 && pen->y == -1)
			delta.x = delta.y = 0;

		dist = sqrtf(delta.x * delta.x + delta.y * delta.y);

		/* Add space and newlines based on pen movement */
		if (dist > 0)
		{
			ndelta.x = delta.x / dist;
			ndelta.y = delta.y / dist;
			dot = ndelta.x * ndir.x + ndelta.y * ndir.y;

			if (dist > size * LINEDIST)
			{
				fz_addtextnewline(last, font, size, text->wmode);
			}
			else if (fabsf(dot) > 0.95f && dist > size * SPACEDIST)
			{
				if ((*last)->len > 0 && (*last)->text[(*last)->len - 1].c != ' ')
				{
					fz_rect spacerect;
					spacerect.x0 = -0.2f;
					spacerect.y0 = 0;
					spacerect.x1 = 0;
					spacerect.y1 = 1;
					spacerect = fz_transformrect(trm, spacerect);
					fz_addtextchar(last, font, size, text->wmode, ' ', fz_roundrect(spacerect));
				}
			}
		}

		/* Calculate bounding box and new pen position based on font metrics */
		if (font->ftface)
		{
			FT_Fixed ftadv = 0;
			int mask = FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING | FT_LOAD_IGNORE_TRANSFORM;
			if (text->wmode)
				mask |= FT_LOAD_VERTICAL_LAYOUT;
			FT_Get_Advance(font->ftface, text->els[i].gid, mask, &ftadv);
			adv = ftadv / 65536.0f;
			if (text->wmode)
			{
				adv = -1; /* TODO: freetype returns broken vertical metrics */
				rect.x0 = 0;
				rect.y0 = 0;
				rect.x1 = 1;
				rect.y1 = adv;
			}
			else
			{
				rect.x0 = 0;
				rect.y0 = descender;
				rect.x1 = adv;
				rect.y1 = ascender;
			}
		}
		else
		{
			adv = font->t3widths[text->els[i].gid];
			rect.x0 = 0;
			rect.y0 = descender;
			rect.x1 = adv;
			rect.y1 = ascender;
		}

		rect = fz_transformrect(trm, rect);
		pen->x = trm.e + dir.x * adv;
		pen->y = trm.f + dir.y * adv;

		fz_addtextchar(last, font, size, text->wmode, text->els[i].ucs, fz_roundrect(rect));
	}
}

static void
fz_textfilltext(void *user, fz_text *text, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_textdevice *tdev = user;
	fz_textextractspan(&tdev->span, text, ctm, &tdev->point);
}

static void
fz_textstroketext(void *user, fz_text *text, fz_strokestate *stroke, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_textdevice *tdev = user;
	fz_textextractspan(&tdev->span, text, ctm, &tdev->point);
}

static void
fz_textcliptext(void *user, fz_text *text, fz_matrix ctm, int accumulate)
{
	fz_textdevice *tdev = user;
	fz_textextractspan(&tdev->span, text, ctm, &tdev->point);
}

static void
fz_textclipstroketext(void *user, fz_text *text, fz_strokestate *stroke, fz_matrix ctm)
{
	fz_textdevice *tdev = user;
	fz_textextractspan(&tdev->span, text, ctm, &tdev->point);
}

static void
fz_textignoretext(void *user, fz_text *text, fz_matrix ctm)
{
	fz_textdevice *tdev = user;
	fz_textextractspan(&tdev->span, text, ctm, &tdev->point);
}

static void
fz_textfreeuser(void *user)
{
	fz_textdevice *tdev = user;

	tdev->span->eol = 1;

	/* TODO: unicode NFC normalization */
	/* TODO: bidi logical reordering */

	fz_free(tdev);
}

fz_device *
fz_newtextdevice(fz_textspan *root)
{
	fz_device *dev;
	fz_textdevice *tdev = fz_malloc(sizeof(fz_textdevice));
	tdev->head = root;
	tdev->span = root;
	tdev->point.x = -1;
	tdev->point.y = -1;

	dev = fz_newdevice(tdev);
	dev->hints = FZ_IGNOREIMAGE | FZ_IGNORESHADE;
	dev->freeuser = fz_textfreeuser;
	dev->filltext = fz_textfilltext;
	dev->stroketext = fz_textstroketext;
	dev->cliptext = fz_textcliptext;
	dev->clipstroketext = fz_textclipstroketext;
	dev->ignoretext = fz_textignoretext;
	return dev;
}
