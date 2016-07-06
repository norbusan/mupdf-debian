#include "mupdf/fitz.h"

struct fz_separations_s
{
	int refs;
	int num_separations;
	uint32_t disabled[(FZ_MAX_SEPARATIONS + 31) / 32];
	uint32_t equiv_rgb[FZ_MAX_SEPARATIONS];
	uint32_t equiv_cmyk[FZ_MAX_SEPARATIONS];
	char *name[FZ_MAX_SEPARATIONS];
};

fz_separations *fz_new_separations(fz_context *ctx)
{
	fz_separations *sep;

	sep = fz_malloc_struct(ctx, fz_separations);
	sep->refs = 1;

	return sep;
}

fz_separations *fz_keep_separations(fz_context *ctx, fz_separations *sep)
{
	int i;

	if (!ctx || !sep)
		return NULL;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	i = sep->refs;
	if (i > 0)
		sep->refs++;
	fz_unlock(ctx, FZ_LOCK_ALLOC);

	return sep;
}

void fz_drop_separations(fz_context *ctx, fz_separations *sep)
{
	int i;

	if (!ctx || !sep)
		return;

	fz_lock(ctx, FZ_LOCK_ALLOC);
	i = sep->refs;
	if (i > 0)
		sep->refs--;
	fz_unlock(ctx, FZ_LOCK_ALLOC);
	if (i == 1)
	{
		for (i = 0; i < sep->num_separations; i++)
			fz_free(ctx, sep->name[i]);
		fz_free(ctx, sep);
	}
}

void fz_add_separation(fz_context *ctx, fz_separations *sep, uint32_t rgb, uint32_t cmyk, char *name)
{
	int n;

	if (!ctx || !sep)
		return;

	n = sep->num_separations;
	if (n == FZ_MAX_SEPARATIONS)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Too many separations");

	sep->name[n] = fz_strdup(ctx, name);
	sep->equiv_rgb[n] = rgb;
	sep->equiv_cmyk[n] = cmyk;

	sep->num_separations++;
}

void fz_control_separation(fz_context *ctx, fz_separations *sep, int separation, int disable)
{
	int bit;

	if (!ctx || !sep)
		return;
	if (separation < 0 || separation >= sep->num_separations)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Can't disable non-existent separation");

	bit = 1<<(separation & 31);
	separation >>= 5;

	if (disable)
	{
		/* If it's already disabled, great */
		if (sep->disabled[separation] & bit)
			return;

		sep->disabled[separation] |= bit;
	}
	else
	{
		/* If it's already enabled, great */
		if ((sep->disabled[separation] & bit) == 0)
			return;

		sep->disabled[separation] &= ~bit;
	}
	/* FIXME: Could only empty images from the store, or maybe only
	 * images that depend on separations. */
	fz_empty_store(ctx);
}

int fz_separation_disabled(fz_context *ctx, fz_separations *sep, int separation)
{
	int bit;

	if (!ctx || !sep)
		return 0;
	if (separation < 0 || separation >= sep->num_separations)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Can't access non-existent separation");

	bit = 1<<(separation & 31);
	separation >>= 5;

	return ((sep->disabled[separation] & bit) != 0);
}

int fz_separations_all_enabled(fz_context *ctx, fz_separations *sep)
{
	int i;

	if (!ctx || !sep)
		return 1;

	for (i = 0; i < (FZ_MAX_SEPARATIONS + 31) / 32; i++)
		if (sep->disabled[i] != 0)
			return 0;

	return 1;
}

const char *fz_get_separation(fz_context *ctx, fz_separations *sep, int separation, uint32_t *rgb, uint32_t *cmyk)
{
	if (!ctx || !sep)
		return NULL;

	if (separation < 0 || separation >= sep->num_separations)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Can't access non-existent separation");

	if (rgb)
		*rgb = sep->equiv_rgb[separation];
	if (cmyk)
		*cmyk = sep->equiv_cmyk[separation];

	return sep->name[separation];
}

int fz_count_separations(fz_context *ctx, fz_separations *sep)
{
	if (!ctx || !sep)
		return 0;

	return sep->num_separations;
}
