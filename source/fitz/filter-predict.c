#include "mupdf/fitz.h"

/* TODO: check if this works with 16bpp images */

typedef struct fz_predict_s fz_predict;

struct fz_predict_s
{
	fz_stream *chain;

	int predictor;
	int columns;
	int colors;
	int bpc;

	int stride;
	int bpp;
	unsigned char *in;
	unsigned char *out;
	unsigned char *ref;
	unsigned char *rp, *wp;

	unsigned char buffer[4096];
};

static inline int getcomponent(unsigned char *line, int x, int bpc)
{
	switch (bpc)
	{
	case 1: return (line[x >> 3] >> ( 7 - (x & 7) ) ) & 1;
	case 2: return (line[x >> 2] >> ( ( 3 - (x & 3) ) << 1 ) ) & 3;
	case 4: return (line[x >> 1] >> ( ( 1 - (x & 1) ) << 2 ) ) & 15;
	case 8: return line[x];
	case 16: return (line[x<<1]<<8)+line[(x<<1)+1];
	}
	return 0;
}

static inline void putcomponent(unsigned char *buf, int x, int bpc, int value)
{
	switch (bpc)
	{
	case 1: buf[x >> 3] |= value << (7 - (x & 7)); break;
	case 2: buf[x >> 2] |= value << ((3 - (x & 3)) << 1); break;
	case 4: buf[x >> 1] |= value << ((1 - (x & 1)) << 2); break;
	case 8: buf[x] = value; break;
	case 16: buf[x<<1] = value>>8; buf[(x<<1)+1] = value; break;
	}
}

