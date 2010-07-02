#include "fitz.h"
#include "mupdf.h"

static pdf_csi *
pdf_newcsi(fz_device *dev, pdf_xref *xref, fz_matrix ctm)
{
	pdf_csi *csi;

	csi = fz_malloc(sizeof(pdf_csi));
	csi->dev = dev;
	csi->xref = xref;

	csi->top = 0;
	csi->xbalance = 0;
	csi->array = nil;

	csi->path = fz_newpath();
	csi->clip = 0;
	csi->clipevenodd = 0;

	csi->text = nil;
	csi->tlm = fz_identity();
	csi->tm = fz_identity();
	csi->textmode = 0;
	csi->accumulate = 1;

	csi->topctm = ctm;
	pdf_initgstate(&csi->gstate[0], ctm);
	csi->gtop = 0;

	return csi;
}

static void
pdf_clearstack(pdf_csi *csi)
{
	int i;
	for (i = 0; i < csi->top; i++)
		fz_dropobj(csi->stack[i]);
	csi->top = 0;
}

pdf_material *
pdf_keepmaterial(pdf_material *mat)
{
	if (mat->cs)
		fz_keepcolorspace(mat->cs);
	if (mat->indexed)
		fz_keepcolorspace(&mat->indexed->super);
	if (mat->pattern)
		pdf_keeppattern(mat->pattern);
	if (mat->shade)
		fz_keepshade(mat->shade);
	return mat;
}

pdf_material *
pdf_dropmaterial(pdf_material *mat)
{
	if (mat->cs)
		fz_dropcolorspace(mat->cs);
	if (mat->indexed)
		fz_dropcolorspace(&mat->indexed->super);
	if (mat->pattern)
		pdf_droppattern(mat->pattern);
	if (mat->shade)
		fz_dropshade(mat->shade);
	return mat;
}

void
pdf_gsave(pdf_csi *csi)
{
	pdf_gstate *gs = csi->gstate + csi->gtop;

	if (csi->gtop == nelem(csi->gstate) - 1)
	{
		fz_warn("gstate overflow in content stream");
		return;
	}

	memcpy(&csi->gstate[csi->gtop + 1], &csi->gstate[csi->gtop], sizeof(pdf_gstate));

	csi->gtop ++;

	pdf_keepmaterial(&gs->stroke);
	pdf_keepmaterial(&gs->fill);
	if (gs->font)
		pdf_keepfont(gs->font);
}

void
pdf_grestore(pdf_csi *csi)
{
	pdf_gstate *gs = csi->gstate + csi->gtop;
	int clipdepth = gs->clipdepth;

	if (csi->gtop == 0)
	{
		fz_warn("gstate underflow in content stream");
		return;
	}

	pdf_dropmaterial(&gs->stroke);
	pdf_dropmaterial(&gs->fill);
	if (gs->font)
		pdf_dropfont(gs->font);

	csi->gtop --;

	gs = csi->gstate + csi->gtop;
	while (clipdepth > gs->clipdepth)
	{
		csi->dev->popclip(csi->dev->user);
		clipdepth--;
	}
}

static void
pdf_freecsi(pdf_csi *csi)
{
	while (csi->gtop)
		pdf_grestore(csi);

	pdf_dropmaterial(&csi->gstate[0].fill);
	pdf_dropmaterial(&csi->gstate[0].stroke);
	if (csi->gstate[0].font)
		pdf_dropfont(csi->gstate[0].font);

	while (csi->gstate[0].clipdepth--)
		csi->dev->popclip(csi->dev->user);

	if (csi->path) fz_freepath(csi->path);
	if (csi->text) fz_freetext(csi->text);
	if (csi->array) fz_dropobj(csi->array);

	pdf_clearstack(csi);

	fz_free(csi);
}

/*
 * Do some magic to call the xobject subroutine.
 * Push gstate, set transform, clip, run, pop gstate.
 */

