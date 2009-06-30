#include "fitz.h"
#include "mupdf.h"

fz_rect pdf_torect(fz_obj *array)
{
	fz_rect r;
	float a = fz_toreal(fz_arrayget(array, 0));
	float b = fz_toreal(fz_arrayget(array, 1));
	float c = fz_toreal(fz_arrayget(array, 2));
	float d = fz_toreal(fz_arrayget(array, 3));
	r.x0 = MIN(a, c);
	r.y0 = MIN(b, d);
	r.x1 = MAX(a, c);
	r.y1 = MAX(b, d);
	return r;
}

fz_matrix pdf_tomatrix(fz_obj *array)
{
	fz_matrix m;
	m.a = fz_toreal(fz_arrayget(array, 0));
	m.b = fz_toreal(fz_arrayget(array, 1));
	m.c = fz_toreal(fz_arrayget(array, 2));
	m.d = fz_toreal(fz_arrayget(array, 3));
	m.e = fz_toreal(fz_arrayget(array, 4));
	m.f = fz_toreal(fz_arrayget(array, 5));
	return m;
}

fz_error
pdf_toutf8(char **dstp, fz_obj *src)
{
	unsigned char *srcptr = (unsigned char *) fz_tostrbuf(src);
	char *dstptr;
	int srclen = fz_tostrlen(src);
	int dstlen = 0;
	int ucs;
	int i;

	if (srclen > 2 && srcptr[0] == 254 && srcptr[1] == 255)
	{
		for (i = 2; i < srclen; i += 2)
		{
			ucs = (srcptr[i] << 8) | srcptr[i+1];
			dstlen += runelen(ucs);
		}

		dstptr = *dstp = fz_malloc(dstlen + 1);
		if (!dstptr)
			return fz_rethrow(-1, "out of memory: utf-8 string");

		for (i = 2; i < srclen; i += 2)
		{
			ucs = (srcptr[i] << 8) | srcptr[i+1];
			dstptr += runetochar(dstptr, &ucs);
		}
	}

	else
	{
		for (i = 0; i < srclen; i++)
			dstlen += runelen(pdf_docencoding[srcptr[i]]);

		dstptr = *dstp = fz_malloc(dstlen + 1);
		if (!dstptr)
			return fz_rethrow(-1, "out of memory: utf-8 string");

		for (i = 0; i < srclen; i++)
		{
			ucs = pdf_docencoding[srcptr[i]];
			dstptr += runetochar(dstptr, &ucs);
		}
	}

	*dstptr = '\0';
	return fz_okay;
}

fz_error
pdf_toucs2(unsigned short **dstp, fz_obj *src)
{
	unsigned char *srcptr = (unsigned char *) fz_tostrbuf(src);
	unsigned short *dstptr;
	int srclen = fz_tostrlen(src);
	int i;

	if (srclen > 2 && srcptr[0] == 254 && srcptr[1] == 255)
	{
		dstptr = *dstp = fz_malloc(((srclen - 2) / 2 + 1) * sizeof(short));
		if (!dstptr)
			return fz_rethrow(-1, "out of memory: ucs-2 string");
		for (i = 2; i < srclen; i += 2)
			*dstptr++ = (srcptr[i] << 8) | srcptr[i+1];
	}

	else
	{
		dstptr = *dstp = fz_malloc((srclen + 1) * sizeof(short));
		if (!dstptr)
			return fz_rethrow(-1, "out of memory: ucs-2 string");
		for (i = 0; i < srclen; i++)
			*dstptr++ = pdf_docencoding[srcptr[i]];
	}

	*dstptr = '\0';
	return fz_okay;
}

