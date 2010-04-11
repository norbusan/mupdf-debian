#include "fitz.h"
#include "mupdf.h"

static inline int iswhite(int ch)
{
	return
		ch == '\000' || ch == '\011' || ch == '\012' ||
		ch == '\014' || ch == '\015' || ch == '\040';
}

/*
 * magic version tag and startxref
 */

static fz_error
loadversion(pdf_xref *xref)
{
	fz_error error;
	char buf[20];

	error = fz_seek(xref->file, 0, 0);
	if (error)
		return fz_rethrow(error, "cannot seek to beginning of file");

	error = fz_readline(xref->file, buf, sizeof buf);
	if (error)
		return fz_rethrow(error, "cannot read version marker");
	if (memcmp(buf, "%PDF-", 5) != 0)
		return fz_throw("cannot recognize version marker");

	xref->version = (int) (atof(buf + 5) * 10.0 + 0.5);

	pdf_logxref("version %d.%d\n", xref->version / 10, xref->version % 10);

	return fz_okay;
}

static fz_error
readstartxref(pdf_xref *xref)
{
	fz_error error;
	unsigned char buf[1024];
	int t, n;
	int i;

	error = fz_seek(xref->file, 0, 2);
	if (error)
		return fz_rethrow(error, "cannot seek to end of file");

	t = MAX(0, fz_tell(xref->file) - ((int)sizeof buf));
	error = fz_seek(xref->file, t, 0);
	if (error)
		return fz_rethrow(error, "cannot seek to offset %d", t);

	error = fz_read(&n, xref->file, buf, sizeof buf);
	if (error)
		return fz_rethrow(error, "cannot read from file");

	for (i = n - 9; i >= 0; i--)
	{
		if (memcmp(buf + i, "startxref", 9) == 0)
		{
			i += 9;
			while (iswhite(buf[i]) && i < n)
				i ++;
			xref->startxref = atoi((char*)(buf + i));
			return fz_okay;
		}
	}

	return fz_throw("cannot find startxref");
}

/*
 * trailer dictionary
 */

static fz_error
readoldtrailer(pdf_xref *xref, char *buf, int cap)
{
	fz_error error;
	int ofs, len;
	char *s;
	int n;
	int t;
	pdf_token_e tok;
	int c;

	pdf_logxref("load old xref format trailer\n");

	error = fz_readline(xref->file, buf, cap);
	if (error)
		return fz_rethrow(error, "cannot read xref marker");
	if (strncmp(buf, "xref", 4) != 0)
		return fz_throw("cannot find xref marker");

	while (1)
	{
		c = fz_peekbyte(xref->file);
		if (!(c >= '0' && c <= '9'))
			break;

		error = fz_readline(xref->file, buf, cap);
		if (error)
			return fz_rethrow(error, "cannot read xref count");

		s = buf;
		ofs = atoi(fz_strsep(&s, " "));
		if (!s)
			return fz_throw("invalid range marker in xref");
		len = atoi(fz_strsep(&s, " "));

		/* broken pdfs where the section is not on a separate line */
		if (s && *s != '\0')
		{
			error = fz_seek(xref->file, -(2 + strlen(s)), 1);
			if (error)
				return fz_rethrow(error, "cannot seek in file");
		}

		t = fz_tell(xref->file);
		if (t < 0)
			return fz_throw("cannot tell in file");

		error = fz_seek(xref->file, t + 20 * len, 0);
		if (error)
			return fz_rethrow(error, "cannot seek in file");
	}

	error = fz_readerror(xref->file);
	if (error)
		return fz_rethrow(error, "cannot read from file");

	error = pdf_lex(&tok, xref->file, buf, cap, &n);
	if (error)
		return fz_rethrow(error, "cannot parse trailer");
	if (tok != PDF_TTRAILER)
		return fz_throw("expected trailer marker");

	error = pdf_lex(&tok, xref->file, buf, cap, &n);
	if (error)
		return fz_rethrow(error, "cannot parse trailer");
	if (tok != PDF_TODICT)
		return fz_throw("expected trailer dictionary");

	error = pdf_parsedict(&xref->trailer, xref, xref->file, buf, cap);
	if (error)
		return fz_rethrow(error, "cannot parse trailer");
	return fz_okay;
}

