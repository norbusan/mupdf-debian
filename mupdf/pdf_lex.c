#include "fitz.h"
#include "mupdf.h"

/*
 * pdf_lex will use fz_peekbyte and fz_readbyte.
 * have to check for file errors with fz_readerror() after lexing.
 */

static inline int iswhite(int ch)
{
	return
		ch == '\000' ||
		ch == '\011' ||
		ch == '\012' ||
		ch == '\014' ||
		ch == '\015' ||
		ch == '\040';
}

static inline int isdelim(int ch)
{
	return
		ch == '(' || ch == ')' ||
		ch == '<' || ch == '>' ||
		ch == '[' || ch == ']' ||
		ch == '{' || ch == '}' ||
		ch == '/' ||
		ch == '%';
}

static inline int isregular(int ch)
{
	return !isdelim(ch) && !iswhite(ch) && ch != EOF;
}

static inline int isnumber(int ch)
{
	return ch == '+' || ch == '-' || ch == '.' || (ch >= '0' && ch <= '9');
}

static inline int ishex(int ch)
{
	return
		(ch >= '0' && ch <= '9') ||
		(ch >= 'A' && ch <= 'F') ||
		(ch >= 'a' && ch <= 'f');
}

static inline int fromhex(int ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	else if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 0xA;
	else if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 0xA;
	return 0;
}

static inline void
lexwhite(fz_stream *f)
{
	int c;
	while (1)
	{
		c = fz_peekbyte(f);
		if (!iswhite(c))
			break;
		fz_readbyte(f);
	}
}

static inline void
lexcomment(fz_stream *f)
{
	int c;
	while (1)
	{
		c = fz_readbyte(f);
		if (c == '\012') break;
		if (c == '\015') break;
		if (c == EOF) break;
	}
}

static void
lexnumber(fz_stream *f, char *s, int n)
{
	while (n > 1)
	{
		if (!isnumber(fz_peekbyte(f)))
			break;
		*s++ = fz_readbyte(f);
		n--;
	}
	*s = '\0';
}

static void
lexname(fz_stream *f, char *s, int n)
{
	char *p = s;
	char *q = s;

	while (n > 1)
	{
		if (!isregular(fz_peekbyte(f)))
			break;
		*s++ = fz_readbyte(f);
		n--;
	}
	*s = '\0';

	while (*p)
	{
		if (p[0] == '#' && p[1] != 0 && p[2] != 0)
		{
			*q++ = fromhex(p[1]) * 16 + fromhex(p[2]);
			p += 3;
		}
		else
			*q++ = *p++;
	}
	*q = '\0';
}

static int
lexstring(fz_stream *f, char *buf, int n)
{
	char *s = buf;
	char *e = buf + n;
	int bal = 1;
	int oct;
	int c;

	while (s < e)
	{
		c = fz_readbyte(f);
		if (c == '(')
		{
			bal++;
			*s++ = c;
		}
		else if (c == ')')
		{
			bal --;
			if (bal == 0)
				break;
			*s++ = c;
		}
		else if (c == '\\')
		{
			c = fz_readbyte(f);
			if (c == 'n') *s++ = '\n';
			else if (c == 'r') *s++ = '\r';
			else if (c == 't') *s++ = '\t';
			else if (c == 'b') *s++ = '\b';
			else if (c == 'f') *s++ = '\f';
			else if (c == '(') *s++ = '(';
			else if (c == ')') *s++ = ')';
			else if (c == '\\') *s++ = '\\';

			else if (c >= '0' && c <= '9')
			{
				oct = c - '0';
				c = fz_peekbyte(f);
				if (c >= '0' && c <= '9')
				{
					fz_readbyte(f);
					oct = oct * 8 + (c - '0');
					c = fz_peekbyte(f);
					if (c >= '0' && c <= '9')
					{
						fz_readbyte(f);
						oct = oct * 8 + (c - '0');
					}
				}
				*s++ = oct;
			}

			else if (c == '\n')
				;
			else if (c == '\r')
			{
				c = fz_peekbyte(f);
				if (c == '\n')
					fz_readbyte(f);
			}
			else *s++ = c;
		}
		else
		{
			*s++ = c;
		}
	}

	return s - buf;
}