static fz_error
pdf_runxobject(pdf_csi *csi, fz_obj *resources, pdf_xobject *xobj)
{
	fz_error error;
	pdf_gstate *gstate;
	fz_matrix oldtopctm;
	int oldtop;

	pdf_gsave(csi);

	gstate = csi->gstate + csi->gtop;
	oldtop = csi->gtop;

	gstate->stroke.parentalpha = gstate->stroke.alpha;
	gstate->fill.parentalpha = gstate->fill.alpha;

	/* reset alpha to 1.0 when starting a new Transparency group */
	if (xobj->transparency)
	{
		gstate->blendmode = FZ_BNORMAL;
		gstate->stroke.alpha = gstate->stroke.parentalpha;
		gstate->fill.alpha = gstate->fill.parentalpha;
	}

	/* apply xobject's transform matrix */
	gstate->ctm = fz_concat(xobj->matrix, gstate->ctm);

	if (xobj->isolated || xobj->knockout)
	{
		/* The xobject's contents ought to be blended properly,
		but for now, just do over and hope for something */
		// TODO: push, pop and blend buffers
	}

	/* clip to the bounds */

	fz_moveto(csi->path, xobj->bbox.x0, xobj->bbox.y0);
	fz_lineto(csi->path, xobj->bbox.x1, xobj->bbox.y0);
	fz_lineto(csi->path, xobj->bbox.x1, xobj->bbox.y1);
	fz_lineto(csi->path, xobj->bbox.x0, xobj->bbox.y1);
	fz_closepath(csi->path);
	csi->clip = 1;
	pdf_showpath(csi, 0, 0, 0, 0);

	/* run contents */

	oldtopctm = csi->topctm;
	csi->topctm = gstate->ctm;

	if (xobj->resources)
		resources = xobj->resources;

	error = pdf_runcsibuffer(csi, resources, xobj->contents);
	if (error)
		return fz_rethrow(error, "cannot interpret XObject stream");

	csi->topctm = oldtopctm;

	while (oldtop < csi->gtop)
		pdf_grestore(csi);

	pdf_grestore(csi);

	return fz_okay;
}

/*
 * Decode inline image and insert into page.
 */

static fz_error
pdf_runinlineimage(pdf_csi *csi, fz_obj *rdb, fz_stream *file, fz_obj *dict)
{
	fz_error error;
	pdf_image *img;
	char buf[256];
	pdf_token_e tok;
	int len;

	error = pdf_loadinlineimage(&img, csi->xref, rdb, dict, file);
	if (error)
		return fz_rethrow(error, "cannot load inline image");

	error = pdf_lex(&tok, file, buf, sizeof buf, &len);
	if (error)
	{
		pdf_dropimage(img);
		return fz_rethrow(error, "syntax error after inline image");
	}

	if (tok != PDF_TKEYWORD || strcmp("EI", buf))
	{
		pdf_dropimage(img);
		return fz_throw("syntax error after inline image");
	}

	pdf_showimage(csi, img);

	pdf_dropimage(img);
	return fz_okay;
}

/*
 * Set gstate params from an ExtGState dictionary.
 */

static fz_error
pdf_runextgstate(pdf_gstate *gstate, pdf_xref *xref, fz_obj *rdb, fz_obj *extgstate)
{
	int i, k;

	for (i = 0; i < fz_dictlen(extgstate); i++)
	{
		fz_obj *key = fz_dictgetkey(extgstate, i);
		fz_obj *val = fz_dictgetval(extgstate, i);
		char *s = fz_toname(key);

		if (!strcmp(s, "Font"))
		{
			if (fz_isarray(val) && fz_arraylen(val) == 2)
			{
				fz_error error;
				fz_obj *font = fz_arrayget(val, 0);

				if (gstate->font)
				{
					pdf_dropfont(gstate->font);
					gstate->font = nil;
				}

				error = pdf_loadfont(&gstate->font, xref, rdb, font);
				if (error)
					return fz_rethrow(error, "cannot load font (%d %d R)", fz_tonum(font), fz_togen(font));
				if (!gstate->font)
					return fz_throw("cannot find font in store");
				gstate->size = fz_toreal(fz_arrayget(val, 1));
			}
			else
				return fz_throw("malformed /Font dictionary");
		}

		else if (!strcmp(s, "LW"))
			gstate->strokestate.linewidth = fz_toreal(val);
		else if (!strcmp(s, "LC"))
			gstate->strokestate.linecap = fz_toint(val);
		else if (!strcmp(s, "LJ"))
			gstate->strokestate.linejoin = fz_toint(val);
		else if (!strcmp(s, "ML"))
			gstate->strokestate.miterlimit = fz_toreal(val);

		else if (!strcmp(s, "D"))
		{
			if (fz_isarray(val) && fz_arraylen(val) == 2)
			{
				fz_obj *dashes = fz_arrayget(val, 0);
				gstate->strokestate.dashlen = MAX(fz_arraylen(dashes), 32);
				for (k = 0; k < gstate->strokestate.dashlen; k++)
					gstate->strokestate.dashlist[k] = fz_toreal(fz_arrayget(dashes, k));
				gstate->strokestate.dashphase = fz_toreal(fz_arrayget(val, 1));
			}
			else
				return fz_throw("malformed /D");
		}

		else if (!strcmp(s, "CA"))
			gstate->stroke.alpha = gstate->stroke.parentalpha * fz_toreal(val);
		else if (!strcmp(s, "ca"))
			gstate->fill.alpha = gstate->fill.parentalpha * fz_toreal(val);

		else if (!strcmp(s, "BM"))
		{
			static const struct { const char *name; fz_blendkind mode; } bm[] = {
				{ "Normal", FZ_BNORMAL },
				{ "Multiply", FZ_BMULTIPLY },
				{ "Screen", FZ_BSCREEN },
				{ "Overlay", FZ_BOVERLAY },
				{ "Darken", FZ_BDARKEN },
				{ "Lighten", FZ_BLIGHTEN },
				{ "Colordodge", FZ_BCOLORDODGE },
				{ "Hardlight", FZ_BHARDLIGHT },
				{ "Softlight", FZ_BSOFTLIGHT },
				{ "Difference", FZ_BDIFFERENCE },
				{ "Exclusion", FZ_BEXCLUSION },
				{ "Hue", FZ_BHUE },
				{ "Saturation", FZ_BSATURATION },
				{ "Color", FZ_BCOLOR },
				{ "Luminosity", FZ_BLUMINOSITY }
			};

			char *n = fz_toname(val);
			if (!fz_isname(val))
				return fz_throw("malformed BM");

			gstate->blendmode = FZ_BNORMAL;
			for (k = 0; k < nelem(bm); k++) {
				if (!strcmp(bm[k].name, n)) {
					gstate->blendmode = bm[k].mode;
					if (gstate->blendmode != FZ_BNORMAL)
						fz_warn("ignoring blend mode %s", n);
					break;
				}
			}

			/* The content stream ought to be blended properly,
			but for now, just do over and hope for something
			*/
			// TODO: push, pop and blend buffers
		}

		else if (!strcmp(s, "SMask"))
		{
			if (fz_isdict(val))
			{
				/* TODO: we should do something here, like inserting a mask node for the S key in val */
				/* TODO: how to deal with the non-recursive nature of pdf soft masks? */
				/*
				fz_error error;
				fz_obj *g;
				fz_obj *subtype;
				pdf_xobject *xobj;
				pdf_image *img;

				g = fz_dictgets(val, "G");
				subtype = fz_dictgets(g, "Subtype");

				if (!strcmp(fz_toname(subtype), "Form"))
				{
				error = pdf_loadxobject(&xobj, xref, g);
				if (error)
				return fz_rethrow(error, "cannot load xobject (%d %d R)", fz_tonum(val), fz_togen(val));
				}

				else if (!strcmp(fz_toname(subtype), "Image"))
				{
				error = pdf_loadimage(&img, xref, g);
				if (error)
				return fz_rethrow(error, "cannot load xobject (%d %d R)", fz_tonum(val), fz_togen(val));
				}
				*/
			}
		}

		else if (!strcmp(s, "TR"))
		{
			if (fz_isname(val) && strcmp(fz_toname(val), "Identity"))
				fz_warn("ignoring transfer function");
		}
	}

	return fz_okay;
}

