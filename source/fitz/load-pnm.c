#include "mupdf/fitz.h"

#include <string.h>
#include <limits.h>

enum
{
	PAM_UNKNOWN = 0,
	PAM_BW,
	PAM_BWA,
	PAM_GRAY,
	PAM_GRAYA,
	PAM_RGB,
	PAM_RGBA,
	PAM_CMYK,
	PAM_CMYKA,
};

enum
{
	TOKEN_UNKNOWN = 0,
	TOKEN_WIDTH,
	TOKEN_HEIGHT,
	TOKEN_DEPTH,
	TOKEN_MAXVAL,
	TOKEN_TUPLTYPE,
	TOKEN_ENDHDR,
};

struct info
{
	fz_colorspace *cs;
	int width, height;
	int maxval, bitdepth;
	int depth, alpha;
	int tupletype;
};

static inline int iswhiteeol(int a)
{
	switch (a) {
	case ' ': case '\t': case '\r': case '\n':
		return 1;
	}
	return 0;
}

static inline int iswhite(int a)
{
	switch (a) {
	case ' ': case '\t':
		return 1;
	}
	return 0;
}

static inline int iseol(int a)
{
	switch (a) {
	case '\r': case '\n':
		return 1;
	}
	return 0;
}

static inline int bitdepth_from_maxval(int maxval)
{
	int depth = 0;
	while (maxval)
	{
		maxval >>= 1;
		depth++;
	}
	return depth;
}

static const unsigned char *
pnm_read_white(fz_context *ctx, const unsigned char *p, const unsigned char *e, int single_line)
{
	if (e - p < 1)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot parse whitespace in pnm image");

	if (single_line)
	{
		if (!iswhiteeol(*p) && *p != '#')
			fz_throw(ctx, FZ_ERROR_GENERIC, "expected whitespace/comment in pnm image");
		while (p < e && iswhite(*p))
			p++;

		if (p < e && *p == '#')
			while (p < e && !iseol(*p))
				p++;
		if (p < e && iseol(*p))
			p++;
	}
	else
	{
		if (!iswhiteeol(*p) && *p != '#')
			fz_throw(ctx, FZ_ERROR_GENERIC, "expected whitespace in pnm image");
		while (p < e && iswhiteeol(*p))
			p++;

		while (p < e && *p == '#')
		{
			while (p < e && !iseol(*p))
				p++;

			if (p < e && iseol(*p))
				p++;

			while (p < e && iswhiteeol(*p))
				p++;

			if (p < e && iseol(*p))
				p++;
		}
	}

	return p;
}

static const unsigned char *
pnm_read_signature(fz_context *ctx, const unsigned char *p, const unsigned char *e, char *signature)
{
	if (e - p < 2)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot parse magic number in pnm image");
	if (p[0] != 'P' || p[1] < '1' || p[1] > '7')
		fz_throw(ctx, FZ_ERROR_GENERIC, "expected signature in pnm image");

	signature[0] = *p++;
	signature[1] = *p++;
	return p;
}

static const unsigned char *
pnm_read_number(fz_context *ctx, const unsigned char *p, const unsigned char *e, int *number)
{
	if (e - p < 1)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot parse number in pnm image");
	if (*p < '0' || *p > '9')
		fz_throw(ctx, FZ_ERROR_GENERIC, "expected numeric field in pnm image");

	while (p < e && *p >= '0' && *p <= '9')
	{
		*number = *number * 10 + *p - '0';
		p++;
	}

	return p;
}