static inline int paeth(int a, int b, int c)
{
	/* The definitions of ac and bc are correct, not a typo. */
	int ac = b - c, bc = a - c, abcc = ac + bc;
	int pa = fz_absi(ac);
	int pb = fz_absi(bc);
	int pc = fz_absi(abcc);
	return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static void
fz_predict_tiff(fz_predict *state, unsigned char *out, unsigned char *in, int len)
{
	int left[FZ_MAX_COLORS];
	int i, k;
	const int mask = (1 << state->bpc)-1;

	for (k = 0; k < state->colors; k++)
		left[k] = 0;
	memset(out, 0, state->stride);

	for (i = 0; i < state->columns; i++)
	{
		for (k = 0; k < state->colors; k++)
		{
			int a = getcomponent(in, i * state->colors + k, state->bpc);
			int b = a + left[k];
			int c = b & mask;
			putcomponent(out, i * state->colors + k, state->bpc, c);
			left[k] = c;
		}
	}
}

static void
fz_predict_png(fz_predict *state, unsigned char *out, unsigned char *in, int len, int predictor)
{
	int bpp = state->bpp;
	int i;
	unsigned char *ref = state->ref;

	if (bpp > len)
		bpp = len;

	switch (predictor)
	{
	case 0:
		memcpy(out, in, len);
		break;
	case 1:
		for (i = bpp; i > 0; i--)
		{
			*out++ = *in++;
		}
		for (i = len - bpp; i > 0; i--)
		{
			*out = *in++ + out[-bpp];
			out++;
		}
		break;
	case 2:
		for (i = bpp; i > 0; i--)
		{
			*out++ = *in++ + *ref++;
		}
		for (i = len - bpp; i > 0; i--)
		{
			*out++ = *in++ + *ref++;
		}
		break;
	case 3:
		for (i = bpp; i > 0; i--)
		{
			*out++ = *in++ + (*ref++) / 2;
		}
		for (i = len - bpp; i > 0; i--)
		{
			*out = *in++ + (out[-bpp] + *ref++) / 2;
			out++;
		}
		break;
	case 4:
		for (i = bpp; i > 0; i--)
		{
			*out++ = *in++ + paeth(0, *ref++, 0);
		}
		for (i = len - bpp; i > 0; i --)
		{
			*out = *in++ + paeth(out[-bpp], *ref, ref[-bpp]);
			ref++;
			out++;
		}
		break;
	}
}

static int
next_predict(fz_stream *stm, int len)
{
	fz_predict *state = stm->state;
	unsigned char *buf = state->buffer;
	unsigned char *p = buf;
	unsigned char *ep;
	int ispng = state->predictor >= 10;
	int n;

	if (len >= sizeof(state->buffer))
		len = sizeof(state->buffer);
	ep = buf + len;

	while (state->rp < state->wp && p < ep)
		*p++ = *state->rp++;

	while (p < ep)
	{
		n = fz_read(state->chain, state->in, state->stride + ispng);
		if (n == 0)
			break;

		if (state->predictor == 1)
			memcpy(state->out, state->in, n);
		else if (state->predictor == 2)
			fz_predict_tiff(state, state->out, state->in, n);
		else
		{
			fz_predict_png(state, state->out, state->in + 1, n - 1, state->in[0]);
			memcpy(state->ref, state->out, state->stride);
		}

		state->rp = state->out;
		state->wp = state->out + n - ispng;

		while (state->rp < state->wp && p < ep)
			*p++ = *state->rp++;
	}

	stm->rp = buf;
	stm->wp = p;
	if (stm->rp == stm->wp)
		return EOF;
	stm->pos += p - buf;

	return *stm->rp++;
}

static void
close_predict(fz_context *ctx, void *state_)
{
	fz_predict *state = (fz_predict *)state_;
	fz_close(state->chain);
	fz_free(ctx, state->in);
	fz_free(ctx, state->out);
	fz_free(ctx, state->ref);
	fz_free(ctx, state);
}

static fz_stream *
rebind_predict(fz_stream *s)
{
	fz_predict *state = s->state;
	return state->chain;
}

/* Default values: predictor = 1, columns = 1, colors = 1, bpc = 8 */
fz_stream *
fz_open_predict(fz_stream *chain, int predictor, int columns, int colors, int bpc)
{
	fz_context *ctx = chain->ctx;
	fz_predict *state = NULL;

	fz_var(state);

	if (predictor < 1)
		predictor = 1;
	if (columns < 1)
		columns = 1;
	if (colors < 1)
		colors = 1;
	if (bpc < 1)
		bpc = 8;

	fz_try(ctx)
	{
		if (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16)
			fz_throw(ctx, FZ_ERROR_GENERIC, "invalid number of bits per component: %d", bpc);
		if (colors > FZ_MAX_COLORS)
			fz_throw(ctx, FZ_ERROR_GENERIC, "too many color components (%d > %d)", colors, FZ_MAX_COLORS);
		if (columns >= INT_MAX / (bpc * colors))
			fz_throw(ctx, FZ_ERROR_GENERIC, "too many columns lead to an integer overflow (%d)", columns);

		state = fz_malloc_struct(ctx, fz_predict);
		state->in = NULL;
		state->out = NULL;
		state->chain = chain;

		state->predictor = predictor;
		state->columns = columns;
		state->colors = colors;
		state->bpc = bpc;

		if (state->predictor != 1 && state->predictor != 2 &&
			state->predictor != 10 && state->predictor != 11 &&
			state->predictor != 12 && state->predictor != 13 &&
			state->predictor != 14 && state->predictor != 15)
		{
			fz_warn(ctx, "invalid predictor: %d", state->predictor);
			state->predictor = 1;
		}

		state->stride = (state->bpc * state->colors * state->columns + 7) / 8;
		state->bpp = (state->bpc * state->colors + 7) / 8;

		state->in = fz_malloc(ctx, state->stride + 1);
		state->out = fz_malloc(ctx, state->stride);
		state->ref = fz_malloc(ctx, state->stride);
		state->rp = state->out;
		state->wp = state->out;

		memset(state->ref, 0, state->stride);
	}
	fz_catch(ctx)
	{
		if (state)
		{
			fz_free(ctx, state->in);
			fz_free(ctx, state->out);
		}
		fz_free(ctx, state);
		fz_close(chain);
		fz_rethrow(ctx);
	}

	return fz_new_stream(ctx, state, next_predict, close_predict, rebind_predict);
}