/*
 * The meat of the interpreter...
 */

static fz_error
pdf_runkeyword(pdf_csi *csi, fz_obj *rdb, char *buf)
{
	pdf_gstate *gstate = csi->gstate + csi->gtop;
	fz_error error;
	float a, b, c, d, e, f;
	float x, y, w, h;
	fz_matrix m;
	float v[FZ_MAXCOLORS];
	int what;
	int i;

	switch (buf[0])
	{
	case 'B':
		switch(buf[1])
		{
		case 0:
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 0, 1, 1, 0);
			break;
		case 'D':
			if ((buf[2] != 'C') || (buf[3] != 0))
				goto defaultcase;
			if (csi->top < 2)
				goto syntaxerror;
			break;
		case 'M':
			if ((buf[2] != 'C') || (buf[3] != 0))
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			break;
		case 'T':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			csi->tm = fz_identity();
			csi->tlm = fz_identity();
			break;
		case 'X':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			csi->xbalance ++;
			break;
		case '*':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 0, 1, 1, 1);
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'C':
		if ((buf[1] == 'S') && (buf[2] == 0))
		{
			fz_colorspace *cs;
			fz_obj *obj;

			what = PDF_MSTROKE;

Lsetcolorspace:
			if (csi->top < 1)
				goto syntaxerror;

			obj = csi->stack[0];

			if (!fz_isname(obj))
				return fz_throw("malformed CS");

			if (!strcmp(fz_toname(obj), "Pattern"))
			{
				pdf_setpattern(csi, what, nil, nil);
			}

			else
			{
				if (!strcmp(fz_toname(obj), "DeviceGray"))
					cs = fz_keepcolorspace(pdf_devicegray);
				else if (!strcmp(fz_toname(obj), "DeviceRGB"))
					cs = fz_keepcolorspace(pdf_devicergb);
				else if (!strcmp(fz_toname(obj), "DeviceCMYK"))
					cs = fz_keepcolorspace(pdf_devicecmyk);
				else
				{
					fz_obj *dict = fz_dictgets(rdb, "ColorSpace");
					if (!dict)
						return fz_throw("cannot find ColorSpace dictionary");
					obj = fz_dictget(dict, obj);
					if (!obj)
						return fz_throw("cannot find colorspace resource /%s", fz_toname(csi->stack[0]));

					error = pdf_loadcolorspace(&cs, csi->xref, obj);
					if (error)
						return fz_rethrow(error, "cannot load colorspace (%d %d R)", fz_tonum(obj), fz_togen(obj));
				}

				pdf_setcolorspace(csi, what, cs);

				fz_dropcolorspace(cs);
			}
		}
		else
			goto defaultcase;
		break;

	case 'D':
		switch (buf[1])
		{
		case 'P':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 2)
				goto syntaxerror;
			break;
		case 'o':
		{
			fz_obj *dict;
			fz_obj *obj;
			fz_obj *subtype;

			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;

			dict = fz_dictgets(rdb, "XObject");
			if (!dict)
				return fz_throw("cannot find XObject dictionary when looking for: '%s'", fz_toname(csi->stack[0]));

			obj = fz_dictget(dict, csi->stack[0]);
			if (!obj)
				return fz_throw("cannot find xobject resource: '%s'", fz_toname(csi->stack[0]));

			subtype = fz_dictgets(obj, "Subtype");
			if (!subtype)
				return fz_throw("no XObject subtype specified");

			if (!strcmp(fz_toname(subtype), "Form") && fz_dictgets(obj, "Subtype2"))
				subtype = fz_dictgets(obj, "Subtype2");

			if (!strcmp(fz_toname(subtype), "Form"))
			{
				pdf_xobject *xobj;

				error = pdf_loadxobject(&xobj, csi->xref, obj);
				if (error)
					return fz_rethrow(error, "cannot load xobject (%d %d R)", fz_tonum(obj), fz_togen(obj));

				/* Inherit parent resources, in case this one was empty XXX check where it's loaded */
				if (!xobj->resources)
					xobj->resources = fz_keepobj(rdb);

				error = pdf_runxobject(csi, rdb, xobj);
				if (error)
					return fz_rethrow(error, "cannot draw xobject (%d %d R)", fz_tonum(obj), fz_togen(obj));

				pdf_dropxobject(xobj);
			}

			else if (!strcmp(fz_toname(subtype), "Image"))
			{
				if ((csi->dev->hints & FZ_IGNOREIMAGE) == 0)
				{
					pdf_image *img;
					error = pdf_loadimage(&img, csi->xref, obj);
					if (error)
						return fz_rethrow(error, "cannot load image (%d %d R)", fz_tonum(obj), fz_togen(obj));
					pdf_showimage(csi, img);
					pdf_dropimage(img);
				}
			}

			else if (!strcmp(fz_toname(subtype), "PS"))
			{
				fz_warn("ignoring XObject with subtype PS");
			}

			else
			{
				return fz_throw("unknown XObject subtype: %s", fz_toname(subtype));
			}
			break;
		}
		default:
			goto defaultcase;
		}
		break;

	case 'E':
		switch (buf[1])
		{
		case 'X':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			csi->xbalance --;
			break;
		case 'M':
			if ((buf[2] != 'C') || (buf[3] != 0))
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			break;
		case 'T':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			pdf_flushtext(csi);
			csi->accumulate = 1;
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'F':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 0)
			goto syntaxerror;
		pdf_showpath(csi, 0, 1, 0, 0);
		break;

	case 'G':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 1)
			goto syntaxerror;

		v[0] = fz_toreal(csi->stack[0]);
		pdf_setcolorspace(csi, PDF_MSTROKE, pdf_devicegray);
		pdf_setcolor(csi, PDF_MSTROKE, v);
		break;

	case 'J':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 1)
			goto syntaxerror;
		gstate->strokestate.linecap = fz_toint(csi->stack[0]);
		break;

	case 'K':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 4)
			goto syntaxerror;

		v[0] = fz_toreal(csi->stack[0]);
		v[1] = fz_toreal(csi->stack[1]);
		v[2] = fz_toreal(csi->stack[2]);
		v[3] = fz_toreal(csi->stack[3]);

		pdf_setcolorspace(csi, PDF_MSTROKE, pdf_devicecmyk);
		pdf_setcolor(csi, PDF_MSTROKE, v);
		break;

	case 'M':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 1)
				goto syntaxerror;
			gstate->strokestate.miterlimit = fz_toreal(csi->stack[0]);
			break;
		case 'P':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'Q':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 0)
			goto syntaxerror;
		pdf_grestore(csi);
		break;

	case 'R':
		if ((buf[1] != 'G') || (buf[2] != 0))
			goto defaultcase;
		if (csi->top < 3)
			goto syntaxerror;

		v[0] = fz_toreal(csi->stack[0]);
		v[1] = fz_toreal(csi->stack[1]);
		v[2] = fz_toreal(csi->stack[2]);

		pdf_setcolorspace(csi, PDF_MSTROKE, pdf_devicergb);
		pdf_setcolor(csi, PDF_MSTROKE, v);
		break;

	case 'S':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 0, 0, 1, 0);
			break;
		case 'C':
		{
			/* SC or SCN */
			pdf_material *mat;
			fz_obj *patterntype;
			fz_obj *dict;
			fz_obj *obj;
			int kind;

			if ((buf[2] != 0) && ((buf[2] != 'N') || (buf[3] != 0)))
				goto defaultcase;

			what = PDF_MSTROKE;

Lsetcolor:
			mat = what == PDF_MSTROKE ? &gstate->stroke : &gstate->fill;

			kind = mat->kind;
			if (fz_isname(csi->stack[csi->top - 1]))
				kind = PDF_MPATTERN;

			switch (kind)
			{
			case PDF_MNONE:
				return fz_throw("cannot set color in mask objects");

			case PDF_MINDEXED:
				if (csi->top < 1)
					goto syntaxerror;
				v[0] = fz_toreal(csi->stack[0]);
				pdf_setcolor(csi, what, v);
				break;

			case PDF_MCOLOR:
			case PDF_MLAB:
				if (csi->top < mat->cs->n)
					goto syntaxerror;
				for (i = 0; i < csi->top; i++)
					v[i] = fz_toreal(csi->stack[i]);
				pdf_setcolor(csi, what, v);
				break;

			case PDF_MPATTERN:
				for (i = 0; i < csi->top - 1; i++)
					v[i] = fz_toreal(csi->stack[i]);

				dict = fz_dictgets(rdb, "Pattern");
				if (!dict)
					return fz_throw("cannot find Pattern dictionary");

				obj = fz_dictget(dict, csi->stack[csi->top - 1]);
				if (!obj)
					return fz_throw("cannot find pattern resource /%s",
							fz_toname(csi->stack[csi->top - 1]));

				patterntype = fz_dictgets(obj, "PatternType");

				if (fz_toint(patterntype) == 1)
				{
					pdf_pattern *pat;
					error = pdf_loadpattern(&pat, csi->xref, obj);
					if (error)
						return fz_rethrow(error, "cannot load pattern (%d %d R)", fz_tonum(obj), fz_togen(obj));
					pdf_setpattern(csi, what, pat, csi->top == 1 ? nil : v);
					pdf_droppattern(pat);
				}

				else if (fz_toint(patterntype) == 2)
				{
					if ((csi->dev->hints & FZ_IGNORESHADE) == 0)
					{
						fz_shade *shd;
						error = pdf_loadshading(&shd, csi->xref, obj);
						if (error)
							return fz_rethrow(error, "cannot load shading (%d %d R)", fz_tonum(obj), fz_togen(obj));
						pdf_setshade(csi, what, shd);
						fz_dropshade(shd);
					}
				}

				else
				{
					return fz_throw("unknown pattern type: %d", fz_toint(patterntype));
				}

				break;

			case PDF_MSHADE:
				return fz_throw("cannot set color in shade objects");
			}
			break;
		}
		default:
			goto defaultcase;
		}
		break;

	case 'T':
		switch (buf[1])
		{
		case 'c':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			gstate->charspace = fz_toreal(csi->stack[0]);
			break;
		case 'w':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			gstate->wordspace = fz_toreal(csi->stack[0]);
			break;
		case 'z':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			a = fz_toreal(csi->stack[0]) / 100;
			pdf_flushtext(csi);
			gstate->scale = a;
			break;
		case 'L':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			gstate->leading = fz_toreal(csi->stack[0]);
			break;
		case 'f':
		{
			fz_obj *dict;
			fz_obj *obj;

			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 2)
				goto syntaxerror;

			dict = fz_dictgets(rdb, "Font");
			if (!dict)
				return fz_throw("cannot find Font dictionary");

			obj = fz_dictget(dict, csi->stack[0]);
			if (!obj)
				return fz_throw("cannot find font resource: %s", fz_toname(csi->stack[0]));

			if (gstate->font)
			{
				pdf_dropfont(gstate->font);
				gstate->font = nil;
			}

			error = pdf_loadfont(&gstate->font, csi->xref, rdb, obj);
			if (error)
				return fz_rethrow(error, "cannot load font (%d %d R)", fz_tonum(obj), fz_togen(obj));

			gstate->size = fz_toreal(csi->stack[1]);

			break;
		}
		case 'r':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			gstate->render = fz_toint(csi->stack[0]);
			break;
		case 's':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			gstate->rise = fz_toreal(csi->stack[0]);
			break;
		case 'd':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 2)
				goto syntaxerror;
			m = fz_translate(fz_toreal(csi->stack[0]), fz_toreal(csi->stack[1]));
			csi->tlm = fz_concat(m, csi->tlm);
			csi->tm = csi->tlm;
			break;
		case 'D':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 2)
				goto syntaxerror;
			gstate->leading = -fz_toreal(csi->stack[1]);
			m = fz_translate(fz_toreal(csi->stack[0]), fz_toreal(csi->stack[1]));
			csi->tlm = fz_concat(m, csi->tlm);
			csi->tm = csi->tlm;
			break;
		case 'm':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 6)
				goto syntaxerror;

			m.a = fz_toreal(csi->stack[0]);
			m.b = fz_toreal(csi->stack[1]);
			m.c = fz_toreal(csi->stack[2]);
			m.d = fz_toreal(csi->stack[3]);
			m.e = fz_toreal(csi->stack[4]);
			m.f = fz_toreal(csi->stack[5]);

			pdf_flushtext(csi);

			csi->tm = m;
			csi->tlm = csi->tm;
			break;
		case '*':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			m = fz_translate(0, -gstate->leading);
			csi->tlm = fz_concat(m, csi->tlm);
			csi->tm = csi->tlm;
			break;
		case 'j':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			pdf_showtext(csi, csi->stack[0]);
			break;
		case 'J':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			pdf_showtext(csi, csi->stack[0]);
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'W':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 0)
				goto syntaxerror;
			csi->clip = 1;
			csi->clipevenodd = 0;
			break;
		case '*':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			csi->clip = 1;
			csi->clipevenodd = 1;
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'b':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 1, 1, 1, 0);
			break;
		case '*':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 1, 1, 1, 1);
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'c':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 6)
				goto syntaxerror;
			a = fz_toreal(csi->stack[0]);
			b = fz_toreal(csi->stack[1]);
			c = fz_toreal(csi->stack[2]);
			d = fz_toreal(csi->stack[3]);
			e = fz_toreal(csi->stack[4]);
			f = fz_toreal(csi->stack[5]);
			fz_curveto(csi->path, a, b, c, d, e, f);
			break;
		case 'm':
		{
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 6)
				goto syntaxerror;

			m.a = fz_toreal(csi->stack[0]);
			m.b = fz_toreal(csi->stack[1]);
			m.c = fz_toreal(csi->stack[2]);
			m.d = fz_toreal(csi->stack[3]);
			m.e = fz_toreal(csi->stack[4]);
			m.f = fz_toreal(csi->stack[5]);

			gstate->ctm = fz_concat(m, gstate->ctm);
			break;
		}
		case 's':
			if (buf[2] != 0)
				goto defaultcase;
			what = PDF_MFILL;
			goto Lsetcolorspace;
		default:
			goto defaultcase;
		}
		break;

	case 'd':
		switch (buf[1])
		{
		case 0:
		{
			int i;
			fz_obj *array;
			if (csi->top < 2)
				goto syntaxerror;
			array = csi->stack[0];
			gstate->strokestate.dashlen = fz_arraylen(array);
			if (gstate->strokestate.dashlen > 32)
				return fz_throw("assert: dash pattern too big");
			for (i = 0; i < gstate->strokestate.dashlen; i++)
				gstate->strokestate.dashlist[i] = fz_toreal(fz_arrayget(array, i));
			gstate->strokestate.dashphase = fz_toreal(csi->stack[1]);
			break;
		}
		case '0':
		case '1':
			if (buf[2] != 0)
				goto defaultcase;
			/* we don't care about setcharwidth and setcachedevice */
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'f':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 0, 1, 0, 0);
			break;
		case '*':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 0, 1, 0, 1);
			break;
		default:
			goto defaultcase;
		}
		break;

	case 'g':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 1)
				goto syntaxerror;

			v[0] = fz_toreal(csi->stack[0]);
			pdf_setcolorspace(csi, PDF_MFILL, pdf_devicegray);
			pdf_setcolor(csi, PDF_MFILL, v);
			break;
		case 's':
		{
			fz_obj *dict;
			fz_obj *obj;

			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;

			dict = fz_dictgets(rdb, "ExtGState");
			if (!dict)
				return fz_throw("cannot find ExtGState dictionary");

			obj = fz_dictget(dict, csi->stack[0]);
			if (!obj)
				return fz_throw("cannot find extgstate resource /%s", fz_toname(csi->stack[0]));

			error = pdf_runextgstate(gstate, csi->xref, rdb, obj);
			if (error)
				return fz_rethrow(error, "cannot set ExtGState (%d %d R)", fz_tonum(obj), fz_togen(obj));
			break;
		}
		default:
			goto defaultcase;
		}
		break;

	case 'h':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 0)
			goto syntaxerror;
		fz_closepath(csi->path);
		break;

	case 'i':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 1)
			goto syntaxerror;
		/* flatness */
		break;

	case 'j':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 1)
			goto syntaxerror;
		gstate->strokestate.linejoin = fz_toint(csi->stack[0]);
		break;

	case 'k':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 4)
			goto syntaxerror;

		v[0] = fz_toreal(csi->stack[0]);
		v[1] = fz_toreal(csi->stack[1]);
		v[2] = fz_toreal(csi->stack[2]);
		v[3] = fz_toreal(csi->stack[3]);

		pdf_setcolorspace(csi, PDF_MFILL, pdf_devicecmyk);
		pdf_setcolor(csi, PDF_MFILL, v);
		break;

	case 'l':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 2)
			goto syntaxerror;
		a = fz_toreal(csi->stack[0]);
		b = fz_toreal(csi->stack[1]);
		fz_lineto(csi->path, a, b);
		break;

	case 'm':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 2)
			goto syntaxerror;
		a = fz_toreal(csi->stack[0]);
		b = fz_toreal(csi->stack[1]);
		fz_moveto(csi->path, a, b);
		break;

	case 'n':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 0)
			goto syntaxerror;
		pdf_showpath(csi, 0, 0, 0, csi->clipevenodd);
		break;

	case 'q':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 0)
			goto syntaxerror;
		pdf_gsave(csi);
		break;

	case 'r':
		switch (buf[1])
		{
		case 'i':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;
			break;
		case 'e':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 4)
				goto syntaxerror;

			x = fz_toreal(csi->stack[0]);
			y = fz_toreal(csi->stack[1]);
			w = fz_toreal(csi->stack[2]);
			h = fz_toreal(csi->stack[3]);

			fz_moveto(csi->path, x, y);
			fz_lineto(csi->path, x + w, y);
			fz_lineto(csi->path, x + w, y + h);
			fz_lineto(csi->path, x, y + h);
			fz_closepath(csi->path);
			break;
		case 'g':
			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 3)
				goto syntaxerror;

			v[0] = fz_toreal(csi->stack[0]);
			v[1] = fz_toreal(csi->stack[1]);
			v[2] = fz_toreal(csi->stack[2]);

			pdf_setcolorspace(csi, PDF_MFILL, pdf_devicergb);
			pdf_setcolor(csi, PDF_MFILL, v);
			break;
		default:
			goto defaultcase;
		}
		break;

	case 's':
		switch (buf[1])
		{
		case 0:
			if (csi->top < 0)
				goto syntaxerror;
			pdf_showpath(csi, 1, 0, 1, 0);
			break;
		case 'c':
			if ((buf[2] != 0) && ((buf[2] != 'n') || (buf[3] != 0)))
				goto defaultcase;
			what = PDF_MFILL;
			goto Lsetcolor;
		case 'h':
		{
			fz_obj *dict;
			fz_obj *obj;
			fz_shade *shd;

			if (buf[2] != 0)
				goto defaultcase;
			if (csi->top < 1)
				goto syntaxerror;

			dict = fz_dictgets(rdb, "Shading");
			if (!dict)
				return fz_throw("cannot find shading dictionary");

			obj = fz_dictget(dict, csi->stack[csi->top - 1]);
			if (!obj)
				return fz_throw("cannot find shading resource: %s", fz_toname(csi->stack[csi->top - 1]));

			if ((csi->dev->hints & FZ_IGNORESHADE) == 0)
			{
				error = pdf_loadshading(&shd, csi->xref, obj);
				if (error)
					return fz_rethrow(error, "cannot load shading (%d %d R)", fz_tonum(obj), fz_togen(obj));
				pdf_showshade(csi, shd);
				fz_dropshade(shd);
			}
			break;
		}
		default:
			goto defaultcase;
		}
		break;

	case 'v':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 4)
			goto syntaxerror;
		a = fz_toreal(csi->stack[0]);
		b = fz_toreal(csi->stack[1]);
		c = fz_toreal(csi->stack[2]);
		d = fz_toreal(csi->stack[3]);
		fz_curvetov(csi->path, a, b, c, d);
		break;

	case 'w':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 1)
			goto syntaxerror;
		gstate->strokestate.linewidth = fz_toreal(csi->stack[0]);
		break;

	case 'y':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 4)
			goto syntaxerror;
		a = fz_toreal(csi->stack[0]);
		b = fz_toreal(csi->stack[1]);
		c = fz_toreal(csi->stack[2]);
		d = fz_toreal(csi->stack[3]);
		fz_curvetoy(csi->path, a, b, c, d);
		break;

	case '\'':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 1)
			goto syntaxerror;

		m = fz_translate(0, -gstate->leading);
		csi->tlm = fz_concat(m, csi->tlm);
		csi->tm = csi->tlm;

		pdf_showtext(csi, csi->stack[0]);
		break;

	case '"':
		if (buf[1] != 0)
			goto defaultcase;
		if (csi->top < 3)
			goto syntaxerror;

		gstate->wordspace = fz_toreal(csi->stack[0]);
		gstate->charspace = fz_toreal(csi->stack[1]);

		m = fz_translate(0, -gstate->leading);
		csi->tlm = fz_concat(m, csi->tlm);
		csi->tm = csi->tlm;

		pdf_showtext(csi, csi->stack[2]);
		break;
	default:
