#include "mupdf/fitz.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_STROKER_H

#define MAX_BBOX_TABLE_SIZE 4096

/* 20 degrees */
#define SHEAR 0.36397f

static void fz_drop_freetype(fz_context *ctx);

static fz_font *
fz_new_font(fz_context *ctx, const char *name, int use_glyph_bbox, int glyph_count)
{
	fz_font *font;
	int i;

	font = fz_malloc_struct(ctx, fz_font);
	font->refs = 1;

	if (name)
		fz_strlcpy(font->name, name, sizeof font->name);
	else
		fz_strlcpy(font->name, "(null)", sizeof font->name);

	font->ft_face = NULL;
	font->ft_substitute = 0;
	font->ft_bold = 0;
	font->ft_italic = 0;
	font->ft_hint = 0;

	font->ft_buffer = NULL;
	font->ft_filepath = NULL;

	font->t3matrix = fz_identity;
	font->t3resources = NULL;
	font->t3procs = NULL;
	font->t3lists = NULL;
	font->t3widths = NULL;
	font->t3flags = NULL;
	font->t3doc = NULL;
	font->t3run = NULL;

	font->bbox.x0 = 0;
	font->bbox.y0 = 0;
	font->bbox.x1 = 1;
	font->bbox.y1 = 1;

	font->use_glyph_bbox = use_glyph_bbox;
	if (use_glyph_bbox && glyph_count <= MAX_BBOX_TABLE_SIZE)
	{
		font->bbox_count = glyph_count;
		font->bbox_table = fz_malloc_array(ctx, glyph_count, sizeof(fz_rect));
		for (i = 0; i < glyph_count; i++)
			font->bbox_table[i] = fz_infinite_rect;
	}
	else
	{
		if (use_glyph_bbox)
			fz_warn(ctx, "not building glyph bbox table for font '%s' with %d glyphs", font->name, glyph_count);
		font->bbox_count = 0;
		font->bbox_table = NULL;
	}

	font->width_count = 0;
	font->width_table = NULL;

	return font;
}

fz_font *
fz_keep_font(fz_context *ctx, fz_font *font)
{
	return fz_keep_imp(ctx, font, &font->refs);
}

static void
free_resources(fz_context *ctx, fz_font *font)
{
	int i;

	if (font->t3resources)
	{
		font->t3freeres(ctx, font->t3doc, font->t3resources);
		font->t3resources = NULL;
	}

	if (font->t3procs)
	{
		for (i = 0; i < 256; i++)
			if (font->t3procs[i])
				fz_drop_buffer(ctx, font->t3procs[i]);
	}
	fz_free(ctx, font->t3procs);
	font->t3procs = NULL;
}

void fz_decouple_type3_font(fz_context *ctx, fz_font *font, void *t3doc)
{
	if (!ctx || !font || !t3doc || font->t3doc == NULL)
		return;

	if (font->t3doc != t3doc)
		fz_throw(ctx, FZ_ERROR_GENERIC, "can't decouple type3 font from a different doc");

	font->t3doc = NULL;
	free_resources(ctx, font);
}

void
fz_drop_font(fz_context *ctx, fz_font *font)
{
	int fterr;
	int i;

	if (!fz_drop_imp(ctx, font, &font->refs))
		return;

	free_resources(ctx, font);
	if (font->t3lists)
	{
		for (i = 0; i < 256; i++)
		{
			if (font->t3lists[i])
				fz_drop_display_list(ctx, font->t3lists[i]);
		}
		fz_free(ctx, font->t3procs);
		fz_free(ctx, font->t3lists);
		fz_free(ctx, font->t3widths);
		fz_free(ctx, font->t3flags);
	}

	if (font->ft_face)
	{
		fz_lock(ctx, FZ_LOCK_FREETYPE);
		fterr = FT_Done_Face((FT_Face)font->ft_face);
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		if (fterr)
			fz_warn(ctx, "freetype finalizing face: %s", ft_error_string(fterr));
		fz_drop_freetype(ctx);
	}

	fz_drop_buffer(ctx, font->ft_buffer);
	fz_free(ctx, font->ft_filepath);
	fz_free(ctx, font->bbox_table);
	fz_free(ctx, font->width_table);
	fz_free(ctx, font);
}

void
fz_set_font_bbox(fz_context *ctx, fz_font *font, float xmin, float ymin, float xmax, float ymax)
{
	if (xmin >= xmax || ymin >= ymax)
	{
		/* Invalid bbox supplied. It would be prohibitively slow to
		 * measure the true one, so make one up. */
		font->bbox.x0 = -1;
		font->bbox.y0 = -1;
		font->bbox.x1 = 2;
		font->bbox.y1 = 2;
	}
	else
	{
		font->bbox.x0 = xmin;
		font->bbox.y0 = ymin;
		font->bbox.x1 = xmax;
		font->bbox.y1 = ymax;
	}
}

/*
 * Freetype hooks
 */

struct fz_font_context_s {
	int ctx_refs;
	FT_Library ftlib;
	int ftlib_refs;
	fz_load_system_font_func load_font;
	fz_load_system_cjk_font_func load_cjk_font;
};

#undef __FTERRORS_H__
#define FT_ERRORDEF(e, v, s) { (e), (s) },
#define FT_ERROR_START_LIST
#define FT_ERROR_END_LIST { 0, NULL }