static const unsigned char *
pnm_read_tupletype(fz_context *ctx, const unsigned char *p, const unsigned char *e, int *tupletype)
{
	const struct { int len; char *str; int type; } tupletypes[] =
	{
		{13, "BLACKANDWHITE", PAM_BW},
		{19, "BLACKANDWHITE_ALPHA", PAM_BWA},
		{9, "GRAYSCALE", PAM_GRAY},
		{15, "GRAYSCALE_ALPHA", PAM_GRAYA},
		{3, "RGB", PAM_RGB},
		{9, "RGB_ALPHA", PAM_RGBA},
		{4, "CMYK", PAM_CMYK},
		{10, "CMYK_ALPHA", PAM_CMYKA},
	};
	const unsigned char *s;
	int i, len;

	if (e - p < 1)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot parse tuple type in pnm image");

	s = p;
	while (!iswhiteeol(*p))
		p++;
	len = p - s;

	for (i = 0; i < nelem(tupletypes); i++)
		if (len == tupletypes[i].len && !strncmp((char *) s, tupletypes[i].str, len))
		{
			*tupletype = tupletypes[i].type;
			return p;
		}

	fz_throw(ctx, FZ_ERROR_GENERIC, "unknown tuple type in pnm image");
}

static const unsigned char *
pnm_read_token(fz_context *ctx, const unsigned char *p, const unsigned char *e, int *token)
{
	const struct { int len; char *str; int type; } tokens[] =
	{
		{5, "WIDTH", TOKEN_WIDTH},
		{6, "HEIGHT", TOKEN_HEIGHT},
		{5, "DEPTH", TOKEN_DEPTH},
		{6, "MAXVAL", TOKEN_MAXVAL},
		{8, "TUPLTYPE", TOKEN_TUPLTYPE},
		{6, "ENDHDR", TOKEN_ENDHDR},
	};
	const unsigned char *s;
	int i, len;

	if (e - p < 1)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot parse header token in pnm image");

	s = p;
	while (!iswhiteeol(*p))
		p++;
	len = p - s;

	for (i = 0; i < nelem(tokens); i++)
		if (len == tokens[i].len && !strncmp((char *) s, tokens[i].str, len))
		{
			*token = tokens[i].type;
			return p;
		}

	fz_throw(ctx, FZ_ERROR_GENERIC, "unknown header token in pnm image");
}

static int
map_color(fz_context *ctx, int color, int inmax, int outmax)
{
	float f = (float) color / inmax;
	return f * outmax;
}

static fz_pixmap *
pnm_ascii_read_image(fz_context *ctx, struct info *pnm, const unsigned char *p, const unsigned char *e, int onlymeta, int bitmap)
{
	fz_pixmap *img = NULL;

	p = pnm_read_number(ctx, p, e, &pnm->width);
	p = pnm_read_white(ctx, p, e, 0);

	if (bitmap)
	{
		p = pnm_read_number(ctx, p, e, &pnm->height);
		p = pnm_read_white(ctx, p, e, 1);
		pnm->maxval = 1;
	}
	else
	{
		p = pnm_read_number(ctx, p, e, &pnm->height);
		p = pnm_read_white(ctx, p, e, 0);
		p = pnm_read_number(ctx, p, e, &pnm->maxval);
		p = pnm_read_white(ctx, p, e, 0);
	}

	if (pnm->maxval <= 0 || pnm->maxval >= 65536)
		fz_throw(ctx, FZ_ERROR_GENERIC, "maximum sample value of out range in pnm image: %d", pnm->maxval);

	pnm->bitdepth = bitdepth_from_maxval(pnm->maxval);

	if (pnm->height <= 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "image height must be > 0");
	if (pnm->width <= 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "image width must be > 0");
	if ((unsigned int)pnm->height > UINT_MAX / pnm->width / fz_colorspace_n(ctx, pnm->cs) / (pnm->bitdepth / 8 + 1))
		fz_throw(ctx, FZ_ERROR_GENERIC, "image too large");

	if (!onlymeta)
	{
		unsigned char *dp;
		int x, y, k;
		int w, h, n;

		img = fz_new_pixmap(ctx, pnm->cs, pnm->width, pnm->height, NULL, 0);
		dp = img->samples;

		w = img->w;
		h = img->h;
		n = img->n;

		if (bitmap)
		{
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					int v = 0;
					p = pnm_read_number(ctx, p, e, &v);
					p = pnm_read_white(ctx, p, e, 0);
					*dp++ = v ? 0x00 : 0xff;
				}
			}
		}
		else
		{
			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++)
					for (k = 0; k < n; k++)
					{
						int v = 0;
						p = pnm_read_number(ctx, p, e, &v);
						p = pnm_read_white(ctx, p, e, 0);
						v = fz_clampi(v, 0, pnm->maxval);
						*dp++ = map_color(ctx, v, pnm->maxval, 255);
					}
		}
	}

	return img;
}