static int
lexhexstring(fz_stream *f, char *buf, int n)
{
	char *s = buf;
	char *e = buf + n;
	int a = 0, x = 0;
	int c;

	while (s < e)
	{
		c = fz_readbyte(f);
		if (c == '>')
			break;
		else if (iswhite(c))
			continue;
		else if (ishex(c))
		{
			if (x)
			{
				*s++ = a * 16 + fromhex(c);
				x = !x;
			}
			else
			{
				a = fromhex(c);
				x = !x;
			}
		}
		else
			break;
	}

	return s - buf;
}

static pdf_token_e
pdf_tokenfromkeyword(char *key)
{
	if (!strcmp(key, "R")) return PDF_TR;
	if (!strcmp(key, "true")) return PDF_TTRUE;
	if (!strcmp(key, "false")) return PDF_TFALSE;
	if (!strcmp(key, "null")) return PDF_TNULL;

	if (!strcmp(key, "obj")) return PDF_TOBJ;
	if (!strcmp(key, "endobj")) return PDF_TENDOBJ;
	if (!strcmp(key, "stream")) return PDF_TSTREAM;
	if (!strcmp(key, "endstream")) return PDF_TENDSTREAM;

	if (!strcmp(key, "xref")) return PDF_TXREF;
	if (!strcmp(key, "trailer")) return PDF_TTRAILER;
	if (!strcmp(key, "startxref")) return PDF_TSTARTXREF;

	return PDF_TKEYWORD;
}

fz_error
pdf_lex(pdf_token_e *tok, fz_stream *f, char *buf, int n, int *sl)
{
	fz_error error;
	int c;

	while (1)
	{
		c = fz_peekbyte(f);

		if (c == EOF)
		{
			*tok = PDF_TEOF;
			goto cleanupokay;
		}

		else if (iswhite(c))
			lexwhite(f);

		else if (c == '%')
			lexcomment(f);

		else if (c == '/')
		{
			fz_readbyte(f);
			lexname(f, buf, n);
			*sl = strlen(buf);
			*tok = PDF_TNAME;
			goto cleanupokay;
		}

		else if (c == '(')
		{
			fz_readbyte(f);
			*sl = lexstring(f, buf, n);
			*tok = PDF_TSTRING;
			goto cleanupokay;
		}

		else if (c == '<')
		{
			fz_readbyte(f);
			c = fz_peekbyte(f);
			if (c == '<')
			{
				fz_readbyte(f);
				*tok = PDF_TODICT;
				goto cleanupokay;
			}
			else
			{
				*sl = lexhexstring(f, buf, n);
				*tok = PDF_TSTRING;
				goto cleanupokay;
			}
		}

		else if (c == '>')
		{
			fz_readbyte(f);
			c = fz_readbyte(f);
			if (c == '>')
			{
				*tok = PDF_TCDICT;
				goto cleanupokay;
			}
			*tok = PDF_TERROR;
			goto cleanuperror;
		}

		else if (c == '[')
		{
			fz_readbyte(f);
			*tok = PDF_TOARRAY;
			goto cleanupokay;
		}

		else if (c == ']')
		{
			fz_readbyte(f);
			*tok = PDF_TCARRAY;
			goto cleanupokay;
		}

		else if (c == '{')
		{
			fz_readbyte(f);
			*tok = PDF_TOBRACE;
			goto cleanupokay;
		}

		else if (c == '}')
		{
			fz_readbyte(f);
			*tok = PDF_TCBRACE;
			goto cleanupokay;
		}

		else if (isnumber(c))
		{
			lexnumber(f, buf, n);
			*sl = strlen(buf);
			if (strchr(buf, '.'))
			{
				*tok = PDF_TREAL;
				goto cleanupokay;
			}
			*tok = PDF_TINT;
			goto cleanupokay;
		}

		else if (isregular(c))
		{
			lexname(f, buf, n);
			*sl = strlen(buf);
			*tok = pdf_tokenfromkeyword(buf);
			goto cleanupokay;
		}

		else
		{
			*tok = PDF_TERROR;
			goto cleanuperror;
		}
	}

cleanupokay:
	error = fz_readerror(f);
	if (error)
	{
		*tok = PDF_TERROR;
		return fz_rethrow(error, "cannot read token");
	}
	return fz_okay;

cleanuperror:
	error = fz_readerror(f);
	if (error)
	{
		*tok = PDF_TERROR;
		return fz_rethrow(error, "cannot read token");
	}
	*tok = PDF_TERROR;
	return fz_throw("lexical error");
}