struct ft_error
{
	int err;
	char *str;
};

void fz_new_font_context(fz_context *ctx)
{
	ctx->font = fz_malloc_struct(ctx, fz_font_context);
	ctx->font->ctx_refs = 1;
	ctx->font->ftlib = NULL;
	ctx->font->ftlib_refs = 0;
	ctx->font->load_font = NULL;
}

fz_font_context *
fz_keep_font_context(fz_context *ctx)
{
	if (!ctx)
		return NULL;
	return fz_keep_imp(ctx, ctx->font, &ctx->font->ctx_refs);
}

void fz_drop_font_context(fz_context *ctx)
{
	if (!ctx)
		return;
	if (fz_drop_imp(ctx, ctx->font, &ctx->font->ctx_refs))
		fz_free(ctx, ctx->font);
}

void fz_install_load_system_font_funcs(fz_context *ctx, fz_load_system_font_func f, fz_load_system_cjk_font_func f_cjk)
{
	ctx->font->load_font = f;
	ctx->font->load_cjk_font = f_cjk;
}

fz_font *fz_load_system_font(fz_context *ctx, const char *name, int bold, int italic, int needs_exact_metrics)
{
	fz_font *font = NULL;

	if (ctx->font->load_font)
	{
		fz_try(ctx)
		{
			font = ctx->font->load_font(ctx, name, bold, italic, needs_exact_metrics);
		}
		fz_catch(ctx)
		{
			font = NULL;
		}
	}

	return font;
}

fz_font *fz_load_system_cjk_font(fz_context *ctx, const char *name, int ros, int serif)
{
	fz_font *font = NULL;

	if (ctx->font->load_cjk_font)
	{
		fz_try(ctx)
		{
			font = ctx->font->load_cjk_font(ctx, name, ros, serif);
		}
		fz_catch(ctx)
		{
			font = NULL;
		}
	}

	return font;
}

static const struct ft_error ft_errors[] =
{
#include FT_ERRORS_H
};

char *ft_error_string(int err)
{
	const struct ft_error *e;

	for (e = ft_errors; e->str; e++)
		if (e->err == err)
			return e->str;

	return "Unknown error";
}

static void
fz_keep_freetype(fz_context *ctx)
{
	int fterr;
	int maj, min, pat;
	fz_font_context *fct = ctx->font;

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	if (fct->ftlib)
	{
		fct->ftlib_refs++;
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		return;
	}

	fterr = FT_Init_FreeType(&fct->ftlib);
	if (fterr)
	{
		char *mess = ft_error_string(fterr);
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot init freetype: %s", mess);
	}

	FT_Library_Version(fct->ftlib, &maj, &min, &pat);
	if (maj == 2 && min == 1 && pat < 7)
	{
		fterr = FT_Done_FreeType(fct->ftlib);
		if (fterr)
			fz_warn(ctx, "freetype finalizing: %s", ft_error_string(fterr));
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		fz_throw(ctx, FZ_ERROR_GENERIC, "freetype version too old: %d.%d.%d", maj, min, pat);
	}

	fct->ftlib_refs++;
	fz_unlock(ctx, FZ_LOCK_FREETYPE);
}

static void
fz_drop_freetype(fz_context *ctx)
{
	int fterr;
	fz_font_context *fct = ctx->font;

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	if (--fct->ftlib_refs == 0)
	{
		fterr = FT_Done_FreeType(fct->ftlib);
		if (fterr)
			fz_warn(ctx, "freetype finalizing: %s", ft_error_string(fterr));
		fct->ftlib = NULL;
	}
	fz_unlock(ctx, FZ_LOCK_FREETYPE);
}

fz_font *
fz_new_font_from_file(fz_context *ctx, const char *name, const char *path, int index, int use_glyph_bbox)
{
	FT_Face face;
	fz_font *font;
	int fterr;

	fz_keep_freetype(ctx);

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	fterr = FT_New_Face(ctx->font->ftlib, path, index, &face);
	fz_unlock(ctx, FZ_LOCK_FREETYPE);
	if (fterr)
	{
		fz_drop_freetype(ctx);
		fz_throw(ctx, FZ_ERROR_GENERIC, "freetype: cannot load font: %s", ft_error_string(fterr));
	}

	if (!name)
		name = face->family_name;

	font = fz_new_font(ctx, name, use_glyph_bbox, face->num_glyphs);
	font->ft_face = face;
	fz_set_font_bbox(ctx, font,
		(float) face->bbox.xMin / face->units_per_EM,
		(float) face->bbox.yMin / face->units_per_EM,
		(float) face->bbox.xMax / face->units_per_EM,
		(float) face->bbox.yMax / face->units_per_EM);
	font->ft_filepath = fz_strdup(ctx, path);

	return font;
}

