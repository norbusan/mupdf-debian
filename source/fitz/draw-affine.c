#include "mupdf/fitz.h"
#include "draw-imp.h"

typedef unsigned char byte;

static inline float roundup(float x)
{
	return (x < 0) ? floorf(x) : ceilf(x);
}

static inline int lerp(int a, int b, int t)
{
	return a + (((b - a) * t) >> 16);
}

static inline int bilerp(int a, int b, int c, int d, int u, int v)
{
	return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

static inline byte *sample_nearest(byte *s, int w, int h, int n, int u, int v)
{
	if (u < 0) u = 0;
	if (v < 0) v = 0;
	if (u >= w) u = w - 1;
	if (v >= h) v = h - 1;
	return s + (v * w + u) * n;
}

/* Blend premultiplied source image in constant alpha over destination */

static inline void
fz_paint_affine_alpha_N_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *hp)
{
	int k;
	int n1 = n-1;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = sample_nearest(sp, sw, sh, n, ui, vi);
			byte *b = sample_nearest(sp, sw, sh, n, ui+1, vi);
			byte *c = sample_nearest(sp, sw, sh, n, ui, vi+1);
			byte *d = sample_nearest(sp, sw, sh, n, ui+1, vi+1);
			int xa = bilerp(a[n1], b[n1], c[n1], d[n1], uf, vf);
			int t;
			xa = fz_mul255(xa, alpha);
			t = 255 - xa;
			for (k = 0; k < n1; k++)
			{
				int x = bilerp(a[k], b[k], c[k], d[k], uf, vf);
				dp[k] = fz_mul255(x, alpha) + fz_mul255(dp[k], t);
			}
			dp[n1] = xa + fz_mul255(dp[n1], t);
			if (hp)
				hp[0] = xa + fz_mul255(hp[0], t);
		}
		dp += n;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

