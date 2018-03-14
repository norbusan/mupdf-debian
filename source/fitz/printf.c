#include "mupdf/fitz.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef _MSC_VER
#if _MSC_VER < 1500 /* MSVC 2008 */
int snprintf(char *s, size_t n, const char *fmt, ...)
{
		int r;
		va_list ap;
		va_start(ap, fmt);
		r = vsprintf(s, fmt, ap);
		va_end(ap);
		return r;
}
#else if _MSC_VER < 1900 /* MSVC 2015 */
#define snprintf _snprintf
#endif
#endif

static const char *fz_hex_digits = "0123456789abcdef";

struct fmtbuf
{
	fz_context *ctx;
	void *user;
	void (*emit)(fz_context *ctx, void *user, int c);
};

static inline void fmtputc(struct fmtbuf *out, int c)
{
	out->emit(out->ctx, out->user, c);
}

/*
 * Convert float to shortest possible string that won't lose precision, except:
 * NaN to 0, +Inf to FLT_MAX, -Inf to -FLT_MAX.
 */
static void fmtfloat(struct fmtbuf *out, float f)
{
	char digits[40], *s = digits;
	int exp, ndigits, point;

	if (isnan(f)) f = 0;
	if (isinf(f)) f = f < 0 ? -FLT_MAX : FLT_MAX;

	if (signbit(f))
		fmtputc(out, '-');

	if (f == 0)
	{
		fmtputc(out, '0');
		return;
	}

	ndigits = fz_grisu(f, digits, &exp);
	point = exp + ndigits;

	if (point <= 0)
	{
		fmtputc(out, '.');
		while (point++ < 0)
			fmtputc(out, '0');
		while (ndigits-- > 0)
			fmtputc(out, *s++);
	}

	else
	{
		while (ndigits-- > 0)
		{
			fmtputc(out, *s++);
			if (--point == 0 && ndigits > 0)
				fmtputc(out, '.');
		}
		while (point-- > 0)
			fmtputc(out, '0');
	}
}

static void fmtfloat_e(struct fmtbuf *out, double f, int w, int p)
{
	char buf[100], *s = buf;
	snprintf(buf, sizeof buf, "%*.*e", w, p, f);
	while (*s)
		fmtputc(out, *s++);
}

static void fmtfloat_f(struct fmtbuf *out, double f, int w, int p)
{
	char buf[100], *s = buf;
	snprintf(buf, sizeof buf, "%*.*f", w, p, f);
	while (*s)
		fmtputc(out, *s++);
}

static void fmtuint32(struct fmtbuf *out, unsigned int a, int s, int z, int w, int base)
{
	char buf[40];
	int i;

	i = 0;
	if (a == 0)
		buf[i++] = '0';
	while (a) {
		buf[i++] = fz_hex_digits[a % base];
		a /= base;
	}
	if (z == '0')
		while (i < w - !!s)
			buf[i++] = z;
	if (s)
		buf[i++] = s;
	while (i < w)
		buf[i++] = z;
	if (z == ' ')
		while (i < w)
			buf[i++] = z;
	while (i > 0)
		fmtputc(out, buf[--i]);
}

static void fmtuint64(struct fmtbuf *out, uint64_t a, int s, int z, int w, int base)
{
	char buf[80];
	int i;

	i = 0;
	if (a == 0)
		buf[i++] = '0';
	while (a) {
		buf[i++] = fz_hex_digits[a % base];
		a /= base;
	}
	if (z == '0')
		while (i < w - !!s)
			buf[i++] = z;
	if (s)
	{
		buf[i++] = s;
		w += 1;
	}
	while (i < w)
		buf[i++] = z;
	if (z == ' ')
		while (i < w)
			buf[i++] = z;
	while (i > 0)
		fmtputc(out, buf[--i]);
}

static void fmtint32(struct fmtbuf *out, int value, int s, int z, int w, int base)
{
	unsigned int a;

	if (value < 0)
	{
		s = '-';
		a = -value;
	}
	else if (s)
	{
		s = '+';
		a = value;
	}
	else
	{
		s = 0;
		a = value;
	}
	fmtuint32(out, a, s, z, w, base);
}