fz_font *
fz_new_font_from_memory(fz_context *ctx, const char *name, unsigned char *data, int len, int index, int use_glyph_bbox)
{
	FT_Face face;
	fz_font *font;
	int fterr;

	fz_keep_freetype(ctx);

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	fterr = FT_New_Memory_Face(ctx->font->ftlib, data, len, index, &face);
	fz_unlock(ctx, FZ_LOCK_FREETYPE);
	if (fterr)
	{
		fz_drop_freetype(ctx);
		fz_throw(ctx, FZ_ERROR_GENERIC, "freetype: cannot load font: %s", ft_error_string(fterr));
	}

	if (!name)
		name = face->family_name;

	font = fz_new_font(ctx, name, use_glyph_bbox, face->num_glyphs);
	font->ft_face = face;
	fz_set_font_bbox(ctx, font,
		(float) face->bbox.xMin / face->units_per_EM,
		(float) face->bbox.yMin / face->units_per_EM,
		(float) face->bbox.xMax / face->units_per_EM,
		(float) face->bbox.yMax / face->units_per_EM);

	return font;
}

fz_font *
fz_new_font_from_buffer(fz_context *ctx, const char *name, fz_buffer *buffer, int index, int use_glyph_bbox)
{
	fz_font *font = fz_new_font_from_memory(ctx, name, buffer->data, buffer->len, index, use_glyph_bbox);
	font->ft_buffer = fz_keep_buffer(ctx, buffer); /* remember buffer so we can drop it when we free the font */
	return font;
}

static fz_matrix *
fz_adjust_ft_glyph_width(fz_context *ctx, fz_font *font, int gid, fz_matrix *trm)
{
	/* Fudge the font matrix to stretch the glyph if we've substituted the font. */
	if (font->ft_substitute && font->width_table && gid < font->width_count /* && font->wmode == 0 */)
	{
		FT_Error fterr;
		int subw;
		int realw;
		float scale;

		fz_lock(ctx, FZ_LOCK_FREETYPE);
		/* TODO: use FT_Get_Advance */
		fterr = FT_Set_Char_Size(font->ft_face, 1000, 1000, 72, 72);
		if (fterr)
			fz_warn(ctx, "freetype setting character size: %s", ft_error_string(fterr));

		fterr = FT_Load_Glyph(font->ft_face, gid,
			FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM);
		if (fterr)
			fz_warn(ctx, "freetype failed to load glyph: %s", ft_error_string(fterr));

		realw = ((FT_Face)font->ft_face)->glyph->metrics.horiAdvance;
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		subw = font->width_table[gid];
		if (realw)
			scale = (float) subw / realw;
		else
			scale = 1;

		fz_pre_scale(trm, scale, 1);
	}

	return trm;
}

static fz_glyph *
glyph_from_ft_bitmap(fz_context *ctx, int left, int top, FT_Bitmap *bitmap)
{
	if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
		return fz_new_glyph_from_1bpp_data(ctx, left, top - bitmap->rows, bitmap->width, bitmap->rows, bitmap->buffer + (bitmap->rows-1)*bitmap->pitch, -bitmap->pitch);
	else
		return fz_new_glyph_from_8bpp_data(ctx, left, top - bitmap->rows, bitmap->width, bitmap->rows, bitmap->buffer + (bitmap->rows-1)*bitmap->pitch, -bitmap->pitch);
}

static fz_pixmap *
pixmap_from_ft_bitmap(fz_context *ctx, int left, int top, FT_Bitmap *bitmap)
{
	if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO)
		return fz_new_pixmap_from_1bpp_data(ctx, left, top - bitmap->rows, bitmap->width, bitmap->rows, bitmap->buffer + (bitmap->rows-1)*bitmap->pitch, -bitmap->pitch);
	else
		return fz_new_pixmap_from_8bpp_data(ctx, left, top - bitmap->rows, bitmap->width, bitmap->rows, bitmap->buffer + (bitmap->rows-1)*bitmap->pitch, -bitmap->pitch);
}