defaultcase:
		/* don't fail on unknown keywords if braced by BX/EX */
		if (!csi->xbalance)
			fz_warn("unknown keyword: %s", buf);
		break;
	}

	return fz_okay;

syntaxerror:
	return fz_throw("syntaxerror near '%s'", buf);
}

static fz_error
pdf_runcsifile(pdf_csi *csi, fz_obj *rdb, fz_stream *file, char *buf, int buflen)
{
	fz_error error;
	pdf_token_e tok;
	int len;
	fz_obj *obj;

	pdf_clearstack(csi);

	while (1)
	{
		if (csi->top == nelem(csi->stack) - 1)
			return fz_throw("stack overflow");

		error = pdf_lex(&tok, file, buf, buflen, &len);
		if (error)
			return fz_rethrow(error, "lexical error in content stream");

		if (csi->array)
		{
			if (tok == PDF_TCARRAY)
			{
				csi->stack[csi->top] = csi->array;
				csi->array = nil;
				csi->top ++;
			}
			else if (tok == PDF_TINT || tok == PDF_TREAL)
			{
				obj = fz_newreal(atof(buf));
				fz_arraypush(csi->array, obj);
				fz_dropobj(obj);
			}
			else if (tok == PDF_TSTRING)
			{
				obj = fz_newstring(buf, len);
				fz_arraypush(csi->array, obj);
				fz_dropobj(obj);
			}
			else if (tok == PDF_TEOF)
			{
				return fz_okay;
			}
			else
			{
				pdf_clearstack(csi);
				return fz_throw("syntaxerror in array");
			}
		}

		else switch (tok)
		{
		case PDF_TENDSTREAM:
		case PDF_TEOF:
			return fz_okay;

			/* optimize text-object array parsing */
		case PDF_TOARRAY:
			csi->array = fz_newarray(8);
			break;

		case PDF_TODICT:
			error = pdf_parsedict(&csi->stack[csi->top], csi->xref, file, buf, buflen);
			if (error)
				return fz_rethrow(error, "cannot parse dictionary");
			csi->top ++;
			break;

		case PDF_TNAME:
			csi->stack[csi->top] = fz_newname(buf);
			csi->top ++;
			break;

		case PDF_TINT:
			csi->stack[csi->top] = fz_newint(atoi(buf));
			csi->top ++;
			break;

		case PDF_TREAL:
			csi->stack[csi->top] = fz_newreal(atof(buf));
			csi->top ++;
			break;

		case PDF_TSTRING:
			csi->stack[csi->top] = fz_newstring(buf, len);
			csi->top ++;
			break;

		case PDF_TTRUE:
			csi->stack[csi->top] = fz_newbool(1);
			csi->top ++;
			break;

		case PDF_TFALSE:
			csi->stack[csi->top] = fz_newbool(0);
			csi->top ++;
			break;

		case PDF_TNULL:
			csi->stack[csi->top] = fz_newnull();
			csi->top ++;
			break;

		case PDF_TKEYWORD:
			if (!strcmp(buf, "BI"))
			{
				fz_obj *obj;
				int ch;

				error = pdf_parsedict(&obj, csi->xref, file, buf, buflen);
				if (error)
					return fz_rethrow(error, "cannot parse inline image dictionary");

				/* read whitespace after ID keyword */
				ch = fz_readbyte(file);
				if (ch == '\r')
					if (fz_peekbyte(file) == '\n')
						fz_readbyte(file);
				error = fz_readerror(file);
				if (error)
					return fz_rethrow(error, "cannot parse whitespace before inline image");

				error = pdf_runinlineimage(csi, rdb, file, obj);
				fz_dropobj(obj);
				if (error)
					return fz_rethrow(error, "cannot parse inline image");
			}
			else
			{
				error = pdf_runkeyword(csi, rdb, buf);
				if (error)
					return fz_rethrow(error, "cannot run '%s'", buf);
				pdf_clearstack(csi);
			}
			break;

		default:
			pdf_clearstack(csi);
			return fz_throw("syntaxerror in content stream");
		}
	}
}

fz_error
pdf_runcsibuffer(pdf_csi *csi, fz_obj *rdb, fz_buffer *contents)
{
	fz_stream *file;
	fz_error error;

	contents->rp = contents->bp;
	file = fz_openbuffer(contents);
	error = pdf_runcsifile(csi, rdb, file, csi->xref->scratch, sizeof csi->xref->scratch);
	fz_dropstream(file);
	contents->rp = contents->bp;
	if (error)
		return fz_rethrow(error, "cannot parse content stream");
	return fz_okay;
}

fz_error
pdf_runcontentstream(fz_device *dev, fz_matrix ctm,
	pdf_xref *xref, fz_obj *resources, fz_buffer *contents)
{
	pdf_csi *csi = pdf_newcsi(dev, xref, ctm);
	fz_error error = pdf_runcsibuffer(csi, resources, contents);
	pdf_freecsi(csi);
	if (error)
		return fz_rethrow(error, "cannot parse content stream");
	return fz_okay;
}