static fz_pixmap *
pnm_binary_read_image(fz_context *ctx, struct info *pnm, const unsigned char *p, const unsigned char *e, int onlymeta, int bitmap)
{
	fz_pixmap *img = NULL;

	p = pnm_read_number(ctx, p, e, &pnm->width);
	p = pnm_read_white(ctx, p, e, 0);

	if (bitmap)
	{
		p = pnm_read_number(ctx, p, e, &pnm->height);
		p = pnm_read_white(ctx, p, e, 1);
		pnm->maxval = 1;
	}
	else
	{
		p = pnm_read_number(ctx, p, e, &pnm->height);
		p = pnm_read_white(ctx, p, e, 0);
		p = pnm_read_number(ctx, p, e, &pnm->maxval);
		p = pnm_read_white(ctx, p, e, 1);
	}

	if (pnm->maxval <= 0 || pnm->maxval >= 65536)
		fz_throw(ctx, FZ_ERROR_GENERIC, "maximum sample value of out range in pnm image: %d", pnm->maxval);

	pnm->bitdepth = bitdepth_from_maxval(pnm->maxval);

	if (pnm->height <= 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "image height must be > 0");
	if (pnm->width <= 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "image width must be > 0");
	if ((unsigned int)pnm->height > UINT_MAX / pnm->width / fz_colorspace_n(ctx, pnm->cs) / (pnm->bitdepth / 8 + 1))
		fz_throw(ctx, FZ_ERROR_GENERIC, "image too large");

	if (!onlymeta)
	{
		unsigned char *dp;
		int x, y, k;
		int w, h, n;

		img = fz_new_pixmap(ctx, pnm->cs, pnm->width, pnm->height, NULL, 0);
		dp = img->samples;

		w = img->w;
		h = img->h;
		n = img->n;

		if (pnm->maxval == 255)
			memcpy(dp, p, w * h * n);
		else if (bitmap)
		{
			for (y = 0; y < h; y++)
			{
				for (x = 0; x < w; x++)
				{
					*dp++ = (*p & (1 << (7 - (x & 0x7)))) ? 0x00 : 0xff;
					if ((x & 0x7) == 7)
						p++;
				}
				if (w & 0x7)
					p++;
			}
		}
		else if (pnm->maxval < 255)
		{
			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++)
					for (k = 0; k < n; k++)
						*dp++ = map_color(ctx, *p++, pnm->maxval, 255);
		}
		else
		{
			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++)
					for (k = 0; k < n; k++)
					{
						*dp++ = map_color(ctx, (p[0] << 8) | p[1], pnm->maxval, 255);
						p += 2;
					}
		}
	}

	return img;
}

static const unsigned char *
pam_binary_read_header(fz_context *ctx, struct info *pnm, const unsigned char *p, const unsigned char *e)
{
	int token = TOKEN_UNKNOWN;

	while (p < e && token != TOKEN_ENDHDR)
	{
		p = pnm_read_token(ctx, p, e, &token);
		p = pnm_read_white(ctx, p, e, 0);
		switch (token)
		{
		case TOKEN_WIDTH: p = pnm_read_number(ctx, p, e, &pnm->width); break;
		case TOKEN_HEIGHT: p = pnm_read_number(ctx, p, e, &pnm->height); break;
		case TOKEN_DEPTH: p = pnm_read_number(ctx, p, e, &pnm->depth); break;
		case TOKEN_MAXVAL: p = pnm_read_number(ctx, p, e, &pnm->maxval); break;
		case TOKEN_TUPLTYPE: p = pnm_read_tupletype(ctx, p, e, &pnm->tupletype); break;
		case TOKEN_ENDHDR: break;
		default:
			   fz_throw(ctx, FZ_ERROR_GENERIC, "unknown header token in pnm image");
		}

		if (token != TOKEN_ENDHDR)
			p = pnm_read_white(ctx, p, e, 0);
	}

	return p;
}