/* Takes the freetype lock, and returns with it held */
static FT_GlyphSlot
do_ft_render_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, int aa)
{
	FT_Face face = font->ft_face;
	FT_Matrix m;
	FT_Vector v;
	FT_Error fterr;
	fz_matrix local_trm = *trm;

	float strength = fz_matrix_expansion(trm) * 0.02f;

	fz_adjust_ft_glyph_width(ctx, font, gid, &local_trm);

	if (font->ft_italic)
		fz_pre_shear(&local_trm, SHEAR, 0);

	/*
	Freetype mutilates complex glyphs if they are loaded
	with FT_Set_Char_Size 1.0. it rounds the coordinates
	before applying transformation. to get more precision in
	freetype, we shift part of the scale in the matrix
	into FT_Set_Char_Size instead
	*/

	m.xx = local_trm.a * 64; /* should be 65536 */
	m.yx = local_trm.b * 64;
	m.xy = local_trm.c * 64;
	m.yy = local_trm.d * 64;
	v.x = local_trm.e * 64;
	v.y = local_trm.f * 64;

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	fterr = FT_Set_Char_Size(face, 65536, 65536, 72, 72); /* should be 64, 64 */
	if (fterr)
		fz_warn(ctx, "freetype setting character size: %s", ft_error_string(fterr));
	FT_Set_Transform(face, &m, &v);

	if (aa == 0)
	{
		/* enable grid fitting for non-antialiased rendering */
		float scale = fz_matrix_expansion(&local_trm);
		m.xx = local_trm.a * 65536 / scale;
		m.yx = local_trm.b * 65536 / scale;
		m.xy = local_trm.c * 65536 / scale;
		m.yy = local_trm.d * 65536 / scale;
		v.x = 0;
		v.y = 0;

		fterr = FT_Set_Char_Size(face, 64 * scale, 64 * scale, 72, 72);
		if (fterr)
			fz_warn(ctx, "freetype setting character size: %s", ft_error_string(fterr));
		FT_Set_Transform(face, &m, &v);
		fterr = FT_Load_Glyph(face, gid, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_MONO);
		if (fterr) {
			fz_warn(ctx, "freetype load hinted glyph (gid %d): %s", gid, ft_error_string(fterr));
			goto retry_unhinted;
		}
	}
	else if (font->ft_hint)
	{
		/*
		Enable hinting, but keep the huge char size so that
		it is hinted for a character. This will in effect nullify
		the effect of grid fitting. This form of hinting should
		only be used for DynaLab and similar tricky TrueType fonts,
		so that we get the correct outline shape.
		*/
		fterr = FT_Load_Glyph(face, gid, FT_LOAD_NO_BITMAP);
		if (fterr) {
			fz_warn(ctx, "freetype load hinted glyph (gid %d): %s", gid, ft_error_string(fterr));
			goto retry_unhinted;
		}
	}
	else
	{
retry_unhinted:
		fterr = FT_Load_Glyph(face, gid, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
		if (fterr)
		{
			fz_warn(ctx, "freetype load glyph (gid %d): %s", gid, ft_error_string(fterr));
			return NULL;
		}
	}

	if (font->ft_bold)
	{
		FT_Outline_Embolden(&face->glyph->outline, strength * 64);
		FT_Outline_Translate(&face->glyph->outline, -strength * 32, -strength * 32);
	}

	fterr = FT_Render_Glyph(face->glyph, fz_aa_level(ctx) > 0 ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO);
	if (fterr)
	{
		fz_warn(ctx, "freetype render glyph (gid %d): %s", gid, ft_error_string(fterr));
		return NULL;
	}
	return face->glyph;
}

fz_pixmap *
fz_render_ft_glyph_pixmap(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, int aa)
{
	FT_GlyphSlot slot = do_ft_render_glyph(ctx, font, gid, trm, aa);
	fz_pixmap *pixmap;

	if (slot == NULL)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		return NULL;
	}

	fz_try(ctx)
	{
		pixmap = pixmap_from_ft_bitmap(ctx, slot->bitmap_left, slot->bitmap_top, &slot->bitmap);
	}
	fz_always(ctx)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return pixmap;
}

/* The glyph cache lock is always taken when this is called. */
fz_glyph *
fz_render_ft_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, int aa)
{
	FT_GlyphSlot slot = do_ft_render_glyph(ctx, font, gid, trm, aa);
	fz_glyph *glyph;

	if (slot == NULL)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		return NULL;
	}

	fz_try(ctx)
	{
		glyph = glyph_from_ft_bitmap(ctx, slot->bitmap_left, slot->bitmap_top, &slot->bitmap);
	}
	fz_always(ctx)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return glyph;
}