/* Special case code for gray -> rgb */
static inline void
fz_paint_affine_alpha_g2rgb_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int alpha, byte *hp)
{
	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = sample_nearest(sp, sw, sh, 2, ui, vi);
			byte *b = sample_nearest(sp, sw, sh, 2, ui+1, vi);
			byte *c = sample_nearest(sp, sw, sh, 2, ui, vi+1);
			byte *d = sample_nearest(sp, sw, sh, 2, ui+1, vi+1);
			int y = bilerp(a[1], b[1], c[1], d[1], uf, vf);
			int x = bilerp(a[0], b[0], c[0], d[0], uf, vf);
			int t;
			x = fz_mul255(x, alpha);
			y = fz_mul255(y, alpha);
			t = 255 - y;
			dp[0] = x + fz_mul255(dp[0], t);
			dp[1] = x + fz_mul255(dp[1], t);
			dp[2] = x + fz_mul255(dp[2], t);
			dp[3] = y + fz_mul255(dp[3], t);
			if (hp)
				hp[0] = y + fz_mul255(hp[0], t);
		}
		dp += 4;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_alpha_N_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *hp)
{
	int k;
	int n1 = n-1;

	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui * n;
		sw *= n;
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				byte *sample = sp + (vi * sw);
				int a = fz_mul255(sample[n-1], alpha);
				int t = 255 - a;
				for (k = 0; k < n1; k++)
					dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
				dp[n1] = a + fz_mul255(dp[n1], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += n;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * sw * n;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				byte *sample = sp + (ui * n);
				int a = fz_mul255(sample[n-1], alpha);
				int t = 255 - a;
				for (k = 0; k < n1; k++)
					dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
				dp[n1] = a + fz_mul255(dp[n1], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += n;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				byte *sample = sp + ((vi * sw + ui) * n);
				int a = fz_mul255(sample[n-1], alpha);
				int t = 255 - a;
				for (k = 0; k < n1; k++)
					dp[k] = fz_mul255(sample[k], alpha) + fz_mul255(dp[k], t);
				dp[n1] = a + fz_mul255(dp[n1], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += n;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

static inline void
fz_paint_affine_alpha_g2rgb_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int alpha, byte *hp)
{
	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui * 2;
		sw *= 2;
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				byte *sample = sp + (vi * sw);
				int x = fz_mul255(sample[0], alpha);
				int a = fz_mul255(sample[1], alpha);
				int t = 255 - a;
				dp[0] = x + fz_mul255(dp[0], t);
				dp[1] = x + fz_mul255(dp[1], t);
				dp[2] = x + fz_mul255(dp[2], t);
				dp[3] = a + fz_mul255(dp[3], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += 4;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * sw * 2;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				byte *sample = sp + (ui * 2);
				int x = fz_mul255(sample[0], alpha);
				int a = fz_mul255(sample[1], alpha);
				int t = 255 - a;
				dp[0] = x + fz_mul255(dp[0], t);
				dp[1] = x + fz_mul255(dp[1], t);
				dp[2] = x + fz_mul255(dp[2], t);
				dp[3] = a + fz_mul255(dp[3], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += 4;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				byte *sample = sp + ((vi * sw + ui) * 2);
				int x = fz_mul255(sample[0], alpha);
				int a = fz_mul255(sample[1], alpha);
				int t = 255 - a;
				dp[0] = x + fz_mul255(dp[0], t);
				dp[1] = x + fz_mul255(dp[1], t);
				dp[2] = x + fz_mul255(dp[2], t);
				dp[3] = a + fz_mul255(dp[3], t);
				if (hp)
					hp[0] = a + fz_mul255(hp[0], t);
			}
			dp += 4;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

/* Blend premultiplied source image over destination */

static inline void
fz_paint_affine_N_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, byte *hp)
{
	int k;
	int n1 = n-1;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = sample_nearest(sp, sw, sh, n, ui, vi);
			byte *b = sample_nearest(sp, sw, sh, n, ui+1, vi);
			byte *c = sample_nearest(sp, sw, sh, n, ui, vi+1);
			byte *d = sample_nearest(sp, sw, sh, n, ui+1, vi+1);
			int y = bilerp(a[n1], b[n1], c[n1], d[n1], uf, vf);
			int t = 255 - y;
			for (k = 0; k < n1; k++)
			{
				int x = bilerp(a[k], b[k], c[k], d[k], uf, vf);
				dp[k] = x + fz_mul255(dp[k], t);
			}
			dp[n1] = y + fz_mul255(dp[n1], t);
			if (hp)
				hp[0] = y + fz_mul255(hp[0], t);
		}
		dp += n;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_solid_g2rgb_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, byte *hp)
{
	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = sample_nearest(sp, sw, sh, 2, ui, vi);
			byte *b = sample_nearest(sp, sw, sh, 2, ui+1, vi);
			byte *c = sample_nearest(sp, sw, sh, 2, ui, vi+1);
			byte *d = sample_nearest(sp, sw, sh, 2, ui+1, vi+1);
			int y = bilerp(a[1], b[1], c[1], d[1], uf, vf);
			int t = 255 - y;
			int x = bilerp(a[0], b[0], c[0], d[0], uf, vf);
			dp[0] = x + fz_mul255(dp[0], t);
			dp[1] = x + fz_mul255(dp[1], t);
			dp[2] = x + fz_mul255(dp[2], t);
			dp[3] = y + fz_mul255(dp[3], t);
			if (hp)
				hp[0] = y + fz_mul255(hp[0], t);
		}
		dp += 4;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_N_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, byte *hp)
{
	int k;
	int n1 = n-1;

	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui*n;
		sw *= n;
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				byte *sample = sp + (vi * sw);
				int a = sample[n1];
				/* If a is 0, then sample[k] = 0 for all k, as premultiplied */
				if (a != 0)
				{
					int t = 255 - a;
					if (t == 0)
					{
						if (n == 4)
						{
							*(int *)dp = *(int *)sample;
						}
						else
						{
							for (k = 0; k < n1; k++)
								dp[k] = sample[k];
							dp[n1] = a;
						}
						if (hp)
							hp[0] = a;
					}
					else
					{
						for (k = 0; k < n1; k++)
							dp[k] = sample[k] + fz_mul255(dp[k], t);
						dp[n1] = a + fz_mul255(dp[n1], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += n;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * sw * n;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				byte *sample = sp + (ui * n);
				int a = sample[n1];
				/* If a is 0, then sample[k] = 0 for all k, as premultiplied */
				if (a != 0)
				{
					int t = 255 - a;
					if (t == 0)
					{
						if (n == 4)
						{
							*(int *)dp = *(int *)sample;
						}
						else
						{
							for (k = 0; k < n1; k++)
								dp[k] = sample[k];
							dp[n1] = a;
						}
						if (hp)
							hp[0] = a;
					}
					else
					{
						for (k = 0; k < n1; k++)
							dp[k] = sample[k] + fz_mul255(dp[k], t);
						dp[n1] = a + fz_mul255(dp[n1], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += n;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				byte *sample = sp + ((vi * sw + ui) * n);
				int a = sample[n1];
				/* If a is 0, then sample[k] = 0 for all k, as premultiplied */
				if (a != 0)
				{
					int t = 255 - a;
					if (t == 0)
					{
						if (n == 4)
						{
							*(int *)dp = *(int *)sample;
						}
						else
						{
							for (k = 0; k < n1; k++)
								dp[k] = sample[k];
							dp[n1] = a;
						}
						if (hp)
							hp[0] = a;
					}
					else
					{
						for (k = 0; k < n1; k++)
							dp[k] = sample[k] + fz_mul255(dp[k], t);
						dp[n1] = a + fz_mul255(dp[n1], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += n;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

static inline void
fz_paint_affine_solid_g2rgb_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, byte *hp)
{
	if (fa == 0)
	{
		int ui = u >> 16;
		if (ui < 0 || ui >= sw)
			return;
		sp += ui * 2;
		sw *= 2;
		while (w--)
		{
			int vi = v >> 16;
			if (vi >= 0 && vi < sh)
			{
				byte *sample = sp + (vi * sw);
				int a = sample[1];
				if (a != 0)
				{
					int x = sample[0];
					int t = 255 - a;
					if (t == 0)
					{
						dp[0] = x;
						dp[1] = x;
						dp[2] = x;
						dp[3] = a;
						if (hp)
							hp[0] = a;
					}
					else
					{
						dp[0] = x + fz_mul255(dp[0], t);
						dp[1] = x + fz_mul255(dp[1], t);
						dp[2] = x + fz_mul255(dp[2], t);
						dp[3] = a + fz_mul255(dp[3], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += 4;
			if (hp)
				hp++;
			v += fb;
		}
	}
	else if (fb == 0)
	{
		int vi = v >> 16;
		if (vi < 0 || vi >= sh)
			return;
		sp += vi * sw * 2;
		while (w--)
		{
			int ui = u >> 16;
			if (ui >= 0 && ui < sw)
			{
				byte *sample = sp + (ui * 2);
				int a = sample[1];
				if (a != 0)
				{
					int x = sample[0];
					int t = 255 - a;
					if (t == 0)
					{
						dp[0] = x;
						dp[1] = x;
						dp[2] = x;
						dp[3] = a;
						if (hp)
							hp[0] = a;
					}
					else
					{
						dp[0] = x + fz_mul255(dp[0], t);
						dp[1] = x + fz_mul255(dp[1], t);
						dp[2] = x + fz_mul255(dp[2], t);
						dp[3] = a + fz_mul255(dp[3], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += 4;
			if (hp)
				hp++;
			u += fa;
		}
	}
	else
	{
		while (w--)
		{
			int ui = u >> 16;
			int vi = v >> 16;
			if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
			{
				byte *sample = sp + ((vi * sw + ui) * 2);
				int a = sample[1];
				if (a != 0)
				{
					int x = sample[0];
					int t = 255 - a;
					if (t == 0)
					{
						dp[0] = x;
						dp[1] = x;
						dp[2] = x;
						dp[3] = a;
						if (hp)
							hp[0] = a;
					}
					else
					{
						dp[0] = x + fz_mul255(dp[0], t);
						dp[1] = x + fz_mul255(dp[1], t);
						dp[2] = x + fz_mul255(dp[2], t);
						dp[3] = a + fz_mul255(dp[3], t);
						if (hp)
							hp[0] = a + fz_mul255(hp[0], t);
					}
				}
			}
			dp += 4;
			if (hp)
				hp++;
			u += fa;
			v += fb;
		}
	}
}

/* Blend non-premultiplied color in source image mask over destination */

static inline void
fz_paint_affine_color_N_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, byte *color, byte *hp)
{
	int n1 = n - 1;
	int sa = color[n1];
	int k;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int uf = u & 0xffff;
			int vf = v & 0xffff;
			byte *a = sample_nearest(sp, sw, sh, 1, ui, vi);
			byte *b = sample_nearest(sp, sw, sh, 1, ui+1, vi);
			byte *c = sample_nearest(sp, sw, sh, 1, ui, vi+1);
			byte *d = sample_nearest(sp, sw, sh, 1, ui+1, vi+1);
			int ma = bilerp(a[0], b[0], c[0], d[0], uf, vf);
			int masa = FZ_COMBINE(FZ_EXPAND(ma), sa);
			for (k = 0; k < n1; k++)
				dp[k] = FZ_BLEND(color[k], dp[k], masa);
			dp[n1] = FZ_BLEND(255, dp[n1], masa);
			if (hp)
				hp[0] = FZ_BLEND(255, hp[0], masa);
		}
		dp += n;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static inline void
fz_paint_affine_color_N_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, byte *color, byte *hp)
{
	int n1 = n-1;
	int sa = color[n1];
	int k;

	while (w--)
	{
		int ui = u >> 16;
		int vi = v >> 16;
		if (ui >= 0 && ui < sw && vi >= 0 && vi < sh)
		{
			int ma = sp[vi * sw + ui];
			int masa = FZ_COMBINE(FZ_EXPAND(ma), sa);
			for (k = 0; k < n1; k++)
				dp[k] = FZ_BLEND(color[k], dp[k], masa);
			dp[n1] = FZ_BLEND(255, dp[n1], masa);
			if (hp)
				hp[0] = FZ_BLEND(255, hp[0], masa);
		}
		dp += n;
		if (hp)
			hp++;
		u += fa;
		v += fb;
	}
}

static void
fz_paint_affine_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *color/*unused*/, byte *hp)
{
	if (alpha == 255)
	{
		switch (n)
		{
		case 1: fz_paint_affine_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 1, hp); break;
		case 2: fz_paint_affine_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 2, hp); break;
		case 4: fz_paint_affine_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 4, hp); break;
		default: fz_paint_affine_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, n, hp); break;
		}
	}
	else if (alpha > 0)
	{
		switch (n)
		{
		case 1: fz_paint_affine_alpha_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 1, alpha, hp); break;
		case 2: fz_paint_affine_alpha_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 2, alpha, hp); break;
		case 4: fz_paint_affine_alpha_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 4, alpha, hp); break;
		default: fz_paint_affine_alpha_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, n, alpha, hp); break;
		}
	}
}

static void
fz_paint_affine_g2rgb_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *color/*unused*/, byte *hp)
{
	if (alpha == 255)
	{
		fz_paint_affine_solid_g2rgb_lerp(dp, sp, sw, sh, u, v, fa, fb, w, hp);
	}
	else if (alpha > 0)
	{
		fz_paint_affine_alpha_g2rgb_lerp(dp, sp, sw, sh, u, v, fa, fb, w, alpha, hp);
	}
}

static void
fz_paint_affine_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *color/*unused */, byte *hp)
{
	if (alpha == 255)
	{
		switch (n)
		{
		case 1: fz_paint_affine_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 1, hp); break;
		case 2: fz_paint_affine_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 2, hp); break;
		case 4: fz_paint_affine_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 4, hp); break;
		default: fz_paint_affine_N_near(dp, sp, sw, sh, u, v, fa, fb, w, n, hp); break;
		}
	}
	else if (alpha > 0)
	{
		switch (n)
		{
		case 1: fz_paint_affine_alpha_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 1, alpha, hp); break;
		case 2: fz_paint_affine_alpha_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 2, alpha, hp); break;
		case 4: fz_paint_affine_alpha_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 4, alpha, hp); break;
		default: fz_paint_affine_alpha_N_near(dp, sp, sw, sh, u, v, fa, fb, w, n, alpha, hp); break;
		}
	}
}

