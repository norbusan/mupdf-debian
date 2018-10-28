#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include <stdio.h>

#include "annotation-icons.h"

#define REPLACEMENT 0xB7
#define CIRCLE_MAGIC 0.551915f

static float pdf_write_border_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf)
{
	float w = pdf_annot_border(ctx, annot);
	fz_append_printf(ctx, buf, "%g w\n", w);
	return w;
}

static int pdf_write_stroke_color_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf)
{
	float color[4];
	int n;
	pdf_annot_color(ctx, annot, &n, color);
	switch (n)
	{
	default: return 0;
	case 1: fz_append_printf(ctx, buf, "%g G\n", color[0]); break;
	case 3: fz_append_printf(ctx, buf, "%g %g %g RG\n", color[0], color[1], color[2]); break;
	case 4: fz_append_printf(ctx, buf, "%g %g %g %g K\n", color[0], color[1], color[2], color[3]); break;
	}
	return 1;
}

static int pdf_write_fill_color_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf)
{
	float color[4];
	int n;
	pdf_annot_color(ctx, annot, &n, color);
	switch (n)
	{
	default: return 0;
	case 1: fz_append_printf(ctx, buf, "%g g\n", color[0]); break;
	case 3: fz_append_printf(ctx, buf, "%g %g %g rg\n", color[0], color[1], color[2]); break;
	case 4: fz_append_printf(ctx, buf, "%g %g %g %g k\n", color[0], color[1], color[2], color[3]); break;
	}
	return 1;
}

static int pdf_write_interior_fill_color_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf)
{
	float color[4];
	int n;
	pdf_annot_interior_color(ctx, annot, &n, color);
	switch (n)
	{
	default: return 0;
	case 1: fz_append_printf(ctx, buf, "%g g\n", color[0]); break;
	case 3: fz_append_printf(ctx, buf, "%g %g %g rg\n", color[0], color[1], color[2]); break;
	case 4: fz_append_printf(ctx, buf, "%g %g %g %g k\n", color[0], color[1], color[2], color[3]); break;
	}
	return 1;
}

static int pdf_write_MK_BG_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf)
{
	float color[4];
	int n;
	pdf_annot_MK_BG(ctx, annot, &n, color);
	switch (n)
	{
	default: return 0;
	case 1: fz_append_printf(ctx, buf, "%g g\n", color[0]); break;
	case 3: fz_append_printf(ctx, buf, "%g %g %g rg\n", color[0], color[1], color[2]); break;
	case 4: fz_append_printf(ctx, buf, "%g %g %g %g k\n", color[0], color[1], color[2], color[3]); break;
	}
	return 1;
}

static int pdf_write_MK_BC_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf)
{
	float color[4];
	int n;
	pdf_annot_MK_BC(ctx, annot, &n, color);
	switch (n)
	{
	default: return 0;
	case 1: fz_append_printf(ctx, buf, "%g G\n", color[0]); break;
	case 3: fz_append_printf(ctx, buf, "%g %g %g RG\n", color[0], color[1], color[2]); break;
	case 4: fz_append_printf(ctx, buf, "%g %g %g %g K\n", color[0], color[1], color[2], color[3]); break;
	}
	return 1;
}

static fz_point rotate_vector(float angle, float x, float y)
{
	float ca = cosf(angle);
	float sa = sinf(angle);
	return fz_make_point(x*ca - y*sa, x*sa + y*ca);
}

static void pdf_write_arrow_appearance(fz_context *ctx, fz_buffer *buf, fz_rect *rect, float x, float y, float dx, float dy, float w)
{
	float r = fz_max(1, w);
	float angle = atan2f(dy, dx);
	fz_point v, a, b;

	v = rotate_vector(angle, 8.8f*r, 4.5f*r);
	a = fz_make_point(x + v.x, y + v.y);
	v = rotate_vector(angle, 8.8f*r, -4.5f*r);
	b = fz_make_point(x + v.x, y + v.y);

	*rect = fz_include_point_in_rect(*rect, a);
	*rect = fz_include_point_in_rect(*rect, b);
	*rect = fz_expand_rect(*rect, w);

	fz_append_printf(ctx, buf, "%g %g m\n", a.x, a.y);
	fz_append_printf(ctx, buf, "%g %g l\n", x, y);
	fz_append_printf(ctx, buf, "%g %g l\n", b.x, b.y);
}

static void include_cap(fz_rect *rect, float x, float y, float r)
{
	rect->x0 = fz_min(rect->x0, x-r);
	rect->y0 = fz_min(rect->y0, y-r);
	rect->x1 = fz_max(rect->x1, x+r);
	rect->y1 = fz_max(rect->y1, y+r);
}

static void
pdf_write_line_cap_appearance(fz_context *ctx, fz_buffer *buf, fz_rect *rect,
		float x, float y, float dx, float dy, float w, int ic, pdf_obj *cap)
{
	if (cap == PDF_NAME(Square))
	{
		float r = fz_max(2.5f, w * 2.5f);
		fz_append_printf(ctx, buf, "%g %g %g %g re\n", x-r, y-r, r*2, r*2);
		fz_append_string(ctx, buf, ic ? "b\n" : "s\n");
		include_cap(rect, x, y, r);
	}
	else if (cap == PDF_NAME(Circle))
	{
		float r = fz_max(2.5f, w * 2.5f);
		float m = r * CIRCLE_MAGIC;
		fz_append_printf(ctx, buf, "%g %g m\n", x, y+r);
		fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", x+m, y+r, x+r, y+m, x+r, y);
		fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", x+r, y-m, x+m, y-r, x, y-r);
		fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", x-m, y-r, x-r, y-m, x-r, y);
		fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", x-r, y+m, x-m, y+r, x, y+r);
		fz_append_string(ctx, buf, ic ? "b\n" : "s\n");
		include_cap(rect, x, y, r);
	}
	else if (cap == PDF_NAME(Diamond))
	{
		float r = fz_max(2.5f, w * 2.5f);
		fz_append_printf(ctx, buf, "%g %g m\n", x, y+r);
		fz_append_printf(ctx, buf, "%g %g l\n", x+r, y);
		fz_append_printf(ctx, buf, "%g %g l\n", x, y-r);
		fz_append_printf(ctx, buf, "%g %g l\n", x-r, y);
		fz_append_string(ctx, buf, ic ? "b\n" : "s\n");
		include_cap(rect, x, y, r);
	}
	else if (cap == PDF_NAME(OpenArrow))
	{
		pdf_write_arrow_appearance(ctx, buf, rect, x, y, dx, dy, w);
		fz_append_string(ctx, buf, "S\n");
	}
	else if (cap == PDF_NAME(ClosedArrow))
	{
		pdf_write_arrow_appearance(ctx, buf, rect, x, y, dx, dy, w);
		fz_append_string(ctx, buf, ic ? "b\n" : "s\n");
	}
	/* PDF 1.5 */
	else if (cap == PDF_NAME(Butt))
	{
		float r = fz_max(3, w * 3);
		fz_point a = { x-dy*r, y+dx*r };
		fz_point b = { x+dy*r, y-dx*r };
		fz_append_printf(ctx, buf, "%g %g m\n", a.x, a.y);
		fz_append_printf(ctx, buf, "%g %g l\n", b.x, b.y);
		fz_append_string(ctx, buf, "S\n");
		*rect = fz_include_point_in_rect(*rect, a);
		*rect = fz_include_point_in_rect(*rect, b);
	}
	/* PDF 1.6 */
	else if (cap == PDF_NAME(ROpenArrow))
	{
		pdf_write_arrow_appearance(ctx, buf, rect, x, y, -dx, -dy, w);
		fz_append_string(ctx, buf, "S\n");
	}
	else if (cap == PDF_NAME(RClosedArrow))
	{
		pdf_write_arrow_appearance(ctx, buf, rect, x, y, -dx, -dy, w);
		fz_append_string(ctx, buf, ic ? "b\n" : "s\n");
	}
	else if (cap == PDF_NAME(Slash))
	{
		float r = fz_max(5, w * 5);
		float angle = atan2f(dy, dx) - (30 * FZ_PI / 180);
		fz_point a, b, v;
		v = rotate_vector(angle, 0, r);
		a = fz_make_point(x + v.x, y + v.y);
		v = rotate_vector(angle, 0, -r);
		b = fz_make_point(x + v.x, y + v.y);
		fz_append_printf(ctx, buf, "%g %g m\n", a.x, a.y);
		fz_append_printf(ctx, buf, "%g %g l\n", b.x, b.y);
		fz_append_string(ctx, buf, "S\n");
		*rect = fz_include_point_in_rect(*rect, a);
		*rect = fz_include_point_in_rect(*rect, b);
	}
}