fz_error
pdf_parsearray(fz_obj **op, pdf_xref *xref, fz_stream *file, char *buf, int cap)
{
	fz_error error = fz_okay;
	fz_obj *ary = nil;
	fz_obj *obj = nil;
	int a = 0, b = 0, n = 0;
	pdf_token_e tok;
	int len;

	error = fz_newarray(&ary, 4);
	if (error)
		return fz_rethrow(error, "cannot create array");

	while (1)
	{
		error = pdf_lex(&tok, file, buf, cap, &len);
		if (error)
			goto cleanup;

		if (tok != PDF_TINT && tok != PDF_TR)
		{
			if (n > 0)
			{
				error = fz_newint(&obj, a);
				if (error)
					goto cleanup;
				error = fz_arraypush(ary, obj);
				if (error)
					goto cleanup;
				fz_dropobj(obj);
				obj = nil;
			}
			if (n > 1)
			{
				error = fz_newint(&obj, b);
				if (error)
					goto cleanup;
				error = fz_arraypush(ary, obj);
				if (error)
					goto cleanup;
				fz_dropobj(obj);
				obj = nil;
			}
			n = 0;
		}

		if (tok == PDF_TINT && n == 2)
		{
			error = fz_newint(&obj, a);
			if (error)
				goto cleanup;
			error = fz_arraypush(ary, obj);
			if (error)
				goto cleanup;
			fz_dropobj(obj);
			obj = nil;
			a = b;
			n --;
		}

		switch (tok)
		{
		case PDF_TCARRAY:
			*op = ary;
			return fz_okay;
		case PDF_TINT:
			if (n == 0)
				a = atoi(buf);
			if (n == 1)
				b = atoi(buf);
			n ++;
			break;
		case PDF_TR:
			if (n != 2)
				goto cleanup;
			error = fz_newindirect(&obj, a, b, xref);
			if (error)
				goto cleanup;
			n = 0;
			break;

		case PDF_TOARRAY: error = pdf_parsearray(&obj, xref, file, buf, cap); break;
		case PDF_TODICT: error = pdf_parsedict(&obj, xref, file, buf, cap); break;
		case PDF_TNAME: error = fz_newname(&obj, buf); break;
		case PDF_TREAL: error = fz_newreal(&obj, atof(buf)); break;
		case PDF_TSTRING: error = fz_newstring(&obj, buf, len); break;
		case PDF_TTRUE: error = fz_newbool(&obj, 1); break;
		case PDF_TFALSE: error = fz_newbool(&obj, 0); break;
		case PDF_TNULL: error = fz_newnull(&obj); break;
		default: goto cleanup;
		}
		if (error)
			goto cleanup;

		if (obj)
		{
			error = fz_arraypush(ary, obj);
			if (error)
				goto cleanup;
			fz_dropobj(obj);
		}

		obj = nil;
	}

cleanup:
	if (obj) fz_dropobj(obj);
	if (ary) fz_dropobj(ary);
	*op = nil;
	return fz_rethrow(error, "cannot parse array");
}

fz_error
pdf_parsedict(fz_obj **op, pdf_xref *xref, fz_stream *file, char *buf, int cap)
{
	fz_error error = fz_okay;
	fz_obj *dict = nil;
	fz_obj *key = nil;
	fz_obj *val = nil;
	pdf_token_e tok;
	int len;
	int a, b;

	error = fz_newdict(&dict, 8);
	if (error)
		return fz_rethrow(error, "cannot create dict");

	while (1)
	{
		error = pdf_lex(&tok, file, buf, cap, &len);
		if (error)
			goto cleanup;

skip:
		if (tok == PDF_TCDICT)
		{
			*op = dict;
			return fz_okay;
		}

		/* for BI .. ID .. EI in content streams */
		if (tok == PDF_TKEYWORD && !strcmp(buf, "ID"))
		{
			*op = dict;
			return fz_okay;
		}

		if (tok != PDF_TNAME)
		{
			error = fz_throw("invalid key in dict");
			goto cleanup;
		}

		error = fz_newname(&key, buf);
		if (error)
			goto cleanup;

		error = pdf_lex(&tok, file, buf, cap, &len);
		if (error)
			goto cleanup;

		switch (tok)
		{
		case PDF_TOARRAY: error = pdf_parsearray(&val, xref, file, buf, cap); break;
		case PDF_TODICT: error = pdf_parsedict(&val, xref, file, buf, cap); break;
		case PDF_TNAME: error = fz_newname(&val, buf); break;
		case PDF_TREAL: error = fz_newreal(&val, atof(buf)); break;
		case PDF_TSTRING: error = fz_newstring(&val, buf, len); break;
		case PDF_TTRUE: error = fz_newbool(&val, 1); break;
		case PDF_TFALSE: error = fz_newbool(&val, 0); break;
		case PDF_TNULL: error = fz_newnull(&val); break;

		case PDF_TINT:
			a = atoi(buf);
			error = pdf_lex(&tok, file, buf, cap, &len);
			if (error)
				goto cleanup;
			if (tok == PDF_TCDICT || tok == PDF_TNAME ||
					(tok == PDF_TKEYWORD && !strcmp(buf, "ID")))
			{
				error = fz_newint(&val, a);
				if (error)
					goto cleanup;
				error = fz_dictput(dict, key, val);
				if (error)
					goto cleanup;
				fz_dropobj(val);
				fz_dropobj(key);
				key = val = nil;
				goto skip;
			}
			if (tok == PDF_TINT)
			{
				b = atoi(buf);
				error = pdf_lex(&tok, file, buf, cap, &len);
				if (error)
					goto cleanup;
				if (tok == PDF_TR)
				{
					error = fz_newindirect(&val, a, b, xref);
					break;
				}
			}
			error = fz_throw("invalid indirect reference in dict");
			goto cleanup;

		default:
			error = fz_throw("unknown token in dict");
			goto cleanup;
		}

		if (error)
			goto cleanup;

		error = fz_dictput(dict, key, val);
		if (error)
			goto cleanup;

		fz_dropobj(val);
		fz_dropobj(key);
		key = val = nil;
	}

cleanup:
	if (key) fz_dropobj(key);
	if (val) fz_dropobj(val);
	if (dict) fz_dropobj(dict);
	*op = nil;
	return fz_rethrow(error, "cannot parse dict");
}