static fz_error
readnewtrailer(pdf_xref *xref, char *buf, int cap)
{
	fz_error error;

	pdf_logxref("load new xref format trailer\n");

	error = pdf_parseindobj(&xref->trailer, xref, xref->file, buf, cap, nil, nil, nil);
	if (error)
		return fz_rethrow(error, "cannot parse trailer (compressed)");
	return fz_okay;
}

static fz_error
readtrailer(pdf_xref *xref, char *buf, int cap)
{
	fz_error error;
	int c;

	error = fz_seek(xref->file, xref->startxref, 0);
	if (error)
		return fz_rethrow(error, "cannot seek to startxref");

	while (iswhite(fz_peekbyte(xref->file)))
		fz_readbyte(xref->file);

	c = fz_peekbyte(xref->file);
	error = fz_readerror(xref->file);
	if (error)
		return fz_rethrow(error, "cannot read trailer");

	if (c == 'x')
	{
		error = readoldtrailer(xref, buf, cap);
		if (error)
			return fz_rethrow(error, "cannot read trailer");
	}
	else if (c >= '0' && c <= '9')
	{
		error = readnewtrailer(xref, buf, cap);
		if (error)
			return fz_rethrow(error, "cannot read trailer");
	}
	else
	{
		return fz_throw("cannot recognize xref format: '%c'", c);
	}

	return fz_okay;
}

/*
 * xref tables
 */

static fz_error
readoldxref(fz_obj **trailerp, pdf_xref *xref, char *buf, int cap)
{
	fz_error error;
	int ofs, len;
	char *s;
	int n;
	pdf_token_e tok;
	int i;
	int c;

	pdf_logxref("load old xref format\n");

	error = fz_readline(xref->file, buf, cap);
	if (error)
		return fz_rethrow(error, "cannot read xref marker");
	if (strncmp(buf, "xref", 4) != 0)
		return fz_throw("cannot find xref marker");

	while (1)
	{
		c = fz_peekbyte(xref->file);
		if (!(c >= '0' && c <= '9'))
			break;

		error = fz_readline(xref->file, buf, cap);
		if (error)
			return fz_rethrow(error, "cannot read xref count");

		s = buf;
		ofs = atoi(fz_strsep(&s, " "));
		len = atoi(fz_strsep(&s, " "));

		/* broken pdfs where the section is not on a separate line */
		if (s && *s != '\0')
		{
			fz_warn("broken xref section. proceeding anyway.");
			error = fz_seek(xref->file, -(2 + strlen(s)), 1);
			if (error)
				return fz_rethrow(error, "cannot seek to xref");
		}

		/* broken pdfs where size in trailer undershoots
		entries in xref sections */
		if ((ofs + len) > xref->cap)
		{
			fz_warn("broken xref section, proceeding anyway.");
			xref->cap = ofs + len;
			xref->table = fz_realloc(xref->table, xref->cap * sizeof(pdf_xrefentry));
		}

		if ((ofs + len) > xref->len)
		{
			for (i = xref->len; i < (ofs + len); i++)
			{
				xref->table[i].ofs = 0;
				xref->table[i].gen = 0;
				xref->table[i].stmofs = 0;
				xref->table[i].obj = nil;
				xref->table[i].type = 0;
			}
			xref->len = ofs + len;
		}

		for (i = 0; i < len; i++)
		{
			error = fz_read(&n, xref->file, (unsigned char *) buf, 20);
			if (error)
				return fz_rethrow(error, "cannot read xref table");
			if (!xref->table[ofs + i].type)
			{
				s = buf;

				/* broken pdfs where line start with white space */
				while (*s != '\0' && iswhite(*s))
					s++;

				xref->table[ofs + i].ofs = atoi(s);
				xref->table[ofs + i].gen = atoi(s + 11);
				xref->table[ofs + i].type = s[17];
			}
		}
	}

	error = pdf_lex(&tok, xref->file, buf, cap, &n);
	if (error)
		return fz_rethrow(error, "cannot parse trailer");
	if (tok != PDF_TTRAILER)
		return fz_throw("expected trailer marker");

	error = pdf_lex(&tok, xref->file, buf, cap, &n);
	if (error)
		return fz_rethrow(error, "cannot parse trailer");
	if (tok != PDF_TODICT)
		return fz_throw("expected trailer dictionary");

	error = pdf_parsedict(trailerp, xref, xref->file, buf, cap);
	if (error)
		return fz_rethrow(error, "cannot parse trailer");
	return fz_okay;
}