static void
pdf_write_line_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	pdf_obj *line, *le;
	fz_point a, b;
	float w;
	int ic;

	w = pdf_write_border_appearance(ctx, annot, buf);
	pdf_write_stroke_color_appearance(ctx, annot, buf);
	ic = pdf_write_interior_fill_color_appearance(ctx, annot, buf);

	line = pdf_dict_get(ctx, annot->obj, PDF_NAME(L));
	a.x = pdf_array_get_real(ctx, line, 0);
	a.y = pdf_array_get_real(ctx, line, 1);
	b.x = pdf_array_get_real(ctx, line, 2);
	b.y = pdf_array_get_real(ctx, line, 3);

	fz_append_printf(ctx, buf, "%g %g m\n%g %g l\nS\n", a.x, a.y, b.x, b.y);

	rect->x0 = fz_min(a.x, b.x);
	rect->y0 = fz_min(a.y, b.y);
	rect->x1 = fz_max(a.x, b.x);
	rect->y1 = fz_max(a.y, b.y);

	le = pdf_dict_get(ctx, annot->obj, PDF_NAME(LE));
	if (pdf_array_len(ctx, le) == 2)
	{
		float dx = b.x - a.x;
		float dy = b.y - a.y;
		float l = sqrtf(dx*dx + dy*dy);
		pdf_write_line_cap_appearance(ctx, buf, rect, a.x, a.y, dx/l, dy/l, w, ic, pdf_array_get(ctx, le, 0));
		pdf_write_line_cap_appearance(ctx, buf, rect, b.x, b.y, -dx/l, -dy/l, w, ic, pdf_array_get(ctx, le, 1));
	}
	*rect = fz_expand_rect(*rect, fz_max(1, w));
}

static void
pdf_write_square_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	float x, y, w, h;
	float lw;
	int ic;

	lw = pdf_write_border_appearance(ctx, annot, buf);
	pdf_write_stroke_color_appearance(ctx, annot, buf);
	ic = pdf_write_interior_fill_color_appearance(ctx, annot, buf);

	x = rect->x0 + lw;
	y = rect->y0 + lw;
	w = rect->x1 - x - lw;
	h = rect->y1 - y - lw;

	fz_append_printf(ctx, buf, "%g %g %g %g re\n", x, y, w, h);
	fz_append_string(ctx, buf, ic ? "b" : "s");
}

static void
pdf_write_circle_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	float rx, ry, cx, cy, mx, my;
	float lw;
	int ic;

	lw = pdf_write_border_appearance(ctx, annot, buf);
	pdf_write_stroke_color_appearance(ctx, annot, buf);
	ic = pdf_write_interior_fill_color_appearance(ctx, annot, buf);

	rx = (rect->x1 - rect->x0) / 2 - lw;
	ry = (rect->y1 - rect->y0) / 2 - lw;
	cx = rect->x0 + lw + rx;
	cy = rect->y0 + lw + ry;
	mx = rx * CIRCLE_MAGIC;
	my = ry * CIRCLE_MAGIC;

	fz_append_printf(ctx, buf, "%g %g m\n", cx, cy+ry);
	fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", cx+mx, cy+ry, cx+rx, cy+my, cx+rx, cy);
	fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", cx+rx, cy-my, cx+mx, cy-ry, cx, cy-ry);
	fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", cx-mx, cy-ry, cx-rx, cy-my, cx-rx, cy);
	fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n", cx-rx, cy+my, cx-mx, cy+ry, cx, cy+ry);
	fz_append_string(ctx, buf, ic ? "b" : "s");
}

static void
pdf_write_polygon_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect, int close)
{
	pdf_obj *verts;
	fz_point p;
	int i, n;
	float lw;

	lw = pdf_write_border_appearance(ctx, annot, buf);
	pdf_write_stroke_color_appearance(ctx, annot, buf);

	*rect = fz_empty_rect;

	verts = pdf_dict_get(ctx, annot->obj, PDF_NAME(Vertices));
	n = pdf_array_len(ctx, verts) / 2;
	if (n > 0)
	{
		for (i = 0; i < n; ++i)
		{
			p.x = pdf_array_get_real(ctx, verts, i*2+0);
			p.y = pdf_array_get_real(ctx, verts, i*2+1);
			if (i == 0)
			{
				rect->x0 = rect->x1 = p.x;
				rect->y0 = rect->y1 = p.y;
			}
			else
				*rect = fz_include_point_in_rect(*rect, p);
			if (i == 0)
				fz_append_printf(ctx, buf, "%g %g m\n", p.x, p.y);
			else
				fz_append_printf(ctx, buf, "%g %g l\n", p.x, p.y);
		}
		fz_append_string(ctx, buf, close ? "s" : "S");
		*rect = fz_expand_rect(*rect, lw);
	}
}