/* Takes the freetype lock, and returns with it held */
static FT_Glyph
do_render_ft_stroked_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, const fz_matrix *ctm, fz_stroke_state *state)
{
	FT_Face face = font->ft_face;
	float expansion = fz_matrix_expansion(ctm);
	int linewidth = state->linewidth * expansion * 64 / 2;
	FT_Matrix m;
	FT_Vector v;
	FT_Error fterr;
	FT_Stroker stroker;
	FT_Glyph glyph;
	FT_Stroker_LineJoin line_join;
	fz_matrix local_trm = *trm;

	fz_adjust_ft_glyph_width(ctx, font, gid, &local_trm);

	if (font->ft_italic)
		fz_pre_shear(&local_trm, SHEAR, 0);

	m.xx = local_trm.a * 64; /* should be 65536 */
	m.yx = local_trm.b * 64;
	m.xy = local_trm.c * 64;
	m.yy = local_trm.d * 64;
	v.x = local_trm.e * 64;
	v.y = local_trm.f * 64;

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	fterr = FT_Set_Char_Size(face, 65536, 65536, 72, 72); /* should be 64, 64 */
	if (fterr)
	{
		fz_warn(ctx, "FT_Set_Char_Size: %s", ft_error_string(fterr));
		return NULL;
	}

	FT_Set_Transform(face, &m, &v);

	fterr = FT_Load_Glyph(face, gid, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
	if (fterr)
	{
		fz_warn(ctx, "FT_Load_Glyph(gid %d): %s", gid, ft_error_string(fterr));
		return NULL;
	}

	fterr = FT_Stroker_New(ctx->font->ftlib, &stroker);
	if (fterr)
	{
		fz_warn(ctx, "FT_Stroker_New: %s", ft_error_string(fterr));
		return NULL;
	}

#if FREETYPE_MAJOR * 10000 + FREETYPE_MINOR * 100 + FREETYPE_PATCH > 20405
	/* New freetype */
	line_join =
		state->linejoin == FZ_LINEJOIN_MITER ? FT_STROKER_LINEJOIN_MITER_FIXED :
		state->linejoin == FZ_LINEJOIN_ROUND ? FT_STROKER_LINEJOIN_ROUND :
		state->linejoin == FZ_LINEJOIN_BEVEL ? FT_STROKER_LINEJOIN_BEVEL :
		FT_STROKER_LINEJOIN_MITER_VARIABLE;
#else
	/* Old freetype */
	line_join =
		state->linejoin == FZ_LINEJOIN_MITER ? FT_STROKER_LINEJOIN_MITER :
		state->linejoin == FZ_LINEJOIN_ROUND ? FT_STROKER_LINEJOIN_ROUND :
		state->linejoin == FZ_LINEJOIN_BEVEL ? FT_STROKER_LINEJOIN_BEVEL :
		FT_STROKER_LINEJOIN_MITER;
#endif
	FT_Stroker_Set(stroker, linewidth, (FT_Stroker_LineCap)state->start_cap, line_join, state->miterlimit * 65536);

	fterr = FT_Get_Glyph(face->glyph, &glyph);
	if (fterr)
	{
		fz_warn(ctx, "FT_Get_Glyph: %s", ft_error_string(fterr));
		FT_Stroker_Done(stroker);
		return NULL;
	}

	fterr = FT_Glyph_Stroke(&glyph, stroker, 1);
	if (fterr)
	{
		fz_warn(ctx, "FT_Glyph_Stroke: %s", ft_error_string(fterr));
		FT_Done_Glyph(glyph);
		FT_Stroker_Done(stroker);
		return NULL;
	}

	FT_Stroker_Done(stroker);

	fterr = FT_Glyph_To_Bitmap(&glyph, fz_aa_level(ctx) > 0 ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO, 0, 1);
	if (fterr)
	{
		fz_warn(ctx, "FT_Glyph_To_Bitmap: %s", ft_error_string(fterr));
		FT_Done_Glyph(glyph);
		return NULL;
	}
	return glyph;
}

fz_pixmap *
fz_render_ft_stroked_glyph_pixmap(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, const fz_matrix *ctm, fz_stroke_state *state)
{
	FT_Glyph glyph = do_render_ft_stroked_glyph(ctx, font, gid, trm, ctm, state);
	FT_BitmapGlyph bitmap = (FT_BitmapGlyph)glyph;
	fz_pixmap *pixmap;

	if (bitmap == NULL)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		return NULL;
	}

	fz_try(ctx)
	{
		pixmap = pixmap_from_ft_bitmap(ctx, bitmap->left, bitmap->top, &bitmap->bitmap);
	}
	fz_always(ctx)
	{
		FT_Done_Glyph(glyph);
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return pixmap;
}

fz_glyph *
fz_render_ft_stroked_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, const fz_matrix *ctm, fz_stroke_state *state)
{
	FT_Glyph glyph = do_render_ft_stroked_glyph(ctx, font, gid, trm, ctm, state);
	FT_BitmapGlyph bitmap = (FT_BitmapGlyph)glyph;
	fz_glyph *result;

	if (bitmap == NULL)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		return NULL;
	}

	fz_try(ctx)
	{
		result = glyph_from_ft_bitmap(ctx, bitmap->left, bitmap->top, &bitmap->bitmap);
	}
	fz_always(ctx)
	{
		FT_Done_Glyph(glyph);
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return result;
}

static fz_rect *
fz_bound_ft_glyph(fz_context *ctx, fz_font *font, int gid, fz_rect *bounds)
{
	FT_Face face = font->ft_face;
	FT_Error fterr;
	FT_BBox cbox;
	FT_Matrix m;
	FT_Vector v;
	int ft_flags;

	// TODO: refactor loading into fz_load_ft_glyph
	// TODO: cache results

	const int scale = face->units_per_EM;
	const float recip = 1 / (float)scale;
	const float strength = 0.02f;
	fz_matrix local_trm = fz_identity;

	fz_adjust_ft_glyph_width(ctx, font, gid, &local_trm);

	if (font->ft_italic)
		fz_pre_shear(&local_trm, SHEAR, 0);

	m.xx = local_trm.a * 65536;
	m.yx = local_trm.b * 65536;
	m.xy = local_trm.c * 65536;
	m.yy = local_trm.d * 65536;
	v.x = local_trm.e * 65536;
	v.y = local_trm.f * 65536;

	if (font->ft_hint)
	{
		ft_flags = FT_LOAD_NO_BITMAP;
	}
	else
	{
		ft_flags = FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING;
	}

	fz_lock(ctx, FZ_LOCK_FREETYPE);
	/* Set the char size to scale=face->units_per_EM to effectively give
	 * us unscaled results. This avoids quantisation. We then apply the
	 * scale ourselves below. */
	fterr = FT_Set_Char_Size(face, scale, scale, 72, 72);
	if (fterr)
		fz_warn(ctx, "freetype setting character size: %s", ft_error_string(fterr));
	FT_Set_Transform(face, &m, &v);

	fterr = FT_Load_Glyph(face, gid, ft_flags);
	if (fterr)
	{
		fz_warn(ctx, "freetype load glyph (gid %d): %s", gid, ft_error_string(fterr));
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		bounds->x0 = bounds->x1 = local_trm.e;
		bounds->y0 = bounds->y1 = local_trm.f;
		return bounds;
	}

	if (font->ft_bold)
	{
		FT_Outline_Embolden(&face->glyph->outline, strength * scale);
		FT_Outline_Translate(&face->glyph->outline, -strength * 0.5 * scale, -strength * 0.5 * scale);
	}

	FT_Outline_Get_CBox(&face->glyph->outline, &cbox);
	fz_unlock(ctx, FZ_LOCK_FREETYPE);
	bounds->x0 = cbox.xMin * recip;
	bounds->y0 = cbox.yMin * recip;
	bounds->x1 = cbox.xMax * recip;
	bounds->y1 = cbox.yMax * recip;

	if (fz_is_empty_rect(bounds))
	{
		bounds->x0 = bounds->x1 = local_trm.e;
		bounds->y0 = bounds->y1 = local_trm.f;
	}

	return bounds;
}