static fz_error
readnewxrefsection(pdf_xref *xref, fz_stream *stm, int i0, int i1, int w0, int w1, int w2)
{
	fz_error error;
	int i, n;

	if (i0 < 0 || i0 + i1 > xref->len)
		return fz_throw("xref stream has too many entries");

	for (i = i0; i < i0 + i1; i++)
	{
		int a = 0;
		int b = 0;
		int c = 0;

		if (fz_peekbyte(stm) == EOF)
		{
			error = fz_readerror(stm);
			if (error)
				return fz_rethrow(error, "truncated xref stream");
			return fz_throw("truncated xref stream");
		}

		for (n = 0; n < w0; n++)
			a = (a << 8) + fz_readbyte(stm);
		for (n = 0; n < w1; n++)
			b = (b << 8) + fz_readbyte(stm);
		for (n = 0; n < w2; n++)
			c = (c << 8) + fz_readbyte(stm);

		error = fz_readerror(stm);
		if (error)
			return fz_rethrow(error, "truncated xref stream");

		if (!xref->table[i].type)
		{
			int t = w0 ? a : 1;
			xref->table[i].type = t == 0 ? 'f' : t == 1 ? 'n' : t == 2 ? 'o' : 0;
			xref->table[i].ofs = w1 ? b : 0;
			xref->table[i].gen = w2 ? c : 0;
		}
	}

	return fz_okay;
}

static fz_error
readnewxref(fz_obj **trailerp, pdf_xref *xref, char *buf, int cap)
{
	fz_error error;
	fz_stream *stm;
	fz_obj *trailer;
	fz_obj *index;
	fz_obj *obj;
	int oid, gen, stmofs;
	int size, w0, w1, w2;
	int t;
	int i;

	pdf_logxref("load new xref format\n");

	error = pdf_parseindobj(&trailer, xref, xref->file, buf, cap, &oid, &gen, &stmofs);
	if (error)
		return fz_rethrow(error, "cannot parse compressed xref stream object");

	obj = fz_dictgets(trailer, "Size");
	if (!obj)
	{
		fz_dropobj(trailer);
		return fz_throw("xref stream missing Size entry");
	}
	size = fz_toint(obj);

	if (size >= xref->cap)
	{
		xref->cap = size + 1; /* for hack to allow broken pdf generators with off-by-one errors */
		xref->table = fz_realloc(xref->table, xref->cap * sizeof(pdf_xrefentry));
	}

	if (size > xref->len)
	{
		for (i = xref->len; i < xref->cap; i++)
		{
			xref->table[i].ofs = 0;
			xref->table[i].gen = 0;
			xref->table[i].stmofs = 0;
			xref->table[i].obj = nil;
			xref->table[i].type = 0;
		}
		xref->len = size;
	}

	if (oid < 0 || oid >= xref->len)
	{
		if (oid == xref->len && oid < xref->cap)
		{
			/* allow broken pdf files that have off-by-one errors in the xref */
			fz_warn("object id (%d %d R) out of range (0..%d)", oid, gen, xref->len - 1);
			xref->len ++;
		}
		else
		{
			fz_dropobj(trailer);
			return fz_throw("object id (%d %d R) out of range (0..%d)", oid, gen, xref->len - 1);
		}
	}

	xref->table[oid].type = 'n';
	xref->table[oid].gen = gen;
	xref->table[oid].obj = fz_keepobj(trailer);
	xref->table[oid].stmofs = stmofs;
	xref->table[oid].ofs = 0;

	obj = fz_dictgets(trailer, "W");
	if (!obj) {
		fz_dropobj(trailer);
		return fz_throw("xref stream missing W entry");
	}
	w0 = fz_toint(fz_arrayget(obj, 0));
	w1 = fz_toint(fz_arrayget(obj, 1));
	w2 = fz_toint(fz_arrayget(obj, 2));

	index = fz_dictgets(trailer, "Index");

	error = pdf_openstream(&stm, xref, oid, gen);
	if (error)
	{
		fz_dropobj(trailer);
		return fz_rethrow(error, "cannot open compressed xref stream");
	}

	if (!index)
	{
		error = readnewxrefsection(xref, stm, 0, size, w0, w1, w2);
		if (error)
		{
			fz_dropstream(stm);
			fz_dropobj(trailer);
			return fz_rethrow(error, "cannot read xref stream");
		}
	}
	else
	{
		for (t = 0; t < fz_arraylen(index); t += 2)
		{
			int i0 = fz_toint(fz_arrayget(index, t + 0));
			int i1 = fz_toint(fz_arrayget(index, t + 1));
			error = readnewxrefsection(xref, stm, i0, i1, w0, w1, w2);
			if (error)
			{
				fz_dropstream(stm);
				fz_dropobj(trailer);
				return fz_rethrow(error, "cannot read xref stream section");
			}
		}
	}

	fz_dropstream(stm);

	*trailerp = trailer;

	return fz_okay;
}