static void
fz_paint_affine_g2rgb_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *color/*unused*/, byte *hp)
{
	if (alpha == 255)
	{
		fz_paint_affine_solid_g2rgb_near(dp, sp, sw, sh, u, v, fa, fb, w, hp);
	}
	else if (alpha > 0)
	{
		fz_paint_affine_alpha_g2rgb_near(dp, sp, sw, sh, u, v, fa, fb, w, alpha, hp);
	}
}

static void
fz_paint_affine_color_lerp(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha/*unused*/, byte *color, byte *hp)
{
	switch (n)
	{
	case 2: fz_paint_affine_color_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 2, color, hp); break;
	case 4: fz_paint_affine_color_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, 4, color, hp); break;
	default: fz_paint_affine_color_N_lerp(dp, sp, sw, sh, u, v, fa, fb, w, n, color, hp); break;
	}
}

static void
fz_paint_affine_color_near(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha/*unused*/, byte *color, byte *hp)
{
	switch (n)
	{
	case 2: fz_paint_affine_color_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 2, color, hp); break;
	case 4: fz_paint_affine_color_N_near(dp, sp, sw, sh, u, v, fa, fb, w, 4, color, hp); break;
	default: fz_paint_affine_color_N_near(dp, sp, sw, sh, u, v, fa, fb, w, n, color, hp); break;
	}
}