/* Turn FT_Outline into a fz_path */

struct closure {
	fz_context *ctx;
	fz_path *path;
	fz_matrix trm;
};

static int move_to(const FT_Vector *p, void *cc_)
{
	struct closure *cc = (struct closure *)cc_;
	fz_context *ctx = cc->ctx;
	fz_path *path = cc->path;
	fz_point pt;

	fz_transform_point_xy(&pt, &cc->trm, p->x, p->y);
	fz_moveto(ctx, path, pt.x, pt.y);
	return 0;
}

static int line_to(const FT_Vector *p, void *cc_)
{
	struct closure *cc = (struct closure *)cc_;
	fz_context *ctx = cc->ctx;
	fz_path *path = cc->path;
	fz_point pt;

	fz_transform_point_xy(&pt, &cc->trm, p->x, p->y);
	fz_lineto(ctx, path, pt.x, pt.y);
	return 0;
}

static int conic_to(const FT_Vector *c, const FT_Vector *p, void *cc_)
{
	struct closure *cc = (struct closure *)cc_;
	fz_context *ctx = cc->ctx;
	fz_path *path = cc->path;
	fz_point ct, pt;

	fz_transform_point_xy(&ct, &cc->trm, c->x, c->y);
	fz_transform_point_xy(&pt, &cc->trm, p->x, p->y);

	fz_quadto(ctx, path, ct.x, ct.y, pt.x, pt.y);
	return 0;
}

static int cubic_to(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *p, void *cc_)
{
	struct closure *cc = (struct closure *)cc_;
	fz_context *ctx = cc->ctx;
	fz_path *path = cc->path;
	fz_point c1t, c2t, pt;

	fz_transform_point_xy(&c1t, &cc->trm, c1->x, c1->y);
	fz_transform_point_xy(&c2t, &cc->trm, c2->x, c2->y);
	fz_transform_point_xy(&pt, &cc->trm, p->x, p->y);

	fz_curveto(ctx, path, c1t.x, c1t.y, c2t.x, c2t.y, pt.x, pt.y);
	return 0;
}

static const FT_Outline_Funcs outline_funcs = {
	move_to, line_to, conic_to, cubic_to, 0, 0
};

fz_path *
fz_outline_ft_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm)
{
	struct closure cc;
	FT_Face face = font->ft_face;
	int fterr;
	fz_matrix local_trm = *trm;
	int ft_flags;

	const int scale = face->units_per_EM;
	const float recip = 1 / (float)scale;
	const float strength = 0.02f;

	fz_adjust_ft_glyph_width(ctx, font, gid, &local_trm);

	if (font->ft_italic)
		fz_pre_shear(&local_trm, SHEAR, 0);

	fz_lock(ctx, FZ_LOCK_FREETYPE);

	if (font->ft_hint)
	{
		ft_flags = FT_LOAD_NO_BITMAP | FT_LOAD_IGNORE_TRANSFORM;
		fterr = FT_Set_Char_Size(face, scale, scale, 72, 72);
		if (fterr)
			fz_warn(ctx, "freetype setting character size: %s", ft_error_string(fterr));
	}
	else
	{
		ft_flags = FT_LOAD_NO_SCALE | FT_LOAD_IGNORE_TRANSFORM;
	}

	fterr = FT_Load_Glyph(face, gid, ft_flags);
	if (fterr)
	{
		fz_warn(ctx, "freetype load glyph (gid %d): %s", gid, ft_error_string(fterr));
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
		return NULL;
	}

	if (font->ft_bold)
	{
		FT_Outline_Embolden(&face->glyph->outline, strength * scale);
		FT_Outline_Translate(&face->glyph->outline, -strength * 0.5 * scale, -strength * 0.5 * scale);
	}

	cc.path = NULL;
	fz_try(ctx)
	{
		cc.ctx = ctx;
		cc.path = fz_new_path(ctx);
		fz_concat(&cc.trm, fz_scale(&cc.trm, recip, recip), &local_trm);
		fz_moveto(ctx, cc.path, cc.trm.e, cc.trm.f);
		FT_Outline_Decompose(&face->glyph->outline, &outline_funcs, &cc);
		fz_closepath(ctx, cc.path);
	}
	fz_always(ctx)
	{
		fz_unlock(ctx, FZ_LOCK_FREETYPE);
	}
	fz_catch(ctx)
	{
		fz_warn(ctx, "freetype cannot decompose outline");
		fz_free(ctx, cc.path);
		return NULL;
	}

	return cc.path;
}

/*
 * Type 3 fonts...
 */

