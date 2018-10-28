#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <string.h>

static pdf_obj *
resolve_dest_rec(fz_context *ctx, pdf_document *doc, pdf_obj *dest, int depth)
{
	if (depth > 10) /* Arbitrary to avoid infinite recursion */
		return NULL;

	if (pdf_is_name(ctx, dest) || pdf_is_string(ctx, dest))
	{
		dest = pdf_lookup_dest(ctx, doc, dest);
		dest = resolve_dest_rec(ctx, doc, dest, depth+1);
		return dest;
	}

	else if (pdf_is_array(ctx, dest))
	{
		return dest;
	}

	else if (pdf_is_dict(ctx, dest))
	{
		dest = pdf_dict_get(ctx, dest, PDF_NAME(D));
		return resolve_dest_rec(ctx, doc, dest, depth+1);
	}

	else if (pdf_is_indirect(ctx, dest))
		return dest;

	return NULL;
}

static pdf_obj *
resolve_dest(fz_context *ctx, pdf_document *doc, pdf_obj *dest)
{
	return resolve_dest_rec(ctx, doc, dest, 0);
}

char *
pdf_parse_link_dest(fz_context *ctx, pdf_document *doc, pdf_obj *dest)
{
	pdf_obj *obj;
	char buf[256];
	const char *ld;
	int page;
	int x, y;

	dest = resolve_dest(ctx, doc, dest);
	if (dest == NULL)
	{
		fz_warn(ctx, "undefined link destination");
		return NULL;
	}

	if (pdf_is_name(ctx, dest))
	{
		ld = pdf_to_name(ctx, dest);
		return fz_strdup(ctx, ld);
	}
	else if (pdf_is_string(ctx, dest))
	{
		ld = pdf_to_str_buf(ctx, dest);
		return fz_strdup(ctx, ld);
	}

	obj = pdf_array_get(ctx, dest, 0);
	if (pdf_is_int(ctx, obj))
		page = pdf_to_int(ctx, obj);
	else
	{
		fz_try(ctx)
			page = pdf_lookup_page_number(ctx, doc, obj);
		fz_catch(ctx)
			page = -1;
	}

	x = y = 0;
	obj = pdf_array_get(ctx, dest, 1);
	if (pdf_name_eq(ctx, obj, PDF_NAME(XYZ)))
	{
		x = pdf_array_get_int(ctx, dest, 2);
		y = pdf_array_get_int(ctx, dest, 3);
	}
	else if (pdf_name_eq(ctx, obj, PDF_NAME(FitR)))
	{
		x = pdf_array_get_int(ctx, dest, 2);
		y = pdf_array_get_int(ctx, dest, 5);
	}
	else if (pdf_name_eq(ctx, obj, PDF_NAME(FitH)) || pdf_name_eq(ctx, obj, PDF_NAME(FitBH)))
		y = pdf_array_get_int(ctx, dest, 2);
	else if (pdf_name_eq(ctx, obj, PDF_NAME(FitV)) || pdf_name_eq(ctx, obj, PDF_NAME(FitBV)))
		x = pdf_array_get_int(ctx, dest, 2);

	if (page >= 0)
	{
		if (x != 0 || y != 0)
			fz_snprintf(buf, sizeof buf, "#%d,%d,%d", page + 1, x, y);
		else
			fz_snprintf(buf, sizeof buf, "#%d", page + 1);
		return fz_strdup(ctx, buf);
	}

	return NULL;
}