fz_error
pdf_parsestmobj(fz_obj **op, pdf_xref *xref, fz_stream *file, char *buf, int cap)
{
	fz_error error;
	pdf_token_e tok;
	int len;

	error = pdf_lex(&tok, file, buf, cap, &len);
	if (error)
		return fz_rethrow(error, "cannot parse token in object stream");

	switch (tok)
	{
	case PDF_TOARRAY: error = pdf_parsearray(op, xref, file, buf, cap); break;
	case PDF_TODICT: error = pdf_parsedict(op, xref, file, buf, cap); break;
	case PDF_TNAME: error = fz_newname(op, buf); break;
	case PDF_TREAL: error = fz_newreal(op, atof(buf)); break;
	case PDF_TSTRING: error = fz_newstring(op, buf, len); break;
	case PDF_TTRUE: error = fz_newbool(op, 1); break;
	case PDF_TFALSE: error = fz_newbool(op, 0); break;
	case PDF_TNULL: error = fz_newnull(op); break;
	case PDF_TINT: error = fz_newint(op, atoi(buf)); break;
	default: return fz_throw("unknown token in object stream");
	}

	if (error)
		return fz_rethrow(error, "cannot parse object stream");
	return fz_okay;
}

fz_error
pdf_parseindobj(fz_obj **op, pdf_xref *xref,
		fz_stream *file, char *buf, int cap,
		int *onum, int *ogen, int *ostmofs)
{
	fz_error error = fz_okay;
	fz_obj *obj = nil;
	int num = 0, gen = 0, stmofs;
	pdf_token_e tok;
	int len;
	int a, b;

	error = pdf_lex(&tok, file, buf, cap, &len);
	if (error || tok != PDF_TINT)
		goto cleanup;
	num = atoi(buf);

	error = pdf_lex(&tok, file, buf, cap, &len);
	if (error || tok != PDF_TINT)
		goto cleanup;
	gen = atoi(buf);

	error = pdf_lex(&tok, file, buf, cap, &len);
	if (error || tok != PDF_TOBJ)
		goto cleanup;

	error = pdf_lex(&tok, file, buf, cap, &len);
	if (error)
		goto cleanup;

	switch (tok)
	{
	case PDF_TOARRAY: error = pdf_parsearray(&obj, xref, file, buf, cap); break;
	case PDF_TODICT: error = pdf_parsedict(&obj, xref, file, buf, cap); break;
	case PDF_TNAME: error = fz_newname(&obj, buf); break;
	case PDF_TREAL: error = fz_newreal(&obj, atof(buf)); break;
	case PDF_TSTRING: error = fz_newstring(&obj, buf, len); break;
	case PDF_TTRUE: error = fz_newbool(&obj, 1); break;
	case PDF_TFALSE: error = fz_newbool(&obj, 0); break;
	case PDF_TNULL: error = fz_newnull(&obj); break;

	case PDF_TINT:
		a = atoi(buf);
		error = pdf_lex(&tok, file, buf, cap, &len);
		if (error)
			goto cleanup;
		if (tok == PDF_TSTREAM || tok == PDF_TENDOBJ)
		{
			error = fz_newint(&obj, a);
			if (error)
				goto cleanup;
			goto skip;
		}
		if (tok == PDF_TINT)
		{
			b = atoi(buf);
			error = pdf_lex(&tok, file, buf, cap, &len);
			if (error)
				goto cleanup;
			if (tok == PDF_TR)
			{
				error = fz_newindirect(&obj, a, b, xref);
				break;
			}
		}
		goto cleanup;

	case PDF_TENDOBJ:
		error = fz_newnull(&obj);
		goto skip;

	default:
		goto cleanup;
	}
	if (error)
		goto cleanup;

	error = pdf_lex(&tok, file, buf, cap, &len);
	if (error)
		goto cleanup;

skip:
	if (tok == PDF_TSTREAM)
	{
		int c = fz_readbyte(file);
		if (c == '\r')
		{
			c = fz_peekbyte(file);
			if (c != '\n')
				fz_warn("line feed missing after stream begin marker (%d %d R)", num, gen);
			else
				c = fz_readbyte(file);
		}
		error = fz_readerror(file);
		if (error)
			goto cleanup;
		stmofs = fz_tell(file);
	}
	else if (tok == PDF_TENDOBJ)
	{
		stmofs = 0;
	}
	else
	{
		fz_warn("expected endobj or stream keyword (%d %d R)", num, gen);
		stmofs = 0;
	}

	if (onum) *onum = num;
	if (ogen) *ogen = gen;
	if (ostmofs) *ostmofs = stmofs;
	*op = obj;
	return fz_okay;

cleanup:
	if (obj)
		fz_dropobj(obj);
	if (error)
		return fz_rethrow(error, "cannot parse indirect object (%d %d R)", num, gen);
	return fz_throw("cannot parse indirect object (%d %d R)", num, gen);
}