fz_font *
fz_new_type3_font(fz_context *ctx, const char *name, const fz_matrix *matrix)
{
	fz_font *font;
	int i;

	font = fz_new_font(ctx, name, 1, 256);
	fz_try(ctx)
	{
		font->t3procs = fz_malloc_array(ctx, 256, sizeof(fz_buffer*));
		font->t3lists = fz_malloc_array(ctx, 256, sizeof(fz_display_list*));
		font->t3widths = fz_malloc_array(ctx, 256, sizeof(float));
		font->t3flags = fz_malloc_array(ctx, 256, sizeof(unsigned short));
	}
	fz_catch(ctx)
	{
		fz_drop_font(ctx, font);
		fz_rethrow(ctx);
	}

	font->t3matrix = *matrix;
	for (i = 0; i < 256; i++)
	{
		font->t3procs[i] = NULL;
		font->t3lists[i] = NULL;
		font->t3widths[i] = 0;
		font->t3flags[i] = 0;
	}

	return font;
}

void
fz_prepare_t3_glyph(fz_context *ctx, fz_font *font, int gid, int nested_depth)
{
	fz_buffer *contents;
	fz_device *dev;

	contents = font->t3procs[gid];
	if (!contents)
		return;

	/* We've not already loaded this one! */
	assert(font->t3lists[gid] == NULL);

	font->t3lists[gid] = fz_new_display_list(ctx);

	dev = fz_new_list_device(ctx, font->t3lists[gid]);
	dev->flags = FZ_DEVFLAG_FILLCOLOR_UNDEFINED |
			FZ_DEVFLAG_STROKECOLOR_UNDEFINED |
			FZ_DEVFLAG_STARTCAP_UNDEFINED |
			FZ_DEVFLAG_DASHCAP_UNDEFINED |
			FZ_DEVFLAG_ENDCAP_UNDEFINED |
			FZ_DEVFLAG_LINEJOIN_UNDEFINED |
			FZ_DEVFLAG_MITERLIMIT_UNDEFINED |
			FZ_DEVFLAG_LINEWIDTH_UNDEFINED;
	font->t3run(ctx, font->t3doc, font->t3resources, contents, dev, &fz_identity, NULL, 0);
	font->t3flags[gid] = dev->flags;
	if (dev->flags & FZ_DEVFLAG_BBOX_DEFINED)
	{
		assert(font->bbox_table != NULL);
		assert(font->bbox_count > gid);
		font->bbox_table[gid] = dev->d1_rect;
		fz_transform_rect(&font->bbox_table[gid], &font->t3matrix);
	}
	fz_drop_device(ctx, dev);
}

static fz_rect *
fz_bound_t3_glyph(fz_context *ctx, fz_font *font, int gid, fz_rect *bounds)
{
	fz_display_list *list;
	fz_device *dev;
	fz_rect big;
	float m;

	list = font->t3lists[gid];
	if (!list)
	{
		*bounds = fz_empty_rect;
		return bounds;
	}

	dev = fz_new_bbox_device(ctx, bounds);
	fz_try(ctx)
	{
		fz_run_display_list(ctx, list, dev, &font->t3matrix, &fz_infinite_rect, NULL);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	/* clip the bbox size to a reasonable maximum for degenerate glyphs */
	big = font->bbox;
	m = fz_max(fz_abs(big.x1 - big.x0), fz_abs(big.y1 - big.y0));
	fz_expand_rect(&big, fz_max(fz_matrix_expansion(&font->t3matrix) * 2, m));
	fz_intersect_rect(bounds, &big);

	return bounds;
}

void
fz_run_t3_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, fz_device *dev)
{
	fz_display_list *list;
	fz_matrix ctm;

	list = font->t3lists[gid];
	if (!list)
		return;

	fz_concat(&ctm, &font->t3matrix, trm);
	fz_run_display_list(ctx, list, dev, &ctm, &fz_infinite_rect, NULL);
}

fz_pixmap *
fz_render_t3_glyph_pixmap(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, fz_colorspace *model, const fz_irect *scissor)
{
	fz_display_list *list;
	fz_rect bounds;
	fz_irect bbox;
	fz_device *dev;
	fz_pixmap *glyph;
	fz_pixmap *result;

	if (gid < 0 || gid > 255)
		return NULL;

	list = font->t3lists[gid];
	if (!list)
		return NULL;

	if (font->t3flags[gid] & FZ_DEVFLAG_MASK)
	{
		if (font->t3flags[gid] & FZ_DEVFLAG_COLOR)
			fz_warn(ctx, "type3 glyph claims to be both masked and colored");
		model = NULL;
	}
	else if (font->t3flags[gid] & FZ_DEVFLAG_COLOR)
	{
		if (!model)
			fz_warn(ctx, "colored type3 glyph wanted in masked context");
	}
	else
	{
		fz_warn(ctx, "type3 glyph doesn't specify masked or colored");
		model = NULL; /* Treat as masked */
	}

	fz_expand_rect(fz_bound_glyph(ctx, font, gid, trm, &bounds), 1);
	fz_irect_from_rect(&bbox, &bounds);
	fz_intersect_irect(&bbox, scissor);

	glyph = fz_new_pixmap_with_bbox(ctx, model ? model : fz_device_gray(ctx), &bbox);
	fz_clear_pixmap(ctx, glyph);

	dev = fz_new_draw_device_type3(ctx, glyph);
	fz_try(ctx)
	{
		fz_run_t3_glyph(ctx, font, gid, trm, dev);
	}
	fz_always(ctx)
	{
		fz_drop_device(ctx, dev);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	if (!model)
	{
		fz_try(ctx)
		{
			result = fz_alpha_from_gray(ctx, glyph, 0);
		}
		fz_always(ctx)
		{
			fz_drop_pixmap(ctx, glyph);
		}
		fz_catch(ctx)
		{
			fz_rethrow(ctx);
		}
	}
	else
		result = glyph;

	return result;
}

fz_glyph *
fz_render_t3_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, fz_colorspace *model, const fz_irect *scissor)
{
	fz_pixmap *pixmap = fz_render_t3_glyph_pixmap(ctx, font, gid, trm, model, scissor);
	return fz_new_glyph_from_pixmap(ctx, pixmap);
}