char *
pdf_parse_file_spec(fz_context *ctx, pdf_document *doc, pdf_obj *file_spec, pdf_obj *dest)
{
	pdf_obj *filename=NULL;
	char *path = NULL;
	char *uri = NULL;
	char buf[256];
	size_t n;

	if (pdf_is_string(ctx, file_spec))
		filename = file_spec;

	if (pdf_is_dict(ctx, file_spec)) {
#ifdef _WIN32
		filename = pdf_dict_get(ctx, file_spec, PDF_NAME(DOS));
#else
		filename = pdf_dict_get(ctx, file_spec, PDF_NAME(Unix));
#endif
		if (!filename)
			filename = pdf_dict_geta(ctx, file_spec, PDF_NAME(UF), PDF_NAME(F));
	}

	if (!pdf_is_string(ctx, filename))
	{
		fz_warn(ctx, "cannot parse file specification");
		return NULL;
	}

	path = fz_strdup(ctx, pdf_to_text_string(ctx, filename));
#ifdef _WIN32
	if (!pdf_name_eq(ctx, pdf_dict_get(ctx, file_spec, PDF_NAME(FS)), PDF_NAME(URL)))
	{
		/* move the file name into the expected place and use the expected path separator */
		char *c;
		if (path[0] == '/' && (('A' <= path[1] && path[1] <= 'Z') || ('a' <= path[1] && path[1] <= 'z')) && path[2] == '/')
		{
			path[0] = path[1];
			path[1] = ':';
		}
		for (c = path; *c; c++)
		{
			if (*c == '/')
				*c = '\\';
		}
	}
#endif

	if (pdf_is_array(ctx, dest))
		fz_snprintf(buf, sizeof buf, "#page=%d", pdf_array_get_int(ctx, dest, 0) + 1);
	else if (pdf_is_name(ctx, dest))
		fz_snprintf(buf, sizeof buf, "#%s", pdf_to_name(ctx, dest));
	else if (pdf_is_string(ctx, dest))
		fz_snprintf(buf, sizeof buf, "#%s", pdf_to_str_buf(ctx, dest));
	else
		buf[0] = 0;

	n = 7 + strlen(path) + strlen(buf) + 1;
	uri = fz_malloc(ctx, n);
	fz_strlcpy(uri, "file://", n);
	fz_strlcat(uri, path, n);
	fz_strlcat(uri, buf, n);
	fz_free(ctx, path);
	return uri;
}

char *
pdf_parse_link_action(fz_context *ctx, pdf_document *doc, pdf_obj *action, int pagenum)
{
	pdf_obj *obj, *dest, *file_spec;

	if (!action)
		return NULL;

	obj = pdf_dict_get(ctx, action, PDF_NAME(S));
	if (pdf_name_eq(ctx, PDF_NAME(GoTo), obj))
	{
		dest = pdf_dict_get(ctx, action, PDF_NAME(D));
		return pdf_parse_link_dest(ctx, doc, dest);
	}
	else if (pdf_name_eq(ctx, PDF_NAME(URI), obj))
	{
		/* URI entries are ASCII strings */
		const char *uri = pdf_dict_get_string(ctx, action, PDF_NAME(URI), NULL);
		if (!fz_is_external_link(ctx, uri))
		{
			pdf_obj *uri_base_obj = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/URI/Base");
			const char *uri_base = uri_base_obj ? pdf_to_str_buf(ctx, uri_base_obj) : "file://";
			char *new_uri = fz_malloc(ctx, strlen(uri_base) + strlen(uri) + 1);
			strcpy(new_uri, uri_base);
			strcat(new_uri, uri);
			return new_uri;
		}
		return fz_strdup(ctx, uri);
	}
	else if (pdf_name_eq(ctx, PDF_NAME(Launch), obj))
	{
		file_spec = pdf_dict_get(ctx, action, PDF_NAME(F));
		return pdf_parse_file_spec(ctx, doc, file_spec, NULL);
	}
	else if (pdf_name_eq(ctx, PDF_NAME(GoToR), obj))
	{
		dest = pdf_dict_get(ctx, action, PDF_NAME(D));
		file_spec = pdf_dict_get(ctx, action, PDF_NAME(F));
		return pdf_parse_file_spec(ctx, doc, file_spec, dest);
	}
	else if (pdf_name_eq(ctx, PDF_NAME(Named), obj))
	{
		dest = pdf_dict_get(ctx, action, PDF_NAME(N));

		if (pdf_name_eq(ctx, PDF_NAME(FirstPage), dest))
			pagenum = 0;
		else if (pdf_name_eq(ctx, PDF_NAME(LastPage), dest))
			pagenum = pdf_count_pages(ctx, doc) - 1;
		else if (pdf_name_eq(ctx, PDF_NAME(PrevPage), dest) && pagenum >= 0)
		{
			if (pagenum > 0)
				pagenum--;
		}
		else if (pdf_name_eq(ctx, PDF_NAME(NextPage), dest) && pagenum >= 0)
		{
			if (pagenum < pdf_count_pages(ctx, doc) - 1)
				pagenum++;
		}
		else
			return NULL;

		return fz_asprintf(ctx, "#%d", pagenum + 1);
	}

	return NULL;
}

