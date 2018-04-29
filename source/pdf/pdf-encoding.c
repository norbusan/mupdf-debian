#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include "pdf-encodings.h"
#include "pdf-glyphlist.h"

#include <string.h>
#include <stdlib.h>

void
pdf_load_encoding(const char **estrings, const char *encoding)
{
	const char * const *bstrings = NULL;
	int i;

	if (!strcmp(encoding, "StandardEncoding"))
		bstrings = pdf_standard;
	if (!strcmp(encoding, "MacRomanEncoding"))
		bstrings = pdf_mac_roman;
	if (!strcmp(encoding, "MacExpertEncoding"))
		bstrings = pdf_mac_expert;
	if (!strcmp(encoding, "WinAnsiEncoding"))
		bstrings = pdf_win_ansi;

	if (bstrings)
		for (i = 0; i < 256; i++)
			estrings[i] = bstrings[i];
}

int
pdf_lookup_agl(const char *name)
{
	char buf[64];
	char *p;
	int l = 0;
	int r = nelem(agl_name_list) - 1;
	int code = 0;

	fz_strlcpy(buf, name, sizeof buf);

	/* kill anything after first period and underscore */
	p = strchr(buf, '.');
	if (p) p[0] = 0;
	p = strchr(buf, '_');
	if (p) p[0] = 0;

	while (l <= r)
	{
		int m = (l + r) >> 1;
		int c = strcmp(buf, agl_name_list[m]);
		if (c < 0)
			r = m - 1;
		else if (c > 0)
			l = m + 1;
		else
			return agl_code_list[m];
	}

	if (buf[0] == 'u' && buf[1] == 'n' && buf[2] == 'i')
		code = strtol(buf + 3, NULL, 16);
	else if (buf[0] == 'u')
		code = strtol(buf + 1, NULL, 16);
	else if (buf[0] == 'a' && buf[1] != 0 && buf[2] != 0)
		code = strtol(buf + 1, NULL, 10);

	return (code > 0 && code <= 0x10ffff) ? code : FZ_REPLACEMENT_CHARACTER;
}

static const char *empty_dup_list[] = { 0 };

const char **
pdf_lookup_agl_duplicates(int ucs)
{
	int l = 0;
	int r = nelem(agl_dup_offsets) / 2 - 1;
	while (l <= r)
	{
		int m = (l + r) >> 1;
		if (ucs < agl_dup_offsets[m << 1])
			r = m - 1;
		else if (ucs > agl_dup_offsets[m << 1])
			l = m + 1;
		else
			return agl_dup_names + agl_dup_offsets[(m << 1) + 1];
	}
	return empty_dup_list;
}

int pdf_cyrillic_from_unicode(int u)
{
	int l = 0;
	int r = nelem(koi8u_from_unicode) - 1;
	if (u < 128)
		return u;
	while (l <= r)
	{
		int m = (l + r) >> 1;
		if (u < koi8u_from_unicode[m].u)
			r = m - 1;
		else if (u > koi8u_from_unicode[m].u)
			l = m + 1;
		else
			return koi8u_from_unicode[m].c;
	}
	return -1;
}

int pdf_greek_from_unicode(int u)
{
	int l = 0;
	int r = nelem(iso8859_7_from_unicode) - 1;
	if (u < 128)
		return u;
	while (l <= r)
	{
		int m = (l + r) >> 1;
		if (u < iso8859_7_from_unicode[m].u)
			r = m - 1;
		else if (u > iso8859_7_from_unicode[m].u)
			l = m + 1;
		else
			return iso8859_7_from_unicode[m].c;
	}
	return -1;
}