static void
pdf_write_ink_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	pdf_obj *ink_list, *stroke;
	int i, n, k, m;
	float lw;
	fz_point p;

	lw = pdf_write_border_appearance(ctx, annot, buf);
	pdf_write_stroke_color_appearance(ctx, annot, buf);

	*rect = fz_empty_rect;

	ink_list = pdf_dict_get(ctx, annot->obj, PDF_NAME(InkList));
	n = pdf_array_len(ctx, ink_list);
	for (i = 0; i < n; ++i)
	{
		stroke = pdf_array_get(ctx, ink_list, i);
		m = pdf_array_len(ctx, stroke) / 2;
		for (k = 0; k < m; ++k)
		{
			p.x = pdf_array_get_real(ctx, stroke, k*2+0);
			p.y = pdf_array_get_real(ctx, stroke, k*2+1);
			if (i == 0 && k == 0)
			{
				rect->x0 = rect->x1 = p.x;
				rect->y0 = rect->y1 = p.y;
			}
			else
				*rect = fz_include_point_in_rect(*rect, p);
			fz_append_printf(ctx, buf, "%g %g %c\n", p.x, p.y, k == 0 ? 'm' : 'l');
		}
	}
	fz_append_printf(ctx, buf, "S");
	*rect = fz_expand_rect(*rect, lw);
}

/* Contrary to the specification, the points within a QuadPoint are NOT
 * ordered in a counter-clockwise fashion starting with the lower left.
 * Experiments with Adobe's implementation indicates a cross-wise
 * ordering is intended: ul, ur, ll, lr.
 */
enum { UL, UR, LL, LR };

static float
extract_quad(fz_context *ctx, fz_point *quad, pdf_obj *obj, int i)
{
	float dx, dy;
	quad[0].x = pdf_array_get_real(ctx, obj, i+0);
	quad[0].y = pdf_array_get_real(ctx, obj, i+1);
	quad[1].x = pdf_array_get_real(ctx, obj, i+2);
	quad[1].y = pdf_array_get_real(ctx, obj, i+3);
	quad[2].x = pdf_array_get_real(ctx, obj, i+4);
	quad[2].y = pdf_array_get_real(ctx, obj, i+5);
	quad[3].x = pdf_array_get_real(ctx, obj, i+6);
	quad[3].y = pdf_array_get_real(ctx, obj, i+7);
	dx = quad[UL].x - quad[LL].x;
	dy = quad[UL].y - quad[LL].y;
	return sqrtf(dx * dx + dy * dy);
}

static void
union_quad(fz_rect *rect, const fz_point quad[4], float lw)
{
	fz_rect qbox;
	qbox.x0 = fz_min(fz_min(quad[0].x, quad[1].x), fz_min(quad[2].x, quad[3].x));
	qbox.y0 = fz_min(fz_min(quad[0].y, quad[1].y), fz_min(quad[2].y, quad[3].y));
	qbox.x1 = fz_max(fz_max(quad[0].x, quad[1].x), fz_max(quad[2].x, quad[3].x));
	qbox.y1 = fz_max(fz_max(quad[0].y, quad[1].y), fz_max(quad[2].y, quad[3].y));
	*rect = fz_union_rect(*rect, fz_expand_rect(qbox, lw));
}

static fz_point
lerp_point(fz_point a, fz_point b, float t)
{
	return fz_make_point(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
}

static void
pdf_write_highlight_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect, pdf_obj **res)
{
	pdf_obj *res_egs, *res_egs_h;
	pdf_obj *qp;
	fz_point quad[4], mquad[4], v;
	float opacity, h, m, dx, dy, vn;
	int i, n;

	*rect = fz_empty_rect;

	/* /Resources << /ExtGState << /H << /Type/ExtGState /BM/Multiply /CA %g >> >> >> */
	*res = pdf_new_dict(ctx, annot->page->doc, 1);
	res_egs = pdf_dict_put_dict(ctx, *res, PDF_NAME(ExtGState), 1);
	res_egs_h = pdf_dict_put_dict(ctx, res_egs, PDF_NAME(H), 2);
	pdf_dict_put(ctx, res_egs_h, PDF_NAME(Type), PDF_NAME(ExtGState));
	pdf_dict_put(ctx, res_egs_h, PDF_NAME(BM), PDF_NAME(Multiply));
	opacity = pdf_annot_opacity(ctx, annot);
	if (opacity < 1)
		pdf_dict_put_real(ctx, res_egs_h, PDF_NAME(ca), opacity);

	pdf_write_fill_color_appearance(ctx, annot, buf);

	fz_append_printf(ctx, buf, "/H gs\n");

	qp = pdf_dict_get(ctx, annot->obj, PDF_NAME(QuadPoints));
	n = pdf_array_len(ctx, qp);
	if (n > 0)
	{
		for (i = 0; i < n; i += 8)
		{
			h = extract_quad(ctx, quad, qp, i);
			m = h / 4.2425f; /* magic number that matches adobe's appearance */
			dx = quad[LR].x - quad[LL].x;
			dy = quad[LR].y - quad[LL].y;
			vn = sqrtf(dx * dx + dy * dy);
			v = fz_make_point(dx * m / vn, dy * m / vn);

			mquad[LL].x = quad[LL].x - v.x - v.y;
			mquad[LL].y = quad[LL].y - v.y + v.x;
			mquad[UL].x = quad[UL].x - v.x + v.y;
			mquad[UL].y = quad[UL].y - v.y - v.x;
			mquad[LR].x = quad[LR].x + v.x - v.y;
			mquad[LR].y = quad[LR].y + v.y + v.x;
			mquad[UR].x = quad[UR].x + v.x + v.y;
			mquad[UR].y = quad[UR].y + v.y - v.x;

			fz_append_printf(ctx, buf, "%g %g m\n", quad[LL].x, quad[LL].y);
			fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n",
				mquad[LL].x, mquad[LL].y,
				mquad[UL].x, mquad[UL].y,
				quad[UL].x, quad[UL].y);
			fz_append_printf(ctx, buf, "%g %g l\n", quad[UR].x, quad[UR].y);
			fz_append_printf(ctx, buf, "%g %g %g %g %g %g c\n",
				mquad[UR].x, mquad[UR].y,
				mquad[LR].x, mquad[LR].y,
				quad[LR].x, quad[LR].y);
			fz_append_printf(ctx, buf, "f\n");

			union_quad(rect, quad, h/16);
			union_quad(rect, mquad, 0);
		}
	}
}

static void
pdf_write_underline_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	fz_point quad[4], a, b;
	float h;
	pdf_obj *qp;
	int i, n;

	*rect = fz_empty_rect;

	pdf_write_stroke_color_appearance(ctx, annot, buf);

	qp = pdf_dict_get(ctx, annot->obj, PDF_NAME(QuadPoints));
	n = pdf_array_len(ctx, qp);
	if (n > 0)
	{
		for (i = 0; i < n; i += 8)
		{
			/* Acrobat draws the line at 1/7 of the box width from the bottom
			 * of the box and 1/16 thick of the box width. */

			h = extract_quad(ctx, quad, qp, i);
			a = lerp_point(quad[LL], quad[UL], 1/7.0f);
			b = lerp_point(quad[LR], quad[UR], 1/7.0f);

			fz_append_printf(ctx, buf, "%g w\n", h/16);
			fz_append_printf(ctx, buf, "%g %g m\n", a.x, a.y);
			fz_append_printf(ctx, buf, "%g %g l\n", b.x, b.y);
			fz_append_printf(ctx, buf, "S\n");

			union_quad(rect, quad, h/16);
		}
	}
}