static fz_link *
pdf_load_link(fz_context *ctx, pdf_document *doc, pdf_obj *dict, int pagenum, fz_matrix page_ctm)
{
	pdf_obj *action;
	pdf_obj *obj;
	fz_rect bbox;
	char *uri;
	fz_link *link = NULL;

	obj = pdf_dict_get(ctx, dict, PDF_NAME(Subtype));
	if (!pdf_name_eq(ctx, obj, PDF_NAME(Link)))
		return NULL;

	obj = pdf_dict_get(ctx, dict, PDF_NAME(Rect));
	if (!obj)
		return NULL;

	bbox = pdf_to_rect(ctx, obj);
	bbox = fz_transform_rect(bbox, page_ctm);

	obj = pdf_dict_get(ctx, dict, PDF_NAME(Dest));
	if (obj)
		uri = pdf_parse_link_dest(ctx, doc, obj);
	else
	{
		action = pdf_dict_get(ctx, dict, PDF_NAME(A));
		/* fall back to additional action button's down/up action */
		if (!action)
			action = pdf_dict_geta(ctx, pdf_dict_get(ctx, dict, PDF_NAME(AA)), PDF_NAME(U), PDF_NAME(D));
		uri = pdf_parse_link_action(ctx, doc, action, pagenum);
	}

	if (!uri)
		return NULL;

	fz_try(ctx)
		link = fz_new_link(ctx, bbox, doc, uri);
	fz_always(ctx)
		fz_free(ctx, uri);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return link;
}

fz_link *
pdf_load_link_annots(fz_context *ctx, pdf_document *doc, pdf_obj *annots, int pagenum, fz_matrix page_ctm)
{
	fz_link *link, *head, *tail;
	pdf_obj *obj;
	int i, n;

	head = tail = NULL;
	link = NULL;

	n = pdf_array_len(ctx, annots);
	for (i = 0; i < n; i++)
	{
		/* FIXME: Move the try/catch out of the loop for performance? */
		fz_try(ctx)
		{
			obj = pdf_array_get(ctx, annots, i);
			link = pdf_load_link(ctx, doc, obj, pagenum, page_ctm);
		}
		fz_catch(ctx)
		{
			fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
			link = NULL;
		}

		if (link)
		{
			if (!head)
				head = tail = link;
			else
			{
				tail->next = link;
				tail = link;
			}
		}
	}

	return head;
}

int
pdf_resolve_link(fz_context *ctx, pdf_document *doc, const char *uri, float *xp, float *yp)
{
	if (uri && uri[0] == '#')
	{
		int page = fz_atoi(uri + 1) - 1;
		if (xp || yp)
		{
			const char *x = strchr(uri, ',');
			const char *y = strrchr(uri, ',');
			if (x && y)
			{
				pdf_obj *obj;
				fz_matrix ctm;
				fz_point p;

				p.x = x ? fz_atoi(x + 1) : 0;
				p.y = y ? fz_atoi(y + 1) : 0;
				obj = pdf_lookup_page_obj(ctx, doc, page);
				pdf_page_obj_transform(ctx, obj, NULL, &ctm);
				p = fz_transform_point(p, ctm);

				if (xp) *xp = p.x;
				if (yp) *yp = p.y;
			}
		}
		return page;
	}
	fz_warn(ctx, "unknown link uri '%s'", uri);
	return -1;
}