static fz_error
readxref(fz_obj **trailerp, pdf_xref *xref, int ofs, char *buf, int cap)
{
	fz_error error;
	int c;

	error = fz_seek(xref->file, ofs, 0);
	if (error)
		return fz_rethrow(error, "cannot seek to xref");

	while (iswhite(fz_peekbyte(xref->file)))
		fz_readbyte(xref->file);

	c = fz_peekbyte(xref->file);
	error = fz_readerror(xref->file);
	if (error)
		return fz_rethrow(error, "cannot read trailer");

	if (c == 'x')
	{
		error = readoldxref(trailerp, xref, buf, cap);
		if (error)
			return fz_rethrow(error, "cannot read xref (ofs=%d)", ofs);
	}
	else if (c >= '0' && c <= '9')
	{
		error = readnewxref(trailerp, xref, buf, cap);
		if (error)
			return fz_rethrow(error, "cannot read xref (ofs=%d)", ofs);
	}
	else
	{
		return fz_throw("cannot recognize xref format");
	}

	return fz_okay;
}

static fz_error
readxrefsections(pdf_xref *xref, int ofs, char *buf, int cap)
{
	fz_error error;
	fz_obj *trailer;
	fz_obj *prev;
	fz_obj *xrefstm;

	error = readxref(&trailer, xref, ofs, buf, cap);
	if (error)
		return fz_rethrow(error, "cannot read xref section");

	/* FIXME: do we overwrite free entries properly? */
	xrefstm = fz_dictgets(trailer, "XRefStm");
	if (xrefstm)
	{
		pdf_logxref("load xrefstm\n");
		error = readxrefsections(xref, fz_toint(xrefstm), buf, cap);
		if (error)
		{
			fz_dropobj(trailer);
			return fz_rethrow(error, "cannot read /XRefStm xref section");
		}
	}

	prev = fz_dictgets(trailer, "Prev");
	if (prev)
	{
		pdf_logxref("load prev at 0x%x\n", fz_toint(prev));
		error = readxrefsections(xref, fz_toint(prev), buf, cap);
		if (error)
		{
			fz_dropobj(trailer);
			return fz_rethrow(error, "cannot read /Prev xref section");
		}
	}

	fz_dropobj(trailer);
	return fz_okay;
}

/*
 * compressed object streams
 */