/* RJW: The following code was originally written to be sensitive to
 * FLT_EPSILON. Given the way the 'minimum representable difference'
 * between 2 floats changes size as we scale, we now pick a larger
 * value to ensure idempotency even with rounding problems. The
 * value we pick is still far smaller than would ever show up with
 * antialiasing.
 */
#define MY_EPSILON 0.001

void
fz_gridfit_matrix(fz_matrix *m)
{
	if (fabsf(m->b) < FLT_EPSILON && fabsf(m->c) < FLT_EPSILON)
	{
		if (m->a > 0)
		{
			float f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->e);
			if (f - m->e > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->a += m->e - f; /* width gets wider as f <= m.e */
			m->e = f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->a);
			if (m->a - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->a = f;
		}
		else if (m->a < 0)
		{
			float f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->e);
			if (m->e - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->a += m->e - f; /* width gets wider (more -ve) */
			m->e = f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->a);
			if (f - m->a > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->a = f;
		}
		if (m->d > 0)
		{
			float f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->f);
			if (f - m->f > MY_EPSILON)
				f -= 1.0; /* Ensure it moves upwards */
			m->d += m->f - f; /* width gets wider as f <= m.f */
			m->f = f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->d);
			if (m->d - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->d = f;
		}
		else if (m->d < 0)
		{
			float f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->f);
			if (m->f - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->d += m->f - f; /* width gets wider (more -ve) */
			m->f = f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->d);
			if (f - m->d > MY_EPSILON)
				f -= 1.0; /* Ensure it moves up */
			m->d = f;
		}
	}
	else if (fabsf(m->a) < FLT_EPSILON && fabsf(m->d) < FLT_EPSILON)
	{
		if (m->b > 0)
		{
			float f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->f);
			if (f - m->f > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->b += m->f - f; /* width gets wider as f <= m.f */
			m->f = f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->b);
			if (m->b - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->b = f;
		}
		else if (m->b < 0)
		{
			float f;
			/* Adjust right hand side onto pixel boundary */
			f = (float)(int)(m->f);
			if (m->f - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves right */
			m->b += m->f - f; /* width gets wider (more -ve) */
			m->f = f;
			/* Adjust left hand side onto pixel boundary */
			f = (float)(int)(m->b);
			if (f - m->b > MY_EPSILON)
				f -= 1.0; /* Ensure it moves left */
			m->b = f;
		}
		if (m->c > 0)
		{
			float f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->e);
			if (f - m->e > MY_EPSILON)
				f -= 1.0; /* Ensure it moves upwards */
			m->c += m->e - f; /* width gets wider as f <= m.e */
			m->e = f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->c);
			if (m->c - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->c = f;
		}
		else if (m->c < 0)
		{
			float f;
			/* Adjust bottom onto pixel boundary */
			f = (float)(int)(m->e);
			if (m->e - f > MY_EPSILON)
				f += 1.0; /* Ensure it moves down */
			m->c += m->e - f; /* width gets wider (more -ve) */
			m->e = f;
			/* Adjust top onto pixel boundary */
			f = (float)(int)(m->c);
			if (f - m->c > MY_EPSILON)
				f -= 1.0; /* Ensure it moves up */
			m->c = f;
		}
	}
}