static void
pdf_write_strike_out_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	fz_point quad[4], a, b;
	float h;
	pdf_obj *qp;
	int i, n;

	pdf_write_stroke_color_appearance(ctx, annot, buf);

	qp = pdf_dict_get(ctx, annot->obj, PDF_NAME(QuadPoints));
	n = pdf_array_len(ctx, qp);
	if (n > 0)
	{
		*rect = fz_empty_rect;
		for (i = 0; i < n; i += 8)
		{
			/* Acrobat draws the line at 3/7 of the box width from the bottom
			 * of the box and 1/16 thick of the box width. */

			h = extract_quad(ctx, quad, qp, i);
			a = lerp_point(quad[LL], quad[UL], 3/7.0f);
			b = lerp_point(quad[LR], quad[UR], 3/7.0f);

			fz_append_printf(ctx, buf, "%g w\n", h/16);
			fz_append_printf(ctx, buf, "%g %g m\n", a.x, a.y);
			fz_append_printf(ctx, buf, "%g %g l\n", b.x, b.y);
			fz_append_printf(ctx, buf, "S\n");

			union_quad(rect, quad, h/16);
		}
	}
}

static void
pdf_write_squiggly_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect)
{
	fz_point quad[4], a, b, c, v;
	float h, x, w;
	pdf_obj *qp;
	int i, n;

	*rect = fz_empty_rect;

	pdf_write_stroke_color_appearance(ctx, annot, buf);

	qp = pdf_dict_get(ctx, annot->obj, PDF_NAME(QuadPoints));
	n = pdf_array_len(ctx, qp);
	if (n > 0)
	{
		for (i = 0; i < n; i += 8)
		{
			int up = 1;
			h = extract_quad(ctx, quad, qp, i);
			v = fz_make_point(quad[LR].x - quad[LL].x, quad[LR].y - quad[LL].y);
			w = sqrtf(v.x * v.x + v.y * v.y);
			x = 0;

			fz_append_printf(ctx, buf, "%g w\n", h/16);
			fz_append_printf(ctx, buf, "%g %g m\n", quad[LL].x, quad[LL].y);
			while (x < w)
			{
				x += h/7;
				a = lerp_point(quad[LL], quad[LR], x/w);
				if (up)
				{
					b = lerp_point(quad[UL], quad[UR], x/w);
					c = lerp_point(a, b, 1/7.0f);
					fz_append_printf(ctx, buf, "%g %g l\n", c.x, c.y);
				}
				else
					fz_append_printf(ctx, buf, "%g %g l\n", a.x, a.y);
				up = !up;
			}
			fz_append_printf(ctx, buf, "S\n");

			union_quad(rect, quad, h/16);
		}
	}
}

static void
pdf_write_caret_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect, fz_rect *bbox)
{
	float xc = (rect->x0 + rect->x1) / 2;
	float yc = (rect->y0 + rect->y1) / 2;

	pdf_write_fill_color_appearance(ctx, annot, buf);

	fz_append_string(ctx, buf, "0 0 m\n");
	fz_append_string(ctx, buf, "10 0 10 7 10 14 c\n");
	fz_append_string(ctx, buf, "10 7 10 0 20 0 c\n");
	fz_append_string(ctx, buf, "f");

	*rect = fz_make_rect(xc - 10, yc - 7, xc + 10, yc + 7);
	*bbox = fz_make_rect(0, 0, 20, 14);
}

static void
pdf_write_icon_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect, fz_rect *bbox)
{
	const char *name;
	float xc = (rect->x0 + rect->x1) / 2;
	float yc = (rect->y0 + rect->y1) / 2;

	if (!pdf_write_fill_color_appearance(ctx, annot, buf))
		fz_append_string(ctx, buf, "1 g\n");

	fz_append_string(ctx, buf, "1 w\n0.5 0.5 15 15 re b\n");
	fz_append_string(ctx, buf, "0 g\n1 0 0 -1 4 12 cm\n");

	name = pdf_annot_icon_name(ctx, annot);

	/* Text names */
	if (!strcmp(name, "Comment"))
		fz_append_string(ctx, buf, icon_comment);
	else if (!strcmp(name, "Key"))
		fz_append_string(ctx, buf, icon_key);
	else if (!strcmp(name, "Note"))
		fz_append_string(ctx, buf, icon_note);
	else if (!strcmp(name, "Help"))
		fz_append_string(ctx, buf, icon_help);
	else if (!strcmp(name, "NewParagraph"))
		fz_append_string(ctx, buf, icon_new_paragraph);
	else if (!strcmp(name, "Paragraph"))
		fz_append_string(ctx, buf, icon_paragraph);
	else if (!strcmp(name, "Insert"))
		fz_append_string(ctx, buf, icon_insert);

	/* FileAttachment names */
	else if (!strcmp(name, "Graph"))
		fz_append_string(ctx, buf, icon_graph);
	else if (!strcmp(name, "PushPin"))
		fz_append_string(ctx, buf, icon_push_pin);
	else if (!strcmp(name, "Paperclip"))
		fz_append_string(ctx, buf, icon_paperclip);
	else if (!strcmp(name, "Tag"))
		fz_append_string(ctx, buf, icon_tag);

	/* Sound names */
	else if (!strcmp(name, "Speaker"))
		fz_append_string(ctx, buf, icon_speaker);
	else if (!strcmp(name, "Mic"))
		fz_append_string(ctx, buf, icon_mic);

	/* Unknown */
	else
		fz_append_string(ctx, buf, icon_star);

	*rect = fz_make_rect(xc - 9, yc - 9, xc + 9, yc + 9);
	*bbox = fz_make_rect(0, 0, 16, 16);
}

static float
measure_simple_string(fz_context *ctx, fz_font *font, const char *text)
{
	float w = 0;
	while (*text)
	{
		int c, g;
		text += fz_chartorune(&c, text);
		c = pdf_winansi_from_unicode(c);
		if (c < 0) c = REPLACEMENT;
		g = fz_encode_character(ctx, font, c);
		w += fz_advance_glyph(ctx, font, g, 0);
	}
	return w;
}

static void
write_simple_string(fz_context *ctx, fz_buffer *buf, const char *a, const char *b)
{
	fz_append_byte(ctx, buf, '(');
	while (a < b)
	{
		int c;
		a += fz_chartorune(&c, a);
		c = pdf_winansi_from_unicode(c);
		if (c < 0) c = REPLACEMENT;
		if (c == '(' || c == ')' || c == '\\')
			fz_append_byte(ctx, buf, '\\');
		fz_append_byte(ctx, buf, c);
	}
	fz_append_byte(ctx, buf, ')');
}