fz_error
pdf_loadobjstm(pdf_xref *xref, int oid, int gen, char *buf, int cap)
{
	fz_error error;
	fz_stream *stm;
	fz_obj *objstm;
	int *oidbuf;
	int *ofsbuf;

	fz_obj *obj;
	int first;
	int count;
	int i, n;
	pdf_token_e tok;

	pdf_logxref("loadobjstm (%d %d R)\n", oid, gen);

	error = pdf_loadobject(&objstm, xref, oid, gen);
	if (error)
		return fz_rethrow(error, "cannot load object stream object");

	count = fz_toint(fz_dictgets(objstm, "N"));
	first = fz_toint(fz_dictgets(objstm, "First"));

	pdf_logxref("  count %d\n", count);

	oidbuf = fz_malloc(count * sizeof(int));
	ofsbuf = fz_malloc(count * sizeof(int));

	error = pdf_openstream(&stm, xref, oid, gen);
	if (error)
	{
		error = fz_rethrow(error, "cannot open object stream");
		goto cleanupbuf;
	}

	for (i = 0; i < count; i++)
	{
		error = pdf_lex(&tok, stm, buf, cap, &n);
		if (error || tok != PDF_TINT)
		{
			error = fz_rethrow(error, "corrupt object stream");
			goto cleanupstm;
		}
		oidbuf[i] = atoi(buf);

		error = pdf_lex(&tok, stm, buf, cap, &n);
		if (error || tok != PDF_TINT)
		{
			error = fz_rethrow(error, "corrupt object stream");
			goto cleanupstm;
		}
		ofsbuf[i] = atoi(buf);
	}

	error = fz_seek(stm, first, 0);
	if (error)
	{
		error = fz_rethrow(error, "cannot seek in object stream");
		goto cleanupstm;
	}

	for (i = 0; i < count; i++)
	{
		/* FIXME: seek to first + ofsbuf[i] */

		error = pdf_parsestmobj(&obj, xref, stm, buf, cap);
		if (error)
		{
			error = fz_rethrow(error, "cannot parse object %d in stream", i);
			goto cleanupstm;
		}

		if (oidbuf[i] < 1 || oidbuf[i] >= xref->len)
		{
			fz_dropobj(obj);
			error = fz_throw("object id (%d 0 R) out of range (0..%d)", oidbuf[i], xref->len - 1);
			goto cleanupstm;
		}

		if (xref->table[oidbuf[i]].obj)
			fz_dropobj(xref->table[oidbuf[i]].obj);
		xref->table[oidbuf[i]].obj = obj;
	}

	fz_dropstream(stm);
	fz_free(ofsbuf);
	fz_free(oidbuf);
	fz_dropobj(objstm);
	return fz_okay;

cleanupstm:
	fz_dropstream(stm);
cleanupbuf:
	fz_free(ofsbuf);
	fz_free(oidbuf);
	fz_dropobj(objstm);
	return error; /* already rethrown */
}

/*
 * open and load xref tables from pdf
 */

fz_error
pdf_loadxref(pdf_xref *xref, char *filename)
{
	fz_error error;
	fz_obj *size;
	int i;

	char buf[65536];	/* yeowch! */

	pdf_logxref("loadxref '%s' %p\n", filename, xref);

	error = fz_openrfile(&xref->file, filename);
	if (error)
	{
		return fz_rethrow(error, "cannot open file: '%s'", filename);
	}

	error = loadversion(xref);
	if (error)
	{
		error = fz_rethrow(error, "cannot read version marker");
		goto cleanup;
	}

	error = readstartxref(xref);
	if (error)
	{
		error = fz_rethrow(error, "cannot read startxref");
		goto cleanup;
	}

	error = readtrailer(xref, buf, sizeof buf);
	if (error)
	{
		error = fz_rethrow(error, "cannot read trailer");
		goto cleanup;
	}

	size = fz_dictgets(xref->trailer, "Size");
	if (!size)
	{
		error = fz_throw("trailer missing Size entry");
		goto cleanup;
	}

	pdf_logxref("  size %d at 0x%x\n", fz_toint(size), xref->startxref);

	assert(xref->table == nil);

	xref->len = fz_toint(size);
	xref->cap = xref->len + 1; /* for hack to allow broken pdf generators with off-by-one errors */
	xref->table = fz_malloc(xref->cap * sizeof(pdf_xrefentry));
	for (i = 0; i < xref->cap; i++)
	{
		xref->table[i].ofs = 0;
		xref->table[i].gen = 0;
		xref->table[i].stmofs = 0;
		xref->table[i].obj = nil;
		xref->table[i].type = 0;
	}

	error = readxrefsections(xref, xref->startxref, buf, sizeof buf);
	if (error)
	{
		error = fz_rethrow(error, "cannot read xref");
		goto cleanup;
	}

	/* broken pdfs where first object is not free */
	if (xref->table[0].type != 'f')
	{
		fz_warn("first object in xref is not free");
		xref->table[0].type = 'f';
	}

	/* broken pdfs where freed objects have offset and gen set to 0 but still exist */
	for (i = 0; i < xref->len; i++)
	{
		if (xref->table[i].type == 'n' && xref->table[i].ofs == 0 &&
			xref->table[i].gen == 0 && xref->table[i].obj == nil)
		{
			fz_warn("object (%d %d R) has invalid offset, assumed missing", i, xref->table[i].gen);
			xref->table[i].type = 'f';
		}
	}

	return fz_okay;

cleanup:
	fz_dropstream(xref->file);
	xref->file = nil;
	free(xref->table);
	xref->table = nil;
	return error;
}