static void fmtint64(struct fmtbuf *out, int64_t value, int s, int z, int w, int base)
{
	uint64_t a;

	if (value < 0)
	{
		s = '-';
		a = -value;
	}
	else if (s)
	{
		s = '+';
		a = value;
	}
	else
	{
		s = 0;
		a = value;
	}
	fmtuint64(out, a, s, z, w, base);
}

static void fmtquote(struct fmtbuf *out, const char *s, int sq, int eq)
{
	int c;
	fmtputc(out, sq);
	while ((c = *s++) != 0) {
		switch (c) {
		default:
			if (c < 32 || c > 127) {
				fmtputc(out, '\\');
				fmtputc(out, '0' + ((c >> 6) & 7));
				fmtputc(out, '0' + ((c >> 3) & 7));
				fmtputc(out, '0' + ((c) & 7));
			} else {
				if (c == sq || c == eq)
					fmtputc(out, '\\');
				fmtputc(out, c);
			}
			break;
		case '\\': fmtputc(out, '\\'); fmtputc(out, '\\'); break;
		case '\b': fmtputc(out, '\\'); fmtputc(out, 'b'); break;
		case '\f': fmtputc(out, '\\'); fmtputc(out, 'f'); break;
		case '\n': fmtputc(out, '\\'); fmtputc(out, 'n'); break;
		case '\r': fmtputc(out, '\\'); fmtputc(out, 'r'); break;
		case '\t': fmtputc(out, '\\'); fmtputc(out, 't'); break;
		}
	}
	fmtputc(out, eq);
}

void
fz_format_string(fz_context *ctx, void *user, void (*emit)(fz_context *ctx, void *user, int c), const char *fmt, va_list args)
{
	struct fmtbuf out;
	int c, s, z, p, w;
	int32_t i32;
	int64_t i64;
	const char *str;
	size_t bits;

	out.ctx = ctx;
	out.user = user;
	out.emit = emit;

	while ((c = *fmt++) != 0)
	{
		if (c == '%')
		{
			s = 0;
			z = 0;

			/* flags */
			while ((c = *fmt++) != 0)
			{
				/* sign */
				if (c == '+')
					s = 1;
				/* space padding */
				else if (c == ' ')
					z = ' ';
				/* leading zero */
				else if (c == '0')
					z = z != ' ' ? '0' : z;
				/* TODO: '-' to left justify */
				else
					break;
			}
			if (!z)
				z = ' ';
			if (c == 0)
				break;

			/* width */
			w = 0;
			if (c == '*') {
				c = *fmt++;
				w = va_arg(args, int);
			} else {
				while (c >= '0' && c <= '9') {
					w = w * 10 + c - '0';
					c = *fmt++;
				}
			}
			if (c == 0)
				break;

			/* precision */
			p = 6;
			if (c == '.') {
				c = *fmt++;
				if (c == 0)
					break;
				if (c == '*') {
					c = *fmt++;
					p = va_arg(args, int);
				} else {
					if (c >= '0' && c <= '9')
						p = 0;
					while (c >= '0' && c <= '9') {
						p = p * 10 + c - '0';
						c = *fmt++;
					}
				}
			}
			if (c == 0)
				break;

			/* lengths */
			bits = 0;
			if (c == 'l') {
				c = *fmt++;
				bits = sizeof(int64_t) * 8;
				if (c == 0)
					break;
			}
			if (c == 't') {
				c = *fmt++;
				bits = sizeof(ptrdiff_t) * 8;
				if (c == 0)
					break;
			}
			if (c == 'z') {
				c = *fmt++;
				bits = sizeof(size_t) * 8;
				if (c == 0)
					break;
			}

			switch (c) {
			default:
				fmtputc(&out, '%');
				fmtputc(&out, c);
				break;
			case '%':
				fmtputc(&out, '%');
				break;

			case 'M':
				{
					fz_matrix *matrix = va_arg(args, fz_matrix*);
					fmtfloat(&out, matrix->a); fmtputc(&out, ' ');
					fmtfloat(&out, matrix->b); fmtputc(&out, ' ');
					fmtfloat(&out, matrix->c); fmtputc(&out, ' ');
					fmtfloat(&out, matrix->d); fmtputc(&out, ' ');
					fmtfloat(&out, matrix->e); fmtputc(&out, ' ');
					fmtfloat(&out, matrix->f);
				}
				break;
			case 'R':
				{
					fz_rect *rect = va_arg(args, fz_rect*);
					fmtfloat(&out, rect->x0); fmtputc(&out, ' ');
					fmtfloat(&out, rect->y0); fmtputc(&out, ' ');
					fmtfloat(&out, rect->x1); fmtputc(&out, ' ');
					fmtfloat(&out, rect->y1);
				}
				break;
			case 'P':
				{
					fz_point *point = va_arg(args, fz_point*);
					fmtfloat(&out, point->x); fmtputc(&out, ' ');
					fmtfloat(&out, point->y);
				}
				break;

			case 'C': /* unicode char */
				c = va_arg(args, int);
				if (c < 128)
					fmtputc(&out, c);
				else {
					char buf[10];
					int i, n = fz_runetochar(buf, c);
					for (i=0; i < n; ++i)
						fmtputc(&out, buf[i]);
				}
				break;
			case 'c':
				c = va_arg(args, int);
				fmtputc(&out, c);
				break;

			case 'e':
				fmtfloat_e(&out, va_arg(args, double), w, p);
				break;
			case 'f':
				fmtfloat_f(&out, va_arg(args, double), w, p);
				break;
			case 'g':
				fmtfloat(&out, va_arg(args, double));
				break;

			case 'p':
				bits = 8 * sizeof(void *);
				w = 2 * sizeof(void *);
				fmtputc(&out, '0');
				fmtputc(&out, 'x');
				/* fallthrough */
			case 'x':
				if (bits == 64)
				{
					i64 = va_arg(args, int64_t);
					fmtuint64(&out, i64, 0, z, w, 16);
				}
				else
				{
					i32 = va_arg(args, int);
					fmtuint32(&out, i32, 0, z, w, 16);
				}
				break;
			case 'd':
				if (bits == 64)
				{
					i64 = va_arg(args, int64_t);
					fmtint64(&out, i64, s, z, w, 10);
				}
				else
				{
					i32 = va_arg(args, int);
					fmtint32(&out, i32, s, z, w, 10);
				}
				break;
			case 'u':
				if (bits == 64)
				{
					i64 = va_arg(args, int64_t);
					fmtuint64(&out, i64, 0, z, w, 10);
				}
				else
				{
					i32 = va_arg(args, int);
					fmtuint32(&out, i32, 0, z, w, 10);
				}
				break;

			case 's':
				str = va_arg(args, const char*);
				if (!str)
					str = "(null)";
				while ((c = *str++) != 0)
					fmtputc(&out, c);
				break;
			case 'q': /* quoted string */
				str = va_arg(args, const char*);
				if (!str) str = "";
				fmtquote(&out, str, '"', '"');
				break;
			case '(': /* pdf string */
				str = va_arg(args, const char*);
				if (!str) str = "";
				fmtquote(&out, str, '(', ')');
				break;
			}
		}
		else
		{
			fmtputc(&out, c);
		}
	}
}