static void
write_stamp_string(fz_context *ctx, fz_buffer *buf, fz_font *font, const char *text)
{
	write_simple_string(ctx, buf, text, text+strlen(text));
}

static void
write_stamp(fz_context *ctx, fz_buffer *buf, fz_font *font, const char *text, float y, float h)
{
	float tw = measure_simple_string(ctx, font, text) * h;
	fz_append_string(ctx, buf, "BT\n");
	fz_append_printf(ctx, buf, "/Times %g Tf\n", h);
	fz_append_printf(ctx, buf, "%g %g Td\n", (190-tw)/2, y);
	write_stamp_string(ctx, buf, font, text);
	fz_append_string(ctx, buf, " Tj\n");
	fz_append_string(ctx, buf, "ET\n");
}

static void
pdf_write_stamp_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, fz_rect *rect, fz_rect *bbox, pdf_obj **res)
{
	fz_font *font;
	pdf_obj *res_font;
	pdf_obj *name;
	float w, h, xs, ys;
	fz_matrix rotate;

	name = pdf_dict_get(ctx, annot->obj, PDF_NAME(Name));
	if (!name)
		name = PDF_NAME(Draft);

	h = rect->y1 - rect->y0;
	w = rect->x1 - rect->x0;
	xs = w / 190;
	ys = h / 50;

	font = fz_new_base14_font(ctx, "Times-Bold");
	fz_try(ctx)
	{
		/* /Resources << /Font << /Times %d 0 R >> >> */
		*res = pdf_new_dict(ctx, annot->page->doc, 1);
		res_font = pdf_dict_put_dict(ctx, *res, PDF_NAME(Font), 1);
		pdf_dict_put_drop(ctx, res_font, PDF_NAME(Times), pdf_add_simple_font(ctx, annot->page->doc, font, 0));

		pdf_write_fill_color_appearance(ctx, annot, buf);
		pdf_write_stroke_color_appearance(ctx, annot, buf);
		rotate = fz_rotate(0.6f);
		fz_append_printf(ctx, buf, "%M cm\n", &rotate);
		fz_append_string(ctx, buf, "2 w\n2 2 186 44 re\nS\n");

		if (name == PDF_NAME(Approved))
			write_stamp(ctx, buf, font, "APPROVED", 13, 30);
		else if (name == PDF_NAME(AsIs))
			write_stamp(ctx, buf, font, "AS IS", 13, 30);
		else if (name == PDF_NAME(Confidential))
			write_stamp(ctx, buf, font, "CONFIDENTIAL", 17, 20);
		else if (name == PDF_NAME(Departmental))
			write_stamp(ctx, buf, font, "DEPARTMENTAL", 17, 20);
		else if (name == PDF_NAME(Experimental))
			write_stamp(ctx, buf, font, "EXPERIMENTAL", 17, 20);
		else if (name == PDF_NAME(Expired))
			write_stamp(ctx, buf, font, "EXPIRED", 13, 30);
		else if (name == PDF_NAME(Final))
			write_stamp(ctx, buf, font, "FINAL", 13, 30);
		else if (name == PDF_NAME(ForComment))
			write_stamp(ctx, buf, font, "FOR COMMENT", 17, 20);
		else if (name == PDF_NAME(ForPublicRelease))
		{
			write_stamp(ctx, buf, font, "FOR PUBLIC", 26, 18);
			write_stamp(ctx, buf, font, "RELEASE", 8.5f, 18);
		}
		else if (name == PDF_NAME(NotApproved))
			write_stamp(ctx, buf, font, "NOT APPROVED", 17, 20);
		else if (name == PDF_NAME(NotForPublicRelease))
		{
			write_stamp(ctx, buf, font, "NOT FOR", 26, 18);
			write_stamp(ctx, buf, font, "PUBLIC RELEASE", 8.5, 18);
		}
		else if (name == PDF_NAME(Sold))
			write_stamp(ctx, buf, font, "SOLD", 13, 30);
		else if (name == PDF_NAME(TopSecret))
			write_stamp(ctx, buf, font, "TOP SECRET", 14, 26);
		else if (name == PDF_NAME(Draft))
			write_stamp(ctx, buf, font, "DRAFT", 13, 30);
		else
			write_stamp(ctx, buf, font, pdf_to_name(ctx, name), 17, 20);
	}
	fz_always(ctx)
		fz_drop_font(ctx, font);
	fz_catch(ctx)
		fz_rethrow(ctx);

	*bbox = fz_make_rect(0, 0, 190, 50);
	if (xs > ys)
	{
		float xc = (rect->x1+rect->x0) / 2;
		rect->x0 = xc - 95 * ys;
		rect->x1 = xc + 95 * ys;
	}
	else
	{
		float yc = (rect->y1+rect->y0) / 2;
		rect->y0 = yc - 25 * xs;
		rect->y1 = yc + 25 * xs;
	}
}

static float
break_simple_string(fz_context *ctx, fz_font *font, float size, const char *a, const char **endp, float maxw)
{
	const char *space = NULL;
	float space_x, x = 0;
	int c, g;
	while (*a)
	{
		a += fz_chartorune(&c, a);
		if (c >= 256)
			c = REPLACEMENT;
		if (c == '\n' || c == '\r')
			break;
		if (c == ' ')
		{
			space = a;
			space_x = x;
		}
		g = fz_encode_character(ctx, font, c);
		x += fz_advance_glyph(ctx, font, g, 0) * size;
		if (space && x > maxw)
			return *endp = space, space_x;
	}
	return *endp = a, x;
}

static void
write_simple_string_with_quadding(fz_context *ctx, fz_buffer *buf, fz_font *font, float size,
	const char *a, float maxw, int q)
{
	const char *b;
	float px = 0, x = 0, w;
	while (*a)
	{
		w = break_simple_string(ctx, font, size, a, &b, maxw);
		if (b > a)
		{
			if (q > 0)
			{
				if (q == 1)
					x = (maxw - w) / 2;
				else
					x = (maxw - w);
				fz_append_printf(ctx, buf, "%g %g Td ", x - px, -size);
			}
			if (b[-1] == '\n' || b[-1] == '\r')
				write_simple_string(ctx, buf, a, b-1);
			else
				write_simple_string(ctx, buf, a, b);
			a = b;
			px = x;
			fz_append_string(ctx, buf, (q > 0) ? "Tj\n" : "'\n");
		}
	}
}

