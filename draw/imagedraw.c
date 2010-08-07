#include "fitz.h"

#define LINEAR

typedef unsigned char byte;

static inline float roundup(float x)
{
	return (x < 0) ? floorf(x) : ceilf(x);
}

#ifdef LINEAR

static inline int lerp(int a, int b, int t)
{
	return a + (((b - a) * t) >> 16);
}

static inline int bilerp(int a, int b, int c, int d, int u, int v)
{
	return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

static inline byte *samplenearest(byte *s, int w, int h, int n, int u, int v)
{
	if (u < 0) u = 0;
	if (v < 0) v = 0;
	if (u >= w) u = w - 1;
	if (v >= h) v = h - 1;
	return s + (v * w + u) * n;
}

#endif

/* Blend premultiplied source image in constant alpha over destination */

static inline void
fz_paintaffinealphaN(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha)
{
	int k;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
#ifdef LINEAR
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = samplenearest(sp, sw, sh, n, ui, vi);
			byte *b = samplenearest(sp, sw, sh, n, ui+1, vi);
			byte *c = samplenearest(sp, sw, sh, n, ui, vi+1);
			byte *d = samplenearest(sp, sw, sh, n, ui+1, vi+1);
			int x = bilerp(a[n-1], b[n-1], c[n-1], d[n-1], uf, vf);
			int t = 255 - fz_mul255(x, alpha);
			for (k = 0; k < n; k++)
			{
				x = bilerp(a[k], b[k], c[k], d[k], uf, vf);
				dp[k] = fz_mul255(x, alpha) + fz_mul255(dp[k], t);
			}
#else
			byte *sample = sp + ((vi * sw + ui) * n);
			int t = 255 - fz_mul255(sample[n-1], alpha);
			for (k = 0; k < n; k++)
				dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
#endif
		}
		dp += n;
		u += fa;
		v += fb;
	}
}

/* Blend premultiplied source image over destination */

static inline void
fz_paintaffineN(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n)
{
	int k;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
#ifdef LINEAR
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = samplenearest(sp, sw, sh, n, ui, vi);
			byte *b = samplenearest(sp, sw, sh, n, ui+1, vi);
			byte *c = samplenearest(sp, sw, sh, n, ui, vi+1);
			byte *d = samplenearest(sp, sw, sh, n, ui+1, vi+1);
			int t = 255 - bilerp(a[n-1], b[n-1], c[n-1], d[n-1], uf, vf);
			for (k = 0; k < n; k++)
			{
				int x = bilerp(a[k], b[k], c[k], d[k], uf, vf);
				dp[k] = x + fz_mul255(dp[k], t);
			}
#else
			byte *sample = sp + ((vi * sw + ui) * n);
			int t = 255 - sample[n-1];
			for (k = 0; k < n; k++)
				dp[k] = sample[k] + fz_mul255(dp[k], t);
#endif
		}
		dp += n;
		u += fa;
		v += fb;
	}
}

/* Blend non-premultiplied color in source image mask over destination */

static inline void
fz_paintaffinecolorN(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, byte *color)
{
	int sa = color[n-1];
	int k;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
#ifdef LINEAR
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = samplenearest(sp, sw, sh, 1, ui, vi);
			byte *b = samplenearest(sp, sw, sh, 1, ui+1, vi);
			byte *c = samplenearest(sp, sw, sh, 1, ui, vi+1);
			byte *d = samplenearest(sp, sw, sh, 1, ui+1, vi+1);
			int ma = bilerp(a[0], b[0], c[0], d[0], uf, vf);
#else
			int ma = sp[vi * sw + ui];
#endif
			int masa = FZ_COMBINE(FZ_EXPAND(ma), sa);
			for (k = 0; k < n - 1; k++)
				dp[k] = FZ_BLEND(color[k], dp[k], masa);
			dp[k] = FZ_BLEND(255, dp[k], masa);
		}
		dp += n;
		u += fa;
		v += fb;
	}
}