struct snprintf_buffer
{
	char *p;
	size_t s, n;
};

static void snprintf_emit(fz_context *ctx, void *out_, int c)
{
	struct snprintf_buffer *out = out_;
	if (out->n < out->s)
		out->p[out->n] = c;
	++(out->n);
}

size_t
fz_vsnprintf(char *buffer, size_t space, const char *fmt, va_list args)
{
	struct snprintf_buffer out;
	out.p = buffer;
	out.s = space;
	out.n = 0;

	/* Note: using a NULL context is safe here */
	fz_format_string(NULL, &out, snprintf_emit, fmt, args);
	snprintf_emit(NULL, &out, 0);
	return out.n - 1;
}

size_t
fz_snprintf(char *buffer, size_t space, const char *fmt, ...)
{
	va_list ap;
	struct snprintf_buffer out;
	out.p = buffer;
	out.s = space;
	out.n = 0;

	va_start(ap, fmt);
	/* Note: using a NULL context is safe here */
	fz_format_string(NULL, &out, snprintf_emit, fmt, ap);
	snprintf_emit(NULL, &out, 0);
	va_end(ap);

	return out.n - 1;
}

char *
fz_asprintf(fz_context *ctx, const char *fmt, ...)
{
	int len;
	char *mem;
	va_list ap;
	va_start(ap, fmt);
	len = fz_vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	mem = fz_malloc(ctx, len+1);
	va_start(ap, fmt);
	fz_vsnprintf(mem, len+1, fmt, ap);
	va_end(ap);
	return mem;
}