static void
write_comb_string(fz_context *ctx, fz_buffer *buf, const char *a, const char *b, fz_font *font, float cell_w)
{
	float gw, pad, carry = 0;
	fz_append_byte(ctx, buf, '[');
	while (a < b)
	{
		int c, g;

		a += fz_chartorune(&c, a);
		c = pdf_winansi_from_unicode(c);
		if (c < 0) c = REPLACEMENT;

		g = fz_encode_character(ctx, font, c);
		gw = fz_advance_glyph(ctx, font, g, 0) * 1000;
		pad = (cell_w - gw) / 2;
		fz_append_printf(ctx, buf, "%g", -(carry + pad));
		carry = pad;

		fz_append_byte(ctx, buf, '(');
		if (c == '(' || c == ')' || c == '\\')
			fz_append_byte(ctx, buf, '\\');
		fz_append_byte(ctx, buf, c);
		fz_append_byte(ctx, buf, ')');
	}
	fz_append_string(ctx, buf, "] TJ\n");
}

static const char *full_font_name(const char **name)
{
	if (!strcmp(*name, "Cour")) return "Courier";
	if (!strcmp(*name, "Helv")) return "Helvetica";
	if (!strcmp(*name, "TiRo")) return "Times-Roman";
	if (!strcmp(*name, "Symb")) return "Symbol";
	if (!strcmp(*name, "ZaDb")) return "ZapfDingbats";
	return *name = "Helv", "Helvetica";
}

static void
write_variable_text(fz_context *ctx, pdf_annot *annot, fz_buffer *buf, pdf_obj **res,
	const char *text, const char *fontname, float size, float color[3], int q,
	float w, float h, float xpadding, float ypadding, int multiline, int comb)
{
	pdf_obj *res_font;
	fz_font *font;
	float lineheight;
	float baseline;

	font = fz_new_base14_font(ctx, full_font_name(&fontname));
	fz_try(ctx)
	{
		w -= xpadding * 2;
		h -= ypadding * 2;

		if (size == 0)
		{
			if (multiline)
				size = 12;
			else
			{
				size = w / measure_simple_string(ctx, font, text);
				if (size > h)
					size = h;
			}
		}

		lineheight = size * 1.15f; /* empirically derived from Adobe reader */
		baseline = size * 0.8f;

		/* /Resources << /Font << /Helv %d 0 R >> >> */
		*res = pdf_new_dict(ctx, annot->page->doc, 1);
		res_font = pdf_dict_put_dict(ctx, *res, PDF_NAME(Font), 1);
		pdf_dict_puts_drop(ctx, res_font, fontname, pdf_add_simple_font(ctx, annot->page->doc, font, 0));

		fz_append_string(ctx, buf, "BT\n");
		fz_append_printf(ctx, buf, "%g %g %g rg\n", color[0], color[1], color[2]);
		fz_append_printf(ctx, buf, "/%s %g Tf\n", fontname, size);
		if (multiline)
		{
			fz_append_printf(ctx, buf, "%g TL\n", lineheight);
			fz_append_printf(ctx, buf, "%g %g Td\n", xpadding, ypadding+h);
			write_simple_string_with_quadding(ctx, buf, font, size, text, w, q);
		}
		else if (comb > 0)
		{
			float ty = (h - size) / 2;
			fz_append_printf(ctx, buf, "%g %g Td\n", xpadding, ypadding+h-baseline-ty);
			write_comb_string(ctx, buf, text, text + strlen(text), font, (w * 1000 / size) / comb);
		}
		else
		{
			float tx = 0, ty = (h - size) / 2;
			if (q > 0)
			{
				float tw = measure_simple_string(ctx, font, text) * size;
				if (q == 1)
					tx = (w - tw) / 2;
				else
					tx = (w - tw);
			}
			fz_append_printf(ctx, buf, "%g %g Td\n", xpadding+tx, ypadding+h-baseline-ty);
			write_simple_string(ctx, buf, text, text + strlen(text));
			fz_append_printf(ctx, buf, " Tj\n");
		}
		fz_append_string(ctx, buf, "ET\n");
	}
	fz_always(ctx)
		fz_drop_font(ctx, font);
	fz_catch(ctx)
		fz_rethrow(ctx);
}

static void
pdf_write_free_text_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf,
	fz_rect *rect, fz_rect *bbox, fz_matrix *matrix, pdf_obj **res)
{
	const char *font;
	float size, color[3];
	const char *text;
	float w, h, t, b;
	int q, r;

	/* /Rotate is an undocumented annotation property supported by Adobe */
	text = pdf_annot_contents(ctx, annot);
	r = pdf_dict_get_int(ctx, annot->obj, PDF_NAME(Rotate));
	q = pdf_annot_quadding(ctx, annot);
	pdf_annot_default_appearance(ctx, annot, &font, &size, color);

	w = rect->x1 - rect->x0;
	h = rect->y1 - rect->y0;
	if (r == 90 || r == 270)
		t = h, h = w, w = t;

	*matrix = fz_rotate(r);
	*bbox = fz_make_rect(0, 0, w, h);

	if (pdf_write_fill_color_appearance(ctx, annot, buf))
		fz_append_printf(ctx, buf, "0 0 %g %g re\nf\n", w, h);

	b = pdf_write_border_appearance(ctx, annot, buf);
	if (b > 0)
	{
		fz_append_printf(ctx, buf, "%g %g %g RG\n", color[0], color[1], color[2]);
		fz_append_printf(ctx, buf, "%g %g %g %g re\nS\n", b/2, b/2, w-b, h-b);
	}

	write_variable_text(ctx, annot, buf, res, text, font, size, color, q, w, h, b*2, b*2, 1, 0);
}

static void
pdf_write_tx_widget_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf,
	fz_rect *rect, fz_rect *bbox, fz_matrix *matrix, pdf_obj **res,
	const char *text, int ff)
{
	const char *font;
	float size, color[3];
	float w, h, t, b;
	int q, r;

	r = pdf_dict_get_int(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME(MK)), PDF_NAME(R));
	q = pdf_annot_quadding(ctx, annot);
	pdf_annot_default_appearance(ctx, annot, &font, &size, color);

	w = rect->x1 - rect->x0;
	h = rect->y1 - rect->y0;
	if (r == 90 || r == 270)
		t = h, h = w, w = t;
	*matrix = fz_rotate(r);
	*bbox = fz_make_rect(0, 0, w, h);

	fz_append_string(ctx, buf, "/Tx BMC\nq\n");

	if (pdf_write_MK_BG_appearance(ctx, annot, buf))
		fz_append_printf(ctx, buf, "0 0 %g %g re\nf\n", w, h);

	b = pdf_write_border_appearance(ctx, annot, buf);
	if (b > 0)
	{
		if (pdf_write_MK_BC_appearance(ctx, annot, buf))
			fz_append_printf(ctx, buf, "%g %g %g %g re\nS\n", b/2, b/2, w-b, h-b);
		else
			b = 0;
	}

	if (ff & PDF_TX_FIELD_IS_MULTILINE)
		write_variable_text(ctx, annot, buf, res, text, font, size, color, q, w, h, b+2, b+3, 1, 0);
	else if (ff & PDF_TX_FIELD_IS_COMB)
	{
		int maxlen = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, annot->obj, PDF_NAME(MaxLen)));
		write_variable_text(ctx, annot, buf, res, text, font, size, color, q, w, h, 0, 0, 0, maxlen);
	}
	else
		write_variable_text(ctx, annot, buf, res, text, font, size, color, q, w, h, b+2, b, 0, 0);

	fz_append_string(ctx, buf, "Q\nEMC\n");
}