void
fz_render_t3_glyph_direct(fz_context *ctx, fz_device *dev, fz_font *font, int gid, const fz_matrix *trm, void *gstate, int nested_depth)
{
	fz_matrix ctm;
	void *contents;

	if (gid < 0 || gid > 255)
		return;

	contents = font->t3procs[gid];
	if (!contents)
		return;

	if (font->t3flags[gid] & FZ_DEVFLAG_MASK)
	{
		if (font->t3flags[gid] & FZ_DEVFLAG_COLOR)
			fz_warn(ctx, "type3 glyph claims to be both masked and colored");
	}
	else if (font->t3flags[gid] & FZ_DEVFLAG_COLOR)
	{
	}
	else
	{
		fz_warn(ctx, "type3 glyph doesn't specify masked or colored");
	}

	fz_concat(&ctm, &font->t3matrix, trm);
	font->t3run(ctx, font->t3doc, font->t3resources, contents, dev, &ctm, gstate, nested_depth);
}

#ifndef NDEBUG
void
fz_print_font(fz_context *ctx, FILE *out, fz_font *font)
{
	fprintf(out, "font '%s' {\n", font->name);

	if (font->ft_face)
	{
		fprintf(out, "\tfreetype face %p\n", font->ft_face);
		if (font->ft_substitute)
			fprintf(out, "\tsubstitute font\n");
	}

	if (font->t3procs)
	{
		fprintf(out, "\ttype3 matrix [%g %g %g %g]\n",
			font->t3matrix.a, font->t3matrix.b,
			font->t3matrix.c, font->t3matrix.d);

		fprintf(out, "\ttype3 bbox [%g %g %g %g]\n",
			font->bbox.x0, font->bbox.y0,
			font->bbox.x1, font->bbox.y1);
	}

	fprintf(out, "}\n");
}
#endif

fz_rect *
fz_bound_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *trm, fz_rect *rect)
{
	if (font->bbox_table && gid < font->bbox_count)
	{
		if (fz_is_infinite_rect(&font->bbox_table[gid]))
		{
			if (font->ft_face)
				fz_bound_ft_glyph(ctx, font, gid, &font->bbox_table[gid]);
			else if (font->t3lists)
				fz_bound_t3_glyph(ctx, font, gid, &font->bbox_table[gid]);
			else
				font->bbox_table[gid] = fz_empty_rect;
		}
		*rect = font->bbox_table[gid];
	}
	else
	{
		/* fall back to font bbox */
		*rect = font->bbox;
	}

	return fz_transform_rect(rect, trm);
}

fz_path *
fz_outline_glyph(fz_context *ctx, fz_font *font, int gid, const fz_matrix *ctm)
{
	if (!font->ft_face)
		return NULL;
	return fz_outline_ft_glyph(ctx, font, gid, ctm);
}

int fz_glyph_cacheable(fz_context *ctx, fz_font *font, int gid)
{
	if (!font->t3procs || !font->t3flags || gid < 0 || gid >= font->bbox_count)
		return 1;
	return (font->t3flags[gid] & FZ_DEVFLAG_UNCACHEABLE) == 0;
}

static float
fz_advance_ft_glyph(fz_context *ctx, fz_font *font, int gid)
{
	FT_Fixed adv;
	int mask;

	if (font->ft_substitute && font->width_table && gid < font->width_count)
		return font->width_table[gid];

	mask = FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_IGNORE_TRANSFORM;
	/* if (font->wmode)
		mask |= FT_LOAD_VERTICAL_LAYOUT; */
	fz_lock(ctx, FZ_LOCK_FREETYPE);
	FT_Get_Advance(font->ft_face, gid, mask, &adv);
	fz_unlock(ctx, FZ_LOCK_FREETYPE);
	return (float) adv / ((FT_Face)font->ft_face)->units_per_EM;
}

static float
fz_advance_t3_glyph(fz_context *ctx, fz_font *font, int gid)
{
	if (gid < 0 || gid > 255)
		return 0;
	return font->t3widths[gid];
}

float
fz_advance_glyph(fz_context *ctx, fz_font *font, int gid)
{
	if (font->ft_face)
		return fz_advance_ft_glyph(ctx, font, gid);
	if (font->t3procs)
		return fz_advance_t3_glyph(ctx, font, gid);
	return 0;
}

static int
fz_encode_ft_character(fz_context *ctx, fz_font *font, int ucs)
{
	return FT_Get_Char_Index(font->ft_face, ucs);
}

int
fz_encode_character(fz_context *ctx, fz_font *font, int ucs)
{
	if (font->ft_face)
		return fz_encode_ft_character(ctx, font, ucs);
	return ucs;
}
