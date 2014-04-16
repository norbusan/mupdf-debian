#ifdef _MSC_VER

#include "mupdf/fitz.h"

#include <time.h>
#include <windows.h>

#ifndef _WINRT

#define DELTA_EPOCH_IN_MICROSECS 11644473600000000Ui64

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	FILETIME ft;
	unsigned __int64 tmpres = 0;

	if (tv)
	{
		GetSystemTimeAsFileTime(&ft);

		tmpres |= ft.dwHighDateTime;
		tmpres <<= 32;
		tmpres |= ft.dwLowDateTime;

		tmpres /= 10; /*convert into microseconds*/
		/*converting file time to unix epoch*/
		tmpres -= DELTA_EPOCH_IN_MICROSECS;
		tv->tv_sec = (long)(tmpres / 1000000UL);
		tv->tv_usec = (long)(tmpres % 1000000UL);
	}

	return 0;
}

#endif /* !_WINRT */

char *
fz_utf8_from_wchar(const wchar_t *s)
{
	const wchar_t *src = s;
	char *d;
	char *dst;
	int len = 1;

	while (*src)
	{
		len += fz_runelen(*src++);
	}

	d = malloc(len);
	if (d != NULL)
	{
		dst = d;
		src = s;
		while (*src)
		{
			dst += fz_runetochar(dst, *src++);
		}
		*dst = 0;
	}
	return d;
}

wchar_t *
fz_wchar_from_utf8(const char *s)
{
	wchar_t *d, *r;
	int c;
	r = d = malloc((strlen(s) + 1) * sizeof(wchar_t));
	if (!r)
		return NULL;
	while (*s) {
		s += fz_chartorune(&c, s);
		*d++ = c;
	}
	*d = 0;
	return r;
}

FILE *
fz_fopen_utf8(const char *name, const char *mode)
{
	wchar_t *wname, *wmode;
	FILE *file;

	wname = fz_wchar_from_utf8(name);
	if (wname == NULL)
	{
		return NULL;
	}

	wmode = fz_wchar_from_utf8(mode);
	if (wmode == NULL)
	{
		free(wname);
		return NULL;
	}

	file = _wfopen(wname, wmode);

	free(wname);
	free(wmode);
	return file;
}

char **
fz_argv_from_wargv(int argc, wchar_t **wargv)
{
	char **argv;
	int i;

	argv = calloc(argc, sizeof(char *));
	if (argv == NULL)
	{
		fprintf(stderr, "Out of memory while processing command line args!\n");
		exit(1);
	}

	for (i = 0; i < argc; i++)
	{
		argv[i] = fz_utf8_from_wchar(wargv[i]);
		if (argv[i] == NULL)
		{
			fprintf(stderr, "Out of memory while processing command line args!\n");
			exit(1);
		}
	}

	return argv;
}

void
fz_free_argv(int argc, char **argv)
{
	int i;
	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

#endif /* _MSC_VER */