static void
pdf_write_ch_widget_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf,
	fz_rect *rect, fz_rect *bbox, fz_matrix *matrix, pdf_obj **res,
	const char *text, int ff)
{
	pdf_write_tx_widget_appearance(ctx, annot, buf, rect, bbox, matrix, res, text, ff);
}

static void
pdf_write_widget_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf,
	fz_rect *rect, fz_rect *bbox, fz_matrix *matrix, pdf_obj **res)
{
	pdf_obj *ft = pdf_dict_get_inheritable(ctx, annot->obj, PDF_NAME(FT));
	int ff = pdf_get_field_flags(ctx, annot->page->doc, annot->obj);
	if (pdf_name_eq(ctx, ft, PDF_NAME(Tx)))
	{
		pdf_document *doc = annot->page->doc;
		pdf_obj *f = pdf_dict_getl(ctx, annot->obj, PDF_NAME(AA), PDF_NAME(F), NULL);
		char *text = NULL;
		if (f && doc->js)
		{
			pdf_js_event e;
			e.target = annot->obj;
			e.value = pdf_field_value(ctx, doc, annot->obj);
			fz_try(ctx)
				pdf_js_setup_event(doc->js, &e);
			fz_always(ctx)
				fz_free(ctx, e.value);
			fz_catch(ctx)
				fz_rethrow(ctx);
			pdf_execute_action(ctx, doc, annot->obj, f);
			if (pdf_js_get_event(doc->js)->rc)
				text = fz_strdup(ctx, pdf_js_get_event(doc->js)->value);
			else
				text = pdf_field_value(ctx, doc, annot->obj);
		}
		else
		{
			text = pdf_field_value(ctx, doc, annot->obj);
		}
		fz_try(ctx)
			pdf_write_tx_widget_appearance(ctx, annot, buf, rect, bbox, matrix, res, text, ff);
		fz_always(ctx)
			fz_free(ctx, text);
		fz_catch(ctx)
			fz_rethrow(ctx);
	}
	else if (pdf_name_eq(ctx, ft, PDF_NAME(Ch)))
	{
		char *text = pdf_field_value(ctx, annot->page->doc, annot->obj);
		fz_try(ctx)
			pdf_write_ch_widget_appearance(ctx, annot, buf, rect, bbox, matrix, res, text, ff);
		fz_always(ctx)
			fz_free(ctx, text);
		fz_catch(ctx)
			fz_rethrow(ctx);
	}
	else
	{
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot create appearance stream for %s widgets", pdf_to_name(ctx, ft));
	}
}

static void
pdf_write_appearance(fz_context *ctx, pdf_annot *annot, fz_buffer *buf,
	fz_rect *rect, fz_rect *bbox, fz_matrix *matrix, pdf_obj **res)
{
	switch (pdf_annot_type(ctx, annot))
	{
	default:
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot create appearance stream");
	case PDF_ANNOT_WIDGET:
		pdf_write_widget_appearance(ctx, annot, buf, rect, bbox, matrix, res);
		break;
	case PDF_ANNOT_INK:
		pdf_write_ink_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_POLYGON:
		pdf_write_polygon_appearance(ctx, annot, buf, rect, 1);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_POLY_LINE:
		pdf_write_polygon_appearance(ctx, annot, buf, rect, 0);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_LINE:
		pdf_write_line_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_SQUARE:
		pdf_write_square_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_CIRCLE:
		pdf_write_circle_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_CARET:
		pdf_write_caret_appearance(ctx, annot, buf, rect, bbox);
		*matrix = fz_identity;
		break;
	case PDF_ANNOT_TEXT:
	case PDF_ANNOT_FILE_ATTACHMENT:
	case PDF_ANNOT_SOUND:
		pdf_write_icon_appearance(ctx, annot, buf, rect, bbox);
		*matrix = fz_identity;
		break;
	case PDF_ANNOT_HIGHLIGHT:
		pdf_write_highlight_appearance(ctx, annot, buf, rect, res);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_UNDERLINE:
		pdf_write_underline_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_STRIKE_OUT:
		pdf_write_strike_out_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_SQUIGGLY:
		pdf_write_squiggly_appearance(ctx, annot, buf, rect);
		*matrix = fz_identity;
		*bbox = *rect;
		break;
	case PDF_ANNOT_STAMP:
		pdf_write_stamp_appearance(ctx, annot, buf, rect, bbox, res);
		*matrix = fz_identity;
		break;
	case PDF_ANNOT_FREE_TEXT:
		pdf_write_free_text_appearance(ctx, annot, buf, rect, bbox, matrix, res);
		break;
	}
}