/* Draw an image with an affine transform on destination */

static void
fz_paint_image_imp(fz_pixmap *dst, const fz_irect *scissor, fz_pixmap *shape, fz_pixmap *img, const fz_matrix *ctm, byte *color, int alpha, int lerp_allowed)
{
	byte *dp, *sp, *hp;
	int u, v, fa, fb, fc, fd;
	int x, y, w, h;
	int sw, sh, n, hw;
	fz_irect bbox;
	int dolerp;
	void (*paintfn)(byte *dp, byte *sp, int sw, int sh, int u, int v, int fa, int fb, int w, int n, int alpha, byte *color, byte *hp);
	fz_matrix local_ctm = *ctm;
	fz_rect rect;
	int is_rectilinear;

	/* grid fit the image */
	fz_gridfit_matrix(&local_ctm);

	/* turn on interpolation for upscaled and non-rectilinear transforms */
	dolerp = 0;
	is_rectilinear = fz_is_rectilinear(&local_ctm);
	if (!is_rectilinear)
		dolerp = lerp_allowed;
	if (sqrtf(local_ctm.a * local_ctm.a + local_ctm.b * local_ctm.b) > img->w)
		dolerp = lerp_allowed;
	if (sqrtf(local_ctm.c * local_ctm.c + local_ctm.d * local_ctm.d) > img->h)
		dolerp = lerp_allowed;

	/* except when we shouldn't, at large magnifications */
	if (!img->interpolate)
	{
		if (sqrtf(local_ctm.a * local_ctm.a + local_ctm.b * local_ctm.b) > img->w * 2)
			dolerp = 0;
		if (sqrtf(local_ctm.c * local_ctm.c + local_ctm.d * local_ctm.d) > img->h * 2)
			dolerp = 0;
	}

	rect = fz_unit_rect;
	fz_irect_from_rect(&bbox, fz_transform_rect(&rect, &local_ctm));
	fz_intersect_irect(&bbox, scissor);

	x = bbox.x0;
	if (shape && shape->x > x)
		x = shape->x;
	y = bbox.y0;
	if (shape && shape->y > y)
		y = shape->y;
	w = bbox.x1;
	if (shape && shape->x + shape->w < w)
		w = shape->x + shape->w;
	w -= x;
	h = bbox.y1;
	if (shape && shape->y + shape->h < h)
		h = shape->y + shape->h;
	h -= y;
	if (w < 0 || h < 0)
		return;

	/* map from screen space (x,y) to image space (u,v) */
	fz_pre_scale(&local_ctm, 1.0f / img->w, 1.0f / img->h);
	fz_invert_matrix(&local_ctm, &local_ctm);

	fa = (int)(local_ctm.a *= 65536.0f);
	fb = (int)(local_ctm.b *= 65536.0f);
	fc = (int)(local_ctm.c *= 65536.0f);
	fd = (int)(local_ctm.d *= 65536.0f);
	local_ctm.e *= 65536.0f;
	local_ctm.f *= 65536.0f;

	/* Calculate initial texture positions. Do a half step to start. */
	/* Bug 693021: Keep calculation in float for as long as possible to
	 * avoid overflow. */
	u = (int)((local_ctm.a * x) + (local_ctm.c * y) + local_ctm.e + ((local_ctm.a + local_ctm.c) * .5f));
	v = (int)((local_ctm.b * x) + (local_ctm.d * y) + local_ctm.f + ((local_ctm.b + local_ctm.d) * .5f));

	/* RJW: The following is voodoo. No idea why it works, but it gives
	 * the best match between scaled/unscaled/interpolated/non-interpolated
	 * that we have found. */
	if (dolerp) {
		u -= 32768;
		v -= 32768;
		if (is_rectilinear)
		{
			if (u < 0)
				u = 0;
			if (v < 0)
				v = 0;
		}
	}

	dp = dst->samples + (unsigned int)(((y - dst->y) * dst->w + (x - dst->x)) * dst->n);
	n = dst->n;
	sp = img->samples;
	sw = img->w;
	sh = img->h;
	if (shape)
	{
		hw = shape->w;
		hp = shape->samples + (unsigned int)(((y - shape->y) * hw) + x - shape->x);
	}
	else
	{
		hw = 0;
		hp = NULL;
	}

	/* TODO: if (fb == 0 && fa == 1) call fz_paint_span */

	if (dst->n == 4 && img->n == 2)
	{
		assert(!color);
		if (dolerp)
			paintfn = fz_paint_affine_g2rgb_lerp;
		else
			paintfn = fz_paint_affine_g2rgb_near;
	}
	else
	{
		if (dolerp)
		{
			if (color)
				paintfn = fz_paint_affine_color_lerp;
			else
				paintfn = fz_paint_affine_lerp;
		}
		else
		{
			if (color)
				paintfn = fz_paint_affine_color_near;
			else
				paintfn = fz_paint_affine_near;
		}
	}

	while (h--)
	{
		paintfn(dp, sp, sw, sh, u, v, fa, fb, w, n, alpha, color, hp);
		dp += dst->w * n;
		hp += hw;
		u += fc;
		v += fd;
	}
}

void
fz_paint_image_with_color(fz_pixmap *dst, const fz_irect *scissor, fz_pixmap *shape, fz_pixmap *img, const fz_matrix *ctm, byte *color, int lerp_allowed)
{
	assert(img->n == 1);
	fz_paint_image_imp(dst, scissor, shape, img, ctm, color, 255, lerp_allowed);
}

void
fz_paint_image(fz_pixmap *dst, const fz_irect *scissor, fz_pixmap *shape, fz_pixmap *img, const fz_matrix *ctm, int alpha, int lerp_allowed)
{
	assert(dst->n == img->n || (dst->n == 4 && img->n == 2));
	fz_paint_image_imp(dst, scissor, shape, img, ctm, NULL, alpha, lerp_allowed);
}
