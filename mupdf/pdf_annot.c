#include "fitz.h"
#include "mupdf.h"

void
pdf_freelink(pdf_link *link)
{
	if (link->next)
		pdf_freelink(link->next);
	if (link->dest)
		fz_dropobj(link->dest);
	fz_free(link);
}

static fz_obj *
resolvedest(pdf_xref *xref, fz_obj *dest)
{
	if (fz_isname(dest) || fz_isstring(dest))
	{
		dest = pdf_lookupdest(xref, dest);
		return resolvedest(xref, dest);
	}

	else if (fz_isarray(dest))
	{
		return fz_arrayget(dest, 0);
	}

	else if (fz_isdict(dest))
	{
		dest = fz_dictgets(dest, "D");
		return resolvedest(xref, dest);
	}

	else if (fz_isindirect(dest))
		return dest;

	return nil;
}

pdf_link *
pdf_loadlink(pdf_xref *xref, fz_obj *dict)
{
	fz_obj *dest;
	fz_obj *action;
	fz_obj *obj;
	fz_rect bbox;
	pdf_linkkind kind;

	pdf_logpage("load link {\n");

	dest = nil;

	obj = fz_dictgets(dict, "Rect");
	if (obj)
	{
		bbox = pdf_torect(obj);
		pdf_logpage("rect [%g %g %g %g]\n",
			bbox.x0, bbox.y0,
			bbox.x1, bbox.y1);
	}
	else
		bbox = fz_emptyrect;

	obj = fz_dictgets(dict, "Dest");
	if (obj)
	{
		kind = PDF_LGOTO;
		dest = resolvedest(xref, obj);
		pdf_logpage("dest (%d %d R)\n", fz_tonum(dest), fz_togen(dest));
	}

	action = fz_dictgets(dict, "A");
	if (action)
	{
		obj = fz_dictgets(action, "S");
		if (fz_isname(obj) && !strcmp(fz_toname(obj), "GoTo"))
		{
			kind = PDF_LGOTO;
			dest = resolvedest(xref, fz_dictgets(action, "D"));
			pdf_logpage("action goto (%d %d R)\n", fz_tonum(dest), fz_togen(dest));
		}
		else if (fz_isname(obj) && !strcmp(fz_toname(obj), "URI"))
		{
			kind = PDF_LURI;
			dest = fz_dictgets(action, "URI");
			pdf_logpage("action uri %s\n", fz_tostrbuf(dest));
		}
		else
		{
			pdf_logpage("unhandled link action, ignoring link\n");
			dest = nil;
		}
	}

	pdf_logpage("}\n");

	if (dest)
	{
		pdf_link *link = fz_malloc(sizeof(pdf_link));
		link->kind = kind;
		link->rect = bbox;
		link->dest = fz_keepobj(dest);
		link->next = nil;
		return link;
	}

	return nil;
}

void
pdf_loadlinks(pdf_link **linkp, pdf_xref *xref, fz_obj *annots)
{
	pdf_link *link;
	fz_obj *subtype;
	fz_obj *obj;
	int i;

	link = nil;

	pdf_logpage("load link annotations {\n");

	for (i = 0; i < fz_arraylen(annots); i++)
	{
		obj = fz_arrayget(annots, i);

		subtype = fz_dictgets(obj, "Subtype");
		if (fz_isname(subtype) && !strcmp(fz_toname(subtype), "Link"))
		{
			pdf_link *temp = pdf_loadlink(xref, obj);
			if (temp)
			{
				temp->next = link;
				link = temp;
			}
		}
	}

	pdf_logpage("}\n");

	*linkp = link;
}

void
pdf_freeannot(pdf_annot *annot)
{
	if (annot->next)
		pdf_freeannot(annot->next);
	if (annot->ap)
		pdf_dropxobject(annot->ap);
	if (annot->obj)
		fz_dropobj(annot->obj);
	fz_free(annot);
}

void
pdf_transformannot(pdf_annot *annot)
{
	fz_matrix matrix = annot->ap->matrix;
	fz_rect bbox = annot->ap->bbox;
	fz_rect rect = annot->rect;
	float w, h, x, y;
	fz_matrix a, aa;

	bbox = fz_transformrect(matrix, bbox);
	w = (rect.x1 - rect.x0) / (bbox.x1 - bbox.x0);
	h = (rect.y1 - rect.y0) / (bbox.y1 - bbox.y0);
	x = rect.x0 - bbox.x0;
	y = rect.y0 - bbox.y0;
	a = fz_concat(fz_scale(w, h), fz_translate(x, y));
	aa = fz_concat(a, matrix);

	annot->ap->matrix = aa;
}

void
pdf_loadannots(pdf_annot **headp, pdf_xref *xref, fz_obj *annots)
{
	pdf_annot *head, *annot;
	fz_obj *obj, *ap, *as, *n, *rect;
	pdf_xobject *form;
	fz_error error;
	int i;

	head = nil;

	pdf_logpage("load appearance annotations {\n");

	for (i = 0; i < fz_arraylen(annots); i++)
	{
		obj = fz_arrayget(annots, i);

		rect = fz_dictgets(obj, "Rect");
		ap = fz_dictgets(obj, "AP");
		as = fz_dictgets(obj, "AS");
		if (fz_isdict(ap))
		{
			n = fz_dictgets(ap, "N"); /* normal state */
			if (fz_isindirect(n) && !pdf_isstream(xref, fz_tonum(n), fz_togen(n)))
				n = fz_dictget(n, as);

			if (fz_isindirect(n) && pdf_isstream(xref, fz_tonum(n), fz_togen(n)))
			{
				error = pdf_loadxobject(&form, xref, n);
				if (error)
				{
					fz_catch(error, "ignoring broken annotation");
					continue;
				}

				annot = fz_malloc(sizeof (pdf_annot));
				annot->obj = fz_keepobj(obj);
				annot->rect = pdf_torect(rect);
				annot->ap = form;
				annot->next = head;

				pdf_transformannot(annot);

				head = annot;
			}
		}
	}

	pdf_logpage("}\n");

	*headp = head;
}