static fz_pixmap *
pam_binary_read_image(fz_context *ctx, struct info *pnm, const unsigned char *p, const unsigned char *e, int onlymeta)
{
	fz_pixmap *img = NULL;
	int bitmap = 0;
	int minval = 1;
	int maxval = 65535;

	fz_var(img);

	p = pam_binary_read_header(ctx, pnm, p, e);

	if (pnm->tupletype == PAM_UNKNOWN)
		switch (pnm->depth)
		{
		case 1: pnm->tupletype = pnm->maxval == 1 ? PAM_BW : PAM_GRAY; break;
		case 2: pnm->tupletype = pnm->maxval == 1 ? PAM_BWA : PAM_GRAYA; break;
		case 3: pnm->tupletype = PAM_RGB; break;
		case 4: pnm->tupletype = PAM_CMYK; break;
		case 5: pnm->tupletype = PAM_CMYKA; break;
		default:
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot guess tuple type based on depth in pnm image");
		}

	if (pnm->tupletype == PAM_BW && pnm->maxval > 1)
		pnm->tupletype = PAM_GRAY;
	else if (pnm->tupletype == PAM_GRAY && pnm->maxval == 1)
		pnm->tupletype = PAM_BW;
	else if (pnm->tupletype == PAM_BWA && pnm->maxval > 1)
		pnm->tupletype = PAM_GRAYA;
	else if (pnm->tupletype == PAM_GRAYA && pnm->maxval == 1)
		pnm->tupletype = PAM_BWA;

	switch (pnm->tupletype)
	{
	case PAM_BWA:
		pnm->alpha = 1;
		/* fallthrough */
	case PAM_BW:
		pnm->cs = fz_device_gray(ctx);
		maxval = 1;
		bitmap = 1;
		break;
	case PAM_GRAYA:
		pnm->alpha = 1;
		/* fallthrough */
	case PAM_GRAY:
		pnm->cs = fz_device_gray(ctx);
		minval = 2;
		break;
	case PAM_RGBA:
		pnm->alpha = 1;
		/* fallthrough */
	case PAM_RGB:
		pnm->cs = fz_device_rgb(ctx);
		break;
	case PAM_CMYKA:
		pnm->alpha = 1;
		/* fallthrough */
	case PAM_CMYK:
		pnm->cs = fz_device_cmyk(ctx);
		break;
	default:
		fz_throw(ctx, FZ_ERROR_GENERIC, "unsupported tuple type");
	}

	if (pnm->depth != fz_colorspace_n(ctx, pnm->cs) + pnm->alpha)
		fz_throw(ctx, FZ_ERROR_GENERIC, "depth out of tuple type range");
	if (pnm->maxval < minval || pnm->maxval > maxval)
		fz_throw(ctx, FZ_ERROR_GENERIC, "maxval out of range");

	pnm->bitdepth = bitdepth_from_maxval(pnm->maxval);

	if (pnm->height <= 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "image height must be > 0");
	if (pnm->width <= 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "image width must be > 0");
	if ((unsigned int)pnm->height > UINT_MAX / pnm->width / fz_colorspace_n(ctx, pnm->cs) / (pnm->bitdepth / 8 + 1))
		fz_throw(ctx, FZ_ERROR_GENERIC, "image too large");

	if (!onlymeta)
	{
		unsigned char *dp;
		int x, y, k, packed = 0;
		int w, h, n;

		img = fz_new_pixmap(ctx, pnm->cs, pnm->width, pnm->height, NULL, pnm->alpha);
		fz_try(ctx)
		{
			dp = img->samples;

			w = img->w;
			h = img->h;
			n = img->n;

			if (pnm->maxval == 1 && e - p < w * h * n)
			{
				if (e - p < w * h * n / 8)
					fz_throw(ctx, FZ_ERROR_GENERIC, "truncated image");
				packed = 1;
			}
			else if (e - p < w * h * n * (pnm->maxval < 256 ? 1 : 2))
				fz_throw(ctx, FZ_ERROR_GENERIC, "truncated image");

			if (pnm->maxval == 255)
				memcpy(dp, p, w * h * n);
			else if (bitmap && packed)
			{
				/* some encoders incorrectly pack bits into bytes and inverts the image */
				for (y = 0; y < h; y++)
					for (x = 0; x < w; x++)
					{
						for (k = 0; k < n; k++)
						{
							*dp++ = (*p & (1 << (7 - (x & 0x7)))) ? 0x00 : 0xff;
							if ((x & 0x7) == 7)
								p++;
						}
						if (w & 0x7)
							p++;
					}
			}
			else if (bitmap)
			{
				for (y = 0; y < h; y++)
					for (x = 0; x < w; x++)
						for (k = 0; k < n; k++)
							*dp++ = *p++ ? 0xff : 0x00;
			}
			else if (pnm->maxval < 255)
			{
				for (y = 0; y < h; y++)
					for (x = 0; x < w; x++)
						for (k = 0; k < n; k++)
							*dp++ = map_color(ctx, *p++, pnm->maxval, 255);
			}
			else
			{
				for (y = 0; y < h; y++)
					for (x = 0; x < w; x++)
						for (k = 0; k < n; k++)
						{
							*dp++ = map_color(ctx, (p[0] << 8) | p[1], pnm->maxval, 255);
							p += 2;
						}
			}

			if (pnm->alpha)
				fz_premultiply_pixmap(ctx, img);
		}
		fz_catch(ctx)
		{
			fz_drop_pixmap(ctx, img);
			fz_rethrow(ctx);
		}
	}

	return img;
}