void
fz_paintaffine(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha)
{
	if (alpha == 255)
	{
		switch (n)
		{
			case 1: fz_paintaffineN(dp, sp, sw, sh, u, v, fa, fb, w, 1); break;
			case 2: fz_paintaffineN(dp, sp, sw, sh, u, v, fa, fb, w, 2); break;
			case 4: fz_paintaffineN(dp, sp, sw, sh, u, v, fa, fb, w, 4); break;
			default: fz_paintaffineN(dp, sp, sw, sh, u, v, fa, fb, w, n); break;
		}
	}
	else if (alpha > 0)
	{
		switch (n)
		{
			case 1: fz_paintaffinealphaN(dp, sp, sw, sh, u, v, fa, fb, w, 1, alpha); break;
			case 2: fz_paintaffinealphaN(dp, sp, sw, sh, u, v, fa, fb, w, 2, alpha); break;
			case 4: fz_paintaffinealphaN(dp, sp, sw, sh, u, v, fa, fb, w, 4, alpha); break;
			default: fz_paintaffinealphaN(dp, sp, sw, sh, u, v, fa, fb, w, n, alpha); break;
		}
	}
}

void
fz_paintaffinecolor(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, byte *color)
{
	switch (n)
	{
		case 2: fz_paintaffinecolorN(dp, sp, sw, sh, u, v, fa, fb, w, 2, color); break;
		case 4: fz_paintaffinecolorN(dp, sp, sw, sh, u, v, fa, fb, w, 4, color); break;
		default: fz_paintaffinecolorN(dp, sp, sw, sh, u, v, fa, fb, w, n, color); break;
	}
}

/* Draw an image with an affine transform on destination */

static void
fz_paintimageimp(fz_pixmap *dst, fz_bbox scissor, fz_pixmap *img, fz_matrix ctm, byte *color, int alpha)
{
	byte *dp, *sp;
	int u, v, fa, fb, fc, fd;
	int x, y, w, h;
	int sw, sh, n;
	fz_matrix inv;
	fz_bbox bbox;

	/* grid fit the image */
	if (fz_isrectilinear(ctm))
	{
		ctm.a = roundup(ctm.a);
		ctm.b = roundup(ctm.b);
		ctm.c = roundup(ctm.c);
		ctm.d = roundup(ctm.d);
		ctm.e = floorf(ctm.e) + 0.5f;
		ctm.f = floorf(ctm.f) + 0.5f;
	}

	bbox = fz_roundrect(fz_transformrect(ctm, fz_unitrect));
	bbox = fz_intersectbbox(bbox, scissor);
	x = bbox.x0;
	y = bbox.y0;
	w = bbox.x1 - bbox.x0;
	h = bbox.y1 - bbox.y0;

	/* map from screen space (x,y) to image space (u,v) */
	inv = fz_scale(1.0f / img->w, -1.0f / img->h);
	inv = fz_concat(inv, fz_translate(0, 1));
	inv = fz_concat(inv, ctm);
	inv = fz_invertmatrix(inv);

	fa = inv.a * 65536;
	fb = inv.b * 65536;
	fc = inv.c * 65536;
	fd = inv.d * 65536;
	u = (fa * x) + (fc * y) + inv.e * 65536;
	v = (fb * x) + (fd * y) + inv.f * 65536;

	dp = dst->samples + ((y - dst->y) * dst->w + (x - dst->x)) * dst->n;
	n = dst->n;
	sp = img->samples;
	sw = img->w;
	sh = img->h;

	/* TODO: if (fb == 0 && fa == 1) call fz_paintspan */

	while (h--)
	{
		if (color)
			fz_paintaffinecolor(dp, sp, sw, sh, u, v, fa, fb, w, n, color);
		else
			fz_paintaffine(dp, sp, sw, sh, u, v, fa, fb, w, n, alpha);
		dp += dst->w * n;
		u += fc;
		v += fd;
	}
}

void
fz_paintimagecolor(fz_pixmap *dst, fz_bbox scissor, fz_pixmap *img, fz_matrix ctm, byte *color)
{
	assert(img->n == 1);
	fz_paintimageimp(dst, scissor, img, ctm, color, 255);
}

void
fz_paintimage(fz_pixmap *dst, fz_bbox scissor, fz_pixmap *img, fz_matrix ctm, int alpha)
{
	assert(dst->n == img->n);
	fz_paintimageimp(dst, scissor, img, ctm, nil, alpha);
}
