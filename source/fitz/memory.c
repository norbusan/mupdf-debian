#include "mupdf/fitz.h"

/* Enable FITZ_DEBUG_LOCKING_TIMES below if you want to check the times
 * for which locks are held too. */
#ifdef FITZ_DEBUG_LOCKING
#undef FITZ_DEBUG_LOCKING_TIMES
#endif

static void *
do_scavenging_malloc(fz_context *ctx, unsigned int size)
{
	void *p;
	int phase = 0;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	do {
		p = ctx->alloc->malloc(ctx->alloc->user, size);
		if (p != NULL)
		{
			fz_unlock(ctx, FZ_LOCK_ALLOC);
			return p;
		}
	} while (fz_store_scavenge(ctx, size, &phase));
	fz_unlock(ctx, FZ_LOCK_ALLOC);

	return NULL;
}

static void *
do_scavenging_realloc(fz_context *ctx, void *p, unsigned int size)
{
	void *q;
	int phase = 0;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	do {
		q = ctx->alloc->realloc(ctx->alloc->user, p, size);
		if (q != NULL)
		{
			fz_unlock(ctx, FZ_LOCK_ALLOC);
			return q;
		}
	} while (fz_store_scavenge(ctx, size, &phase));
	fz_unlock(ctx, FZ_LOCK_ALLOC);

	return NULL;
}

void *
fz_malloc(fz_context *ctx, unsigned int size)
{
	void *p;

	if (size == 0)
		return NULL;

	p = do_scavenging_malloc(ctx, size);
	if (!p)
		fz_throw(ctx, FZ_ERROR_GENERIC, "malloc of %d bytes failed", size);
	return p;
}

void *
fz_malloc_no_throw(fz_context *ctx, unsigned int size)
{
	return do_scavenging_malloc(ctx, size);
}

void *
fz_malloc_array(fz_context *ctx, unsigned int count, unsigned int size)
{
	void *p;

	if (count == 0 || size == 0)
		return 0;

	if (count > UINT_MAX / size)
		fz_throw(ctx, FZ_ERROR_GENERIC, "malloc of array (%d x %d bytes) failed (integer overflow)", count, size);

	p = do_scavenging_malloc(ctx, count * size);
	if (!p)
		fz_throw(ctx, FZ_ERROR_GENERIC, "malloc of array (%d x %d bytes) failed", count, size);
	return p;
}

void *
fz_malloc_array_no_throw(fz_context *ctx, unsigned int count, unsigned int size)
{
	if (count == 0 || size == 0)
		return 0;

	if (count > UINT_MAX / size)
	{
		fprintf(stderr, "error: malloc of array (%d x %d bytes) failed (integer overflow)", count, size);
		return NULL;
	}

	return do_scavenging_malloc(ctx, count * size);
}

void *
fz_calloc(fz_context *ctx, unsigned int count, unsigned int size)
{
	void *p;

	if (count == 0 || size == 0)
		return 0;

	if (count > UINT_MAX / size)
	{
		fz_throw(ctx, FZ_ERROR_GENERIC, "calloc (%d x %d bytes) failed (integer overflow)", count, size);
	}

	p = do_scavenging_malloc(ctx, count * size);
	if (!p)
	{
		fz_throw(ctx, FZ_ERROR_GENERIC, "calloc (%d x %d bytes) failed", count, size);
	}
	memset(p, 0, count*size);
	return p;
}

void *
fz_calloc_no_throw(fz_context *ctx, unsigned int count, unsigned int size)
{
	void *p;

	if (count == 0 || size == 0)
		return 0;

	if (count > UINT_MAX / size)
	{
		fprintf(stderr, "error: calloc (%d x %d bytes) failed (integer overflow)\n", count, size);
		return NULL;
	}

	p = do_scavenging_malloc(ctx, count * size);
	if (p)
	{
		memset(p, 0, count*size);
	}
	return p;
}

void *
fz_resize_array(fz_context *ctx, void *p, unsigned int count, unsigned int size)
{
	void *np;

	if (count == 0 || size == 0)
	{
		fz_free(ctx, p);
		return 0;
	}

	if (count > UINT_MAX / size)
		fz_throw(ctx, FZ_ERROR_GENERIC, "resize array (%d x %d bytes) failed (integer overflow)", count, size);

	np = do_scavenging_realloc(ctx, p, count * size);
	if (!np)
		fz_throw(ctx, FZ_ERROR_GENERIC, "resize array (%d x %d bytes) failed", count, size);
	return np;
}

void *
fz_resize_array_no_throw(fz_context *ctx, void *p, unsigned int count, unsigned int size)
{
	if (count == 0 || size == 0)
	{
		fz_free(ctx, p);
		return 0;
	}

	if (count > UINT_MAX / size)
	{
		fprintf(stderr, "error: resize array (%d x %d bytes) failed (integer overflow)\n", count, size);
		return NULL;
	}

	return do_scavenging_realloc(ctx, p, count * size);
}

void
fz_free(fz_context *ctx, void *p)
{
	fz_lock(ctx, FZ_LOCK_ALLOC);
	ctx->alloc->free(ctx->alloc->user, p);
	fz_unlock(ctx, FZ_LOCK_ALLOC);
}

char *
fz_strdup(fz_context *ctx, const char *s)
{
	int len = strlen(s) + 1;
	char *ns = fz_malloc(ctx, len);
	memcpy(ns, s, len);
	return ns;
}