static fz_pixmap *
pnm_read_image(fz_context *ctx, struct info *pnm, const unsigned char *p, size_t total, int onlymeta)
{
	const unsigned char *e = p + total;
	char signature[3] = { 0 };

	p = pnm_read_signature(ctx, p, e, signature);
	p = pnm_read_white(ctx, p, e, 0);

	if (!strcmp(signature, "P1"))
	{
		pnm->cs = fz_device_gray(ctx);
		return pnm_ascii_read_image(ctx, pnm, p, e, onlymeta, 1);
	}
	else if (!strcmp(signature, "P2"))
	{
		pnm->cs = fz_device_gray(ctx);
		return pnm_ascii_read_image(ctx, pnm, p, e, onlymeta, 0);
	}
	else if (!strcmp(signature, "P3"))
	{
		pnm->cs = fz_device_rgb(ctx);
		return pnm_ascii_read_image(ctx, pnm, p, e, onlymeta, 0);
	}
	else if (!strcmp(signature, "P4"))
	{
		pnm->cs = fz_device_gray(ctx);
		return pnm_binary_read_image(ctx, pnm, p, e, onlymeta, 1);
	}
	else if (!strcmp(signature, "P5"))
	{
		pnm->cs = fz_device_gray(ctx);
		return pnm_binary_read_image(ctx, pnm, p, e, onlymeta, 0);
	}
	else if (!strcmp(signature, "P6"))
	{
		pnm->cs = fz_device_rgb(ctx);
		return pnm_binary_read_image(ctx, pnm, p, e, onlymeta, 0);
	}
	else if (!strcmp(signature, "P7"))
		return pam_binary_read_image(ctx, pnm, p, e, onlymeta);
	else
		fz_throw(ctx, FZ_ERROR_GENERIC, "unsupported portable anymap signature (0x%02x, 0x%02x)", signature[0], signature[1]);
}

fz_pixmap *
fz_load_pnm(fz_context *ctx, const unsigned char *p, size_t total)
{
	struct info pnm = { 0 };
	return pnm_read_image(ctx, &pnm, p, total, 0);
}

void
fz_load_pnm_info(fz_context *ctx, const unsigned char *p, size_t total, int *wp, int *hp, int *xresp, int *yresp, fz_colorspace **cspacep)
{
	struct info pnm = { 0 };
	(void) pnm_read_image(ctx, &pnm, p, total, 1);
	*cspacep = fz_keep_colorspace(ctx, pnm.cs); /* pnm.cs is a borrowed device colorspace */
	*wp = pnm.width;
	*hp = pnm.height;
	*xresp = 72;
	*yresp = 72;
}