void pdf_update_signature_appearance(fz_context *ctx, pdf_annot *annot, const char *name, const char *dn, const char *date)
{
	pdf_obj *ap, *new_ap_n, *res_font;
	char tmp[500];
	fz_font *helv = NULL;
	fz_font *zadb = NULL;
	pdf_obj *res = NULL;
	fz_buffer *buf;
	fz_rect rect;
	float w, h, size, name_w;

	rect = pdf_dict_get_rect(ctx, annot->obj, PDF_NAME(Rect));

	fz_var(helv);
	fz_var(zadb);
	fz_var(res);

	buf = fz_new_buffer(ctx, 1024);
	fz_try(ctx)
	{
		helv = fz_new_base14_font(ctx, "Helvetica");
		zadb = fz_new_base14_font(ctx, "ZapfDingbats");

		res = pdf_new_dict(ctx, annot->page->doc, 1);
		res_font = pdf_dict_put_dict(ctx, res, PDF_NAME(Font), 1);
		pdf_dict_put_drop(ctx, res_font, PDF_NAME(Helv), pdf_add_simple_font(ctx, annot->page->doc, helv, 0));
		pdf_dict_put_drop(ctx, res_font, PDF_NAME(ZaDb), pdf_add_simple_font(ctx, annot->page->doc, zadb, 0));

		w = (rect.x1 - rect.x0) / 2;
		h = (rect.y1 - rect.y0);

		/* Use flower symbol from ZapfDingbats as sigil */
		fz_append_printf(ctx, buf, "q 1 0.8 0.8 rg BT /ZaDb %g Tf %g %g Td (`) Tj ET Q\n",
			h*1.1f,
			rect.x0 + w - (h*0.4f),
			rect.y0 + h*0.1f);

		/* Name */
		name_w = measure_simple_string(ctx, helv, name);
		size = fz_min(fz_min((w - 4) / name_w, h), 24);
		fz_append_string(ctx, buf, "BT\n");
		fz_append_printf(ctx, buf, "/Helv %g Tf\n", size);
		fz_append_printf(ctx, buf, "%g %g Td\n", rect.x0+2, rect.y1 - size*0.8f - (h-size)/2);
		write_simple_string(ctx, buf, name, name + strlen(name));
		fz_append_string(ctx, buf, " Tj\n");
		fz_append_string(ctx, buf, "ET\n");

		/* Information text */
		size = fz_min(fz_min((w / 12), h / 6), 16);
		fz_append_string(ctx, buf, "BT\n");
		fz_append_printf(ctx, buf, "/Helv %g Tf\n", size);
		fz_append_printf(ctx, buf, "%g TL\n", size);
		fz_append_printf(ctx, buf, "%g %g Td\n", rect.x0+w+2, rect.y1);
		fz_snprintf(tmp, sizeof tmp, "Digitally signed by %s", name);
		write_simple_string_with_quadding(ctx, buf, helv, size, tmp, w-4, 0);
		fz_snprintf(tmp, sizeof tmp, "DN: %s", dn);
		write_simple_string_with_quadding(ctx, buf, helv, size, tmp, w-4, 0);
		if (date)
		{
			fz_snprintf(tmp, sizeof tmp, "Date: %s", date);
			write_simple_string_with_quadding(ctx, buf, helv, size, tmp, w-4, 0);
		}
		fz_append_string(ctx, buf, "ET\n");

		/* Update the AP/N stream */
		ap = pdf_dict_get(ctx, annot->obj, PDF_NAME(AP));
		if (!ap)
			ap = pdf_dict_put_dict(ctx, annot->obj, PDF_NAME(AP), 1);
		new_ap_n = pdf_new_xobject(ctx, annot->page->doc, rect, fz_identity, res, buf);
		pdf_dict_put(ctx, ap, PDF_NAME(N), new_ap_n);

		pdf_drop_obj(ctx, annot->ap);
		annot->ap = new_ap_n;
		annot->has_new_ap = 1;
	}
	fz_always(ctx)
	{
		fz_drop_font(ctx, helv);
		fz_drop_font(ctx, zadb);
		pdf_drop_obj(ctx, res);
		fz_drop_buffer(ctx, buf);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

void pdf_update_appearance(fz_context *ctx, pdf_annot *annot)
{
	pdf_obj *subtype;
	pdf_obj *ap, *ap_n, *as;

	subtype = pdf_dict_get(ctx, annot->obj, PDF_NAME(Subtype));
	if (subtype == PDF_NAME(Popup))
		return;
	if (subtype == PDF_NAME(Link))
		return;

	as = pdf_dict_get(ctx, annot->obj, PDF_NAME(AS));
	ap = pdf_dict_get(ctx, annot->obj, PDF_NAME(AP));
	ap_n = pdf_dict_get(ctx, ap, PDF_NAME(N));
	if (!pdf_is_stream(ctx, ap_n))
		ap_n = pdf_dict_get(ctx, ap_n, as);

	if (annot->ap != ap_n)
	{
		pdf_drop_obj(ctx, annot->ap);
		annot->ap = NULL;
		if (pdf_is_stream(ctx, ap_n))
			annot->ap = pdf_keep_obj(ctx, ap_n);
		annot->has_new_ap = 1;
	}

	if (!annot->ap || annot->needs_new_ap)
	{
		fz_rect rect, bbox;
		fz_matrix matrix = fz_identity;
		fz_buffer *buf;
		pdf_obj *res = NULL;
		pdf_obj *new_ap_n = NULL;
		fz_var(res);
		fz_var(new_ap_n);

		annot->needs_new_ap = 0;

		/* Ignore Btn widgets */
		if (pdf_name_eq(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME(Subtype)), PDF_NAME(Widget)))
			if (pdf_name_eq(ctx, pdf_dict_get_inheritable(ctx, annot->obj, PDF_NAME(FT)), PDF_NAME(Btn)))
				return;

		buf = fz_new_buffer(ctx, 1024);
		fz_try(ctx)
		{
			rect = pdf_dict_get_rect(ctx, annot->obj, PDF_NAME(Rect));
			pdf_write_appearance(ctx, annot, buf, &rect, &bbox, &matrix, &res);
			pdf_dict_put_rect(ctx, annot->obj, PDF_NAME(Rect), rect);

			if (!ap_n)
			{
				if (!ap)
				{
					ap = pdf_new_dict(ctx, annot->page->doc, 1);
					pdf_dict_put_drop(ctx, annot->obj, PDF_NAME(AP), ap);
				}
				new_ap_n = pdf_new_xobject(ctx, annot->page->doc, bbox, matrix, res, buf);
				pdf_dict_put(ctx, ap, PDF_NAME(N), new_ap_n);
			}
			else
			{
				new_ap_n = pdf_keep_obj(ctx, ap_n);
				pdf_update_xobject(ctx, annot->page->doc, ap_n, bbox, matrix, res, buf);
			}

			pdf_drop_obj(ctx, annot->ap);
			annot->ap = NULL;
			annot->ap = pdf_keep_obj(ctx, new_ap_n);
			annot->has_new_ap = 1;
		}
		fz_always(ctx)
		{
			fz_drop_buffer(ctx, buf);
			pdf_drop_obj(ctx, res);
			pdf_drop_obj(ctx, new_ap_n);
		}
		fz_catch(ctx)
		{
			fz_warn(ctx, "cannot create appearance stream");
		}
	}
}

int
pdf_update_annot(fz_context *ctx, pdf_annot *annot)
{
	pdf_document *doc = annot->page->doc;
	pdf_obj *obj, *ap, *as, *n;
	int changed = 0;

	/* TODO: handle form field updates without using the annot pdf_obj dirty flag */
	obj = annot->obj;
	if (pdf_obj_is_dirty(ctx, obj))
	{
		pdf_clean_obj(ctx, obj);
		annot->needs_new_ap = 1;
	}

	pdf_update_appearance(ctx, annot);

	ap = pdf_dict_get(ctx, obj, PDF_NAME(AP));
	as = pdf_dict_get(ctx, obj, PDF_NAME(AS));

	if (pdf_is_dict(ctx, ap))
	{
		pdf_hotspot *hp = &doc->hotspot;

		n = NULL;
		if (hp->num == pdf_to_num(ctx, obj) && (hp->state & HOTSPOT_POINTER_DOWN))
			n = pdf_dict_get(ctx, ap, PDF_NAME(D)); /* down state */
		if (n == NULL)
			n = pdf_dict_get(ctx, ap, PDF_NAME(N)); /* normal state */

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

	changed = annot->has_new_ap;
	annot->has_new_ap = 0;
	return changed;
}