char *
fz_strdup_no_throw(fz_context *ctx, const char *s)
{
	int len = strlen(s) + 1;
	char *ns = fz_malloc_no_throw(ctx, len);
	if (ns)
		memcpy(ns, s, len);
	return ns;
}

static void *
fz_malloc_default(void *opaque, unsigned int size)
{
	return malloc(size);
}

static void *
fz_realloc_default(void *opaque, void *old, unsigned int size)
{
	return realloc(old, size);
}

static void
fz_free_default(void *opaque, void *ptr)
{
	free(ptr);
}

fz_alloc_context fz_alloc_default =
{
	NULL,
	fz_malloc_default,
	fz_realloc_default,
	fz_free_default
};

static void
fz_lock_default(void *user, int lock)
{
}

static void
fz_unlock_default(void *user, int lock)
{
}

fz_locks_context fz_locks_default =
{
	NULL,
	fz_lock_default,
	fz_unlock_default
};

#ifdef FITZ_DEBUG_LOCKING

enum
{
	FZ_LOCK_DEBUG_CONTEXT_MAX = 100
};

fz_context *fz_lock_debug_contexts[FZ_LOCK_DEBUG_CONTEXT_MAX];
int fz_locks_debug[FZ_LOCK_DEBUG_CONTEXT_MAX][FZ_LOCK_MAX];
#ifdef FITZ_DEBUG_LOCKING_TIMES
int fz_debug_locking_inited = 0;
int fz_lock_program_start;
int fz_lock_time[FZ_LOCK_DEBUG_CONTEXT_MAX][FZ_LOCK_MAX] = { { 0 } };
int fz_lock_taken[FZ_LOCK_DEBUG_CONTEXT_MAX][FZ_LOCK_MAX] = { { 0 } };

/* We implement our own millisecond clock, as clock() cannot be trusted
 * when threads are involved. */
static int ms_clock(void)
{
#ifdef _WIN32
	return (int)GetTickCount();
#else
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return (tp.tv_sec*1000) + (tp.tv_usec/1000);
#endif
}

static void dump_lock_times(void)
{
	int i, j;
	int prog_time = ms_clock() - fz_lock_program_start;

	for (j = 0; j < FZ_LOCK_MAX; j++)
	{
		int total = 0;
		for (i = 0; i < FZ_LOCK_DEBUG_CONTEXT_MAX; i++)
		{
			total += fz_lock_time[i][j];
		}
		printf("Lock %d held for %g seconds (%g%%)\n", j, ((double)total)/1000, 100.0*total/prog_time);
	}
	printf("Total program time %g seconds\n", ((double)prog_time)/1000);
}

#endif

static int find_context(fz_context *ctx)
{
	int i;

	for (i = 0; i < FZ_LOCK_DEBUG_CONTEXT_MAX; i++)
	{
		if (fz_lock_debug_contexts[i] == ctx)
			return i;
		if (fz_lock_debug_contexts[i] == NULL)
		{
			int gottit = 0;
			/* We've not locked on this context before, so use
			 * this one for this new context. We might have other
			 * threads trying here too though so, so claim it
			 * atomically. No one has locked on this context
			 * before, so we are safe to take the ALLOC lock. */
			ctx->locks->lock(ctx->locks->user, FZ_LOCK_ALLOC);
			/* If it's still free, then claim it as ours,
			 * otherwise we'll keep hunting. */
			if (fz_lock_debug_contexts[i] == NULL)
			{
				gottit = 1;
				fz_lock_debug_contexts[i] = ctx;
#ifdef FITZ_DEBUG_LOCKING_TIMES
				if (fz_debug_locking_inited == 0)
				{
					fz_debug_locking_inited = 1;
					fz_lock_program_start = ms_clock();
					atexit(dump_lock_times);
				}
#endif
			}
			ctx->locks->unlock(ctx->locks->user, FZ_LOCK_ALLOC);
			if (gottit)
				return i;
		}
	}
	return -1;
}

void
fz_assert_lock_held(fz_context *ctx, int lock)
{
	int idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] == 0)
		fprintf(stderr, "Lock %d not held when expected\n", lock);
}

void
fz_assert_lock_not_held(fz_context *ctx, int lock)
{
	int idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] != 0)
		fprintf(stderr, "Lock %d held when not expected\n", lock);
}

void fz_lock_debug_lock(fz_context *ctx, int lock)
{
	int i;
	int idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] != 0)
	{
		fprintf(stderr, "Attempt to take lock %d when held already!\n", lock);
	}
	for (i = lock-1; i >= 0; i--)
	{
		if (fz_locks_debug[idx][i] != 0)
		{
			fprintf(stderr, "Lock ordering violation: Attempt to take lock %d when %d held already!\n", lock, i);
		}
	}
	fz_locks_debug[idx][lock] = 1;
#ifdef FITZ_DEBUG_LOCKING_TIMES
	fz_lock_taken[idx][lock] = clock();
#endif
}

void fz_lock_debug_unlock(fz_context *ctx, int lock)
{
	int idx = find_context(ctx);
	if (idx < 0)
		return;

	if (fz_locks_debug[idx][lock] == 0)
	{
		fprintf(stderr, "Attempt to release lock %d when not held!\n", lock);
	}
	fz_locks_debug[idx][lock] = 0;
#ifdef FITZ_DEBUG_LOCKING_TIMES
	fz_lock_time[idx][lock] += clock() - fz_lock_taken[idx][lock];
#endif
}

#endif
