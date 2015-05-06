#include "mupdf/fitz.h"

#define MIN_BOMB (100 << 20)

int
fz_read(fz_context *ctx, fz_stream *stm, unsigned char *buf, int len)
{
	int count, n;

	count = 0;
	do
	{
		n = fz_available(ctx, stm, len);
		if (n > len)
			n = len;
		if (n == 0)
			break;

		memcpy(buf, stm->rp, n);
		stm->rp += n;
		buf += n;
		count += n;
		len -= n;
	}
	while (len > 0);

	return count;
}

fz_buffer *
fz_read_all(fz_context *ctx, fz_stream *stm, int initial)
{
	return fz_read_best(ctx, stm, initial, NULL);
}

fz_buffer *
fz_read_best(fz_context *ctx, fz_stream *stm, int initial, int *truncated)
{
	fz_buffer *buf = NULL;
	int n;

	fz_var(buf);

	if (truncated)
		*truncated = 0;

	fz_try(ctx)
	{
		if (initial < 1024)
			initial = 1024;

		buf = fz_new_buffer(ctx, initial+1);

		while (1)
		{
			if (buf->len == buf->cap)
				fz_grow_buffer(ctx, buf);

			if (buf->len >= MIN_BOMB && buf->len / 200 > initial)
			{
				fz_throw(ctx, FZ_ERROR_GENERIC, "compression bomb detected");
			}

			n = fz_read(ctx, stm, buf->data + buf->len, buf->cap - buf->len);
			if (n == 0)
				break;

			buf->len += n;
		}
	}
	fz_catch(ctx)
	{
		if (fz_caught(ctx) == FZ_ERROR_TRYLATER)
		{
			fz_drop_buffer(ctx, buf);
			fz_rethrow(ctx);
		}
		if (truncated)
		{
			*truncated = 1;
		}
		else
		{
			fz_drop_buffer(ctx, buf);
			fz_rethrow(ctx);
		}
	}

	return buf;
}

void
fz_read_line(fz_context *ctx, fz_stream *stm, char *mem, int n)
{
	char *s = mem;
	int c = EOF;
	while (n > 1)
	{
		c = fz_read_byte(ctx, stm);
		if (c == EOF)
			break;
		if (c == '\r') {
			c = fz_peek_byte(ctx, stm);
			if (c == '\n')
				fz_read_byte(ctx, stm);
			break;
		}
		if (c == '\n')
			break;
		*s++ = c;
		n--;
	}
	if (n)
		*s = '\0';
}

int
fz_tell(fz_context *ctx, fz_stream *stm)
{
	return stm->pos - (stm->wp - stm->rp);
}

void
fz_seek(fz_context *ctx, fz_stream *stm, int offset, int whence)
{
	stm->avail = 0; /* Reset bit reading */
	if (stm->seek)
	{
		if (whence == 1)
		{
			offset = fz_tell(ctx, stm) + offset;
			whence = 0;
		}
		stm->seek(ctx, stm, offset, whence);
		stm->eof = 0;
	}
	else if (whence != 2)
	{
		if (whence == 0)
			offset -= fz_tell(ctx, stm);
		if (offset < 0)
			fz_warn(ctx, "cannot seek backwards");
		/* dog slow, but rare enough */
		while (offset-- > 0)
		{
			if (fz_read_byte(ctx, stm) == EOF)
			{
				fz_warn(ctx, "seek failed");
				break;
			}
		}
	}
	else
		fz_warn(ctx, "cannot seek");
}

int fz_stream_meta(fz_context *ctx, fz_stream *stm, int key, int size, void *ptr)
{
	if (!stm || !stm->meta)
		return -1;
	return stm->meta(ctx, stm, key, size, ptr);
}

fz_buffer *
fz_read_file(fz_context *ctx, const char *filename)
{
	fz_stream *stm;
	fz_buffer *buf = NULL;

	fz_var(buf);

	stm = fz_open_file(ctx, filename);
	fz_try(ctx)
	{
		buf = fz_read_all(ctx, stm, 0);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	return buf;
}
