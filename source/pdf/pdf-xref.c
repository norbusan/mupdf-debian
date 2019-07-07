#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

#undef DEBUG_PROGESSIVE_ADVANCE

#ifdef DEBUG_PROGESSIVE_ADVANCE
#define DEBUGMESS(A) do { fz_warn A; } while (0)
#else
#define DEBUGMESS(A) do { } while (0)
#endif

#define isdigit(c) (c >= '0' && c <= '9')

static inline int iswhite(int ch)
{
	return
		ch == '\000' || ch == '\011' || ch == '\012' ||
		ch == '\014' || ch == '\015' || ch == '\040';
}

/*
 * xref tables
 */

static void pdf_drop_xref_sections_imp(fz_context *ctx, pdf_document *doc, pdf_xref *xref_sections, int num_xref_sections)
{
	pdf_unsaved_sig *usig;
	int x, e;

	for (x = 0; x < num_xref_sections; x++)
	{
		pdf_xref *xref = &xref_sections[x];
		pdf_xref_subsec *sub = xref->subsec;

		while (sub != NULL)
		{
			pdf_xref_subsec *next_sub = sub->next;
			for (e = 0; e < sub->len; e++)
			{
				pdf_xref_entry *entry = &sub->table[e];
				if (entry->obj)
				{
					pdf_drop_obj(ctx, entry->obj);
					fz_drop_buffer(ctx, entry->stm_buf);
				}
			}
			fz_free(ctx, sub->table);
			fz_free(ctx, sub);
			sub = next_sub;
		}

		pdf_drop_obj(ctx, xref->pre_repair_trailer);
		pdf_drop_obj(ctx, xref->trailer);

		while ((usig = xref->unsaved_sigs) != NULL)
		{
			xref->unsaved_sigs = usig->next;
			pdf_drop_obj(ctx, usig->field);
			usig->signer->drop(usig->signer);
			fz_free(ctx, usig);
		}
	}

	fz_free(ctx, xref_sections);
}

static void pdf_drop_xref_sections(fz_context *ctx, pdf_document *doc)
{
	pdf_drop_xref_sections_imp(ctx, doc, doc->saved_xref_sections, doc->saved_num_xref_sections);
	pdf_drop_xref_sections_imp(ctx, doc, doc->xref_sections, doc->num_xref_sections);

	doc->saved_xref_sections = NULL;
	doc->saved_num_xref_sections = 0;
	doc->xref_sections = NULL;
	doc->num_xref_sections = 0;
	doc->num_incremental_sections = 0;
}

static void
extend_xref_index(fz_context *ctx, pdf_document *doc, int newlen)
{
	int i;

	doc->xref_index = fz_resize_array(ctx, doc->xref_index, newlen, sizeof(int));
	for (i = doc->max_xref_len; i < newlen; i++)
	{
		doc->xref_index[i] = 0;
	}
	doc->max_xref_len = newlen;
}

/* This is only ever called when we already have an incremental
 * xref. This means there will only be 1 subsec, and it will be
 * a complete subsec. */
static void pdf_resize_xref(fz_context *ctx, pdf_document *doc, int newlen)
{
	int i;
	pdf_xref *xref = &doc->xref_sections[doc->xref_base];
	pdf_xref_subsec *sub;

	assert(xref != NULL);
	sub = xref->subsec;
	assert(sub->next == NULL && sub->start == 0 && sub->len == xref->num_objects);
	assert(newlen > xref->num_objects);

	sub->table = fz_resize_array(ctx, sub->table, newlen, sizeof(pdf_xref_entry));
	for (i = xref->num_objects; i < newlen; i++)
	{
		sub->table[i].type = 0;
		sub->table[i].ofs = 0;
		sub->table[i].gen = 0;
		sub->table[i].num = 0;
		sub->table[i].stm_ofs = 0;
		sub->table[i].stm_buf = NULL;
		sub->table[i].obj = NULL;
	}
	xref->num_objects = newlen;
	sub->len = newlen;
	if (doc->max_xref_len < newlen)
		extend_xref_index(ctx, doc, newlen);
}

static void pdf_populate_next_xref_level(fz_context *ctx, pdf_document *doc)
{
	pdf_xref *xref;
	doc->xref_sections = fz_resize_array(ctx, doc->xref_sections, doc->num_xref_sections + 1, sizeof(pdf_xref));
	doc->num_xref_sections++;

	xref = &doc->xref_sections[doc->num_xref_sections - 1];
	xref->subsec = NULL;
	xref->num_objects = 0;
	xref->trailer = NULL;
	xref->pre_repair_trailer = NULL;
	xref->unsaved_sigs = NULL;
	xref->unsaved_sigs_end = NULL;
}

pdf_obj *pdf_trailer(fz_context *ctx, pdf_document *doc)
{
	/* Return the document's final trailer */
	pdf_xref *xref = &doc->xref_sections[0];

	return xref ? xref->trailer : NULL;
}

void pdf_set_populating_xref_trailer(fz_context *ctx, pdf_document *doc, pdf_obj *trailer)
{
	/* Update the trailer of the xref section being populated */
	pdf_xref *xref = &doc->xref_sections[doc->num_xref_sections - 1];
	if (xref->trailer)
	{
		pdf_drop_obj(ctx, xref->pre_repair_trailer);
		xref->pre_repair_trailer = xref->trailer;
	}
	xref->trailer = pdf_keep_obj(ctx, trailer);
}

int pdf_xref_len(fz_context *ctx, pdf_document *doc)
{
	return doc->max_xref_len;
}

/* Ensure that the given xref has a single subsection
 * that covers the entire range. */
static void
ensure_solid_xref(fz_context *ctx, pdf_document *doc, int num, int which)
{
	pdf_xref *xref = &doc->xref_sections[which];
	pdf_xref_subsec *sub = xref->subsec;
	pdf_xref_subsec *new_sub;

	if (num < xref->num_objects)
		num = xref->num_objects;

	if (sub != NULL && sub->next == NULL && sub->start == 0 && sub->len >= num)
		return;

	new_sub = fz_malloc_struct(ctx, pdf_xref_subsec);
	fz_try(ctx)
	{
		new_sub->table = fz_calloc(ctx, num, sizeof(pdf_xref_entry));
		new_sub->start = 0;
		new_sub->len = num;
		new_sub->next = NULL;
	}
	fz_catch(ctx)
	{
		fz_free(ctx, new_sub);
		fz_rethrow(ctx);
	}

	/* Move objects over to the new subsection and destroy the old
	 * ones */
	sub = xref->subsec;
	while (sub != NULL)
	{
		pdf_xref_subsec *next = sub->next;
		int i;

		for (i = 0; i < sub->len; i++)
		{
			new_sub->table[i+sub->start] = sub->table[i];
		}
		fz_free(ctx, sub->table);
		fz_free(ctx, sub);
		sub = next;
	}
	xref->num_objects = num;
	xref->subsec = new_sub;
	if (doc->max_xref_len < num)
		extend_xref_index(ctx, doc, num);
}

/* Used while reading the individual xref sections from a file */
pdf_xref_entry *pdf_get_populating_xref_entry(fz_context *ctx, pdf_document *doc, int num)
{
	/* Return an entry within the xref currently being populated */
	pdf_xref *xref;
	pdf_xref_subsec *sub;

	if (doc->num_xref_sections == 0)
	{
		doc->xref_sections = fz_malloc_struct(ctx, pdf_xref);
		doc->num_xref_sections = 1;
	}

	/* Prevent accidental heap underflow */
	if (num < 0 || num > PDF_MAX_OBJECT_NUMBER)
		fz_throw(ctx, FZ_ERROR_GENERIC, "object number out of range (%d)", num);

	/* Return the pointer to the entry in the last section. */
	xref = &doc->xref_sections[doc->num_xref_sections-1];

	for (sub = xref->subsec; sub != NULL; sub = sub->next)
	{
		if (num >= sub->start && num < sub->start + sub->len)
			return &sub->table[num-sub->start];
	}

	/* We've been asked for an object that's not in a subsec. */
	ensure_solid_xref(ctx, doc, num+1, doc->num_xref_sections-1);
	xref = &doc->xref_sections[doc->num_xref_sections-1];
	sub = xref->subsec;

	return &sub->table[num-sub->start];
}

/* Used after loading a document to access entries */
/* This will never throw anything, or return NULL if it is
 * only asked to return objects in range within a 'solid'
 * xref. */
pdf_xref_entry *pdf_get_xref_entry(fz_context *ctx, pdf_document *doc, int i)
{
	pdf_xref *xref = NULL;
	pdf_xref_subsec *sub;
	int j;

	if (i < 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "Negative object number requested");

	if (i <= doc->max_xref_len)
		j = doc->xref_index[i];
	else
		j = 0;

	/* We may be accessing an earlier version of the document using xref_base
	 * and j may be an index into a later xref section */
	if (doc->xref_base > j)
		j = doc->xref_base;

	/* Find the first xref section where the entry is defined. */
	for (; j < doc->num_xref_sections; j++)
	{
		xref = &doc->xref_sections[j];

		if (i < xref->num_objects)
		{
			for (sub = xref->subsec; sub != NULL; sub = sub->next)
			{
				pdf_xref_entry *entry;

				if (i < sub->start || i >= sub->start + sub->len)
					continue;

				entry = &sub->table[i - sub->start];
				if (entry->type)
				{
					/* Don't update xref_index if xref_base may have
					 * influenced the value of j */
					if (doc->xref_base == 0)
						doc->xref_index[i] = j;
					return entry;
				}
			}
		}
	}

	/* Didn't find the entry in any section. Return the entry from
	 * the final section. */
	doc->xref_index[i] = 0;
	if (xref == NULL || i < xref->num_objects)
	{
		xref = &doc->xref_sections[doc->xref_base];
		for (sub = xref->subsec; sub != NULL; sub = sub->next)
		{
			if (i >= sub->start && i < sub->start + sub->len)
				return &sub->table[i - sub->start];
		}
	}

	/* At this point, we solidify the xref. This ensures that we
	 * can return a pointer. This is the only case where this function
	 * might throw an exception, and it will never happen when we are
	 * working within a 'solid' xref. */
	ensure_solid_xref(ctx, doc, i+1, 0);
	xref = &doc->xref_sections[0];
	sub = xref->subsec;
	return &sub->table[i - sub->start];
}

/*
	Ensure we have an incremental xref section where we can store
	updated versions of indirect objects. This is a new xref section
	consisting of a single xref subsection.
*/
static void ensure_incremental_xref(fz_context *ctx, pdf_document *doc)
{
	/* If there are as yet no incremental sections, or if the most recent
	 * one has been used to sign a signature field, then we need a new one.
	 * After a signing, any further document changes require a new increment */
	if ((doc->num_incremental_sections == 0 || doc->xref_sections[0].unsaved_sigs != NULL)
		&& !doc->disallow_new_increments)
	{
		pdf_xref *xref = &doc->xref_sections[0];
		pdf_xref *pxref;
		pdf_xref_entry *new_table = fz_calloc(ctx, xref->num_objects, sizeof(pdf_xref_entry));
		pdf_xref_subsec *sub = NULL;
		pdf_obj *trailer = NULL;
		int i;

		fz_var(trailer);
		fz_var(sub);
		fz_try(ctx)
		{
			sub = fz_malloc_struct(ctx, pdf_xref_subsec);
			trailer = xref->trailer ? pdf_copy_dict(ctx, xref->trailer) : NULL;
			doc->xref_sections = fz_resize_array(ctx, doc->xref_sections, doc->num_xref_sections + 1, sizeof(pdf_xref));
			xref = &doc->xref_sections[0];
			pxref = &doc->xref_sections[1];
			memmove(pxref, xref, doc->num_xref_sections * sizeof(pdf_xref));
			/* xref->num_objects is already correct */
			xref->subsec = sub;
			sub = NULL;
			xref->trailer = trailer;
			xref->pre_repair_trailer = NULL;
			xref->unsaved_sigs = NULL;
			xref->unsaved_sigs_end = NULL;
			xref->subsec->next = NULL;
			xref->subsec->len = xref->num_objects;
			xref->subsec->start = 0;
			xref->subsec->table = new_table;
			doc->num_xref_sections++;
			doc->num_incremental_sections++;
		}
		fz_catch(ctx)
		{
			fz_free(ctx, sub);
			fz_free(ctx, new_table);
			pdf_drop_obj(ctx, trailer);
			fz_rethrow(ctx);
		}

		/* Update the xref_index */
		for (i = 0; i < doc->max_xref_len; i++)
		{
			doc->xref_index[i]++;
		}
	}
}

/* Used when altering a document */
static pdf_xref_entry *pdf_get_incremental_xref_entry(fz_context *ctx, pdf_document *doc, int i)
{
	pdf_xref *xref;
	pdf_xref_subsec *sub;

	/* Make a new final xref section if we haven't already */
	ensure_incremental_xref(ctx, doc);

	xref = &doc->xref_sections[doc->xref_base];
	if (i >= xref->num_objects)
		pdf_resize_xref(ctx, doc, i + 1);

	sub = xref->subsec;
	assert(sub != NULL && sub->next == NULL);
	assert(i >= sub->start && i < sub->start + sub->len);
	doc->xref_index[i] = 0;
	return &sub->table[i - sub->start];
}

int pdf_xref_is_incremental(fz_context *ctx, pdf_document *doc, int num)
{
	pdf_xref *xref = &doc->xref_sections[doc->xref_base];
	pdf_xref_subsec *sub = xref->subsec;

	assert(sub != NULL && sub->next == NULL && sub->len == xref->num_objects && sub->start == 0);

	return num < xref->num_objects && sub->table[num].type;
}

void pdf_xref_store_unsaved_signature(fz_context *ctx, pdf_document *doc, pdf_obj *field, pdf_pkcs7_signer *signer)
{
	pdf_xref *xref = &doc->xref_sections[0];
	pdf_unsaved_sig *unsaved_sig;

	/* Record details within the document structure so that contents
	 * and byte_range can be updated with their correct values at
	 * saving time */
	unsaved_sig = fz_malloc_struct(ctx, pdf_unsaved_sig);
	unsaved_sig->field = pdf_keep_obj(ctx, field);
	unsaved_sig->signer = signer->keep(signer);
	unsaved_sig->next = NULL;
	if (xref->unsaved_sigs_end == NULL)
		xref->unsaved_sigs_end = &xref->unsaved_sigs;

	*xref->unsaved_sigs_end = unsaved_sig;
	xref->unsaved_sigs_end = &unsaved_sig->next;
}

int pdf_xref_obj_is_unsaved_signature(pdf_document *doc, pdf_obj *obj)
{
	int i;
	for (i = 0; i < doc->num_incremental_sections; i++)
	{
		pdf_xref *xref = &doc->xref_sections[i];
		pdf_unsaved_sig *usig;

		for (usig = xref->unsaved_sigs; usig; usig = usig->next)
		{
			if (usig->field == obj)
				return 1;
		}
	}

	return 0;
}

/* Ensure that the current populating xref has a single subsection
 * that covers the entire range. */
void pdf_ensure_solid_xref(fz_context *ctx, pdf_document *doc, int num)
{
	if (doc->num_xref_sections == 0)
		pdf_populate_next_xref_level(ctx, doc);

	ensure_solid_xref(ctx, doc, num, doc->num_xref_sections-1);
}

/* Ensure that an object has been cloned into the incremental xref section */
void pdf_xref_ensure_incremental_object(fz_context *ctx, pdf_document *doc, int num)
{
	pdf_xref_entry *new_entry, *old_entry;
	pdf_xref_subsec *sub = NULL;
	int i;

	/* Make sure we have created an xref section for incremental updates */
	ensure_incremental_xref(ctx, doc);

	/* Search for the section that contains this object */
	for (i = doc->xref_index[num]; i < doc->num_xref_sections; i++)
	{
		pdf_xref *xref = &doc->xref_sections[i];

		if (num < 0 && num >= xref->num_objects)
			break;
		for (sub = xref->subsec; sub != NULL; sub = sub->next)
		{
			if (sub->start <= num && num < sub->start + sub->len && sub->table[num - sub->start].type)
				break;
		}
		if (sub != NULL)
			break;
	}
	/* sub == NULL implies we did not find it */

	/* If we don't find it, or it's already in the incremental section, return */
	if (i == 0 || sub == NULL)
		return;

	/* Move the object to the incremental section */
	doc->xref_index[num] = 0;
	old_entry = &sub->table[num - sub->start];
	new_entry = pdf_get_incremental_xref_entry(ctx, doc, num);
	*new_entry = *old_entry;
	if (i < doc->num_incremental_sections)
	{
		/* old entry is incremental and may have changes.
		 * Better keep a copy. We must override the old entry with
		 * the copy because the caller may be holding a reference to
		 * the original and expect it to end up in the new entry */
		old_entry->obj = pdf_deep_copy_obj(ctx, old_entry->obj);
	}
	else
	{
		old_entry->obj = NULL;
	}
	old_entry->stm_buf = NULL;
}

void pdf_replace_xref(fz_context *ctx, pdf_document *doc, pdf_xref_entry *entries, int n)
{
	int *xref_index = NULL;
	pdf_xref *xref = NULL;
	pdf_xref_subsec *sub;

	fz_var(xref_index);
	fz_var(xref);

	fz_try(ctx)
	{
		xref_index = fz_calloc(ctx, n, sizeof(int));
		xref = fz_malloc_struct(ctx, pdf_xref);
		sub = fz_malloc_struct(ctx, pdf_xref_subsec);
	}
	fz_catch(ctx)
	{
		fz_free(ctx, xref);
		fz_free(ctx, xref_index);
		fz_rethrow(ctx);
	}

	sub->table = entries;
	sub->start = 0;
	sub->len = n;

	xref->subsec = sub;
	xref->num_objects = n;
	xref->trailer = pdf_keep_obj(ctx, pdf_trailer(ctx, doc));

	/* The new table completely replaces the previous separate sections */
	pdf_drop_xref_sections(ctx, doc);

	doc->xref_sections = xref;
	doc->num_xref_sections = 1;
	doc->num_incremental_sections = 0;
	doc->xref_base = 0;
	doc->disallow_new_increments = 0;
	doc->max_xref_len = n;

	fz_free(ctx, doc->xref_index);
	doc->xref_index = xref_index;
}

void pdf_forget_xref(fz_context *ctx, pdf_document *doc)
{
	pdf_obj *trailer = pdf_keep_obj(ctx, pdf_trailer(ctx, doc));

	if (doc->saved_xref_sections)
		pdf_drop_xref_sections_imp(ctx, doc, doc->saved_xref_sections, doc->saved_num_xref_sections);

	doc->saved_xref_sections = doc->xref_sections;
	doc->saved_num_xref_sections = doc->num_xref_sections;

	doc->startxref = 0;
	doc->num_xref_sections = 0;
	doc->num_incremental_sections = 0;
	doc->xref_base = 0;
	doc->disallow_new_increments = 0;

	fz_try(ctx)
	{
		pdf_get_populating_xref_entry(ctx, doc, 0);
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, trailer);
		fz_rethrow(ctx);
	}

	/* Set the trailer of the final xref section. */
	doc->xref_sections[0].trailer = trailer;
}

/*
 * magic version tag and startxref
 */

static void
pdf_load_version(fz_context *ctx, pdf_document *doc)
{
	char buf[20];

	fz_seek(ctx, doc->file, 0, SEEK_SET);
	fz_read_line(ctx, doc->file, buf, sizeof buf);
	if (strlen(buf) < 5 || memcmp(buf, "%PDF-", 5) != 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot recognize version marker");

	doc->version = 10 * (fz_atof(buf+5) + 0.05f);
	if (doc->version < 10 || doc->version > 17)
		if (doc->version != 20)
			fz_warn(ctx, "unknown PDF version: %d.%d", doc->version / 10, doc->version % 10);
}

static void
pdf_read_start_xref(fz_context *ctx, pdf_document *doc)
{
	unsigned char buf[1024];
	size_t i, n;
	int64_t t;

	fz_seek(ctx, doc->file, 0, SEEK_END);

	doc->file_size = fz_tell(ctx, doc->file);

	t = fz_maxi64(0, doc->file_size - (int64_t)sizeof buf);
	fz_seek(ctx, doc->file, t, SEEK_SET);

	n = fz_read(ctx, doc->file, buf, sizeof buf);
	if (n < 9)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find startxref");

	i = n - 9;
	do
	{
		if (memcmp(buf + i, "startxref", 9) == 0)
		{
			i += 9;
			while (i < n && iswhite(buf[i]))
				i ++;
			doc->startxref = 0;
			while (i < n && isdigit(buf[i]))
			{
				if (doc->startxref >= INT64_MAX/10)
					fz_throw(ctx, FZ_ERROR_GENERIC, "startxref too large");
				doc->startxref = doc->startxref * 10 + (buf[i++] - '0');
			}
			if (doc->startxref != 0)
				return;
			break;
		}
	} while (i-- > 0);

	fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find startxref");
}

static void
fz_skip_space(fz_context *ctx, fz_stream *stm)
{
	do
	{
		int c = fz_peek_byte(ctx, stm);
		if (c == EOF || c > 32)
			return;
		(void)fz_read_byte(ctx, stm);
	}
	while (1);
}

static int fz_skip_string(fz_context *ctx, fz_stream *stm, const char *str)
{
	while (*str)
	{
		int c = fz_peek_byte(ctx, stm);
		if (c == EOF || c != *str++)
			return 1;
		(void)fz_read_byte(ctx, stm);
	}
	return 0;
}

/*
 * trailer dictionary
 */

static int
pdf_xref_size_from_old_trailer(fz_context *ctx, pdf_document *doc, pdf_lexbuf *buf)
{
	int len;
	char *s;
	int64_t t;
	pdf_token tok;
	int c;
	int size = 0;
	int64_t ofs;
	pdf_obj *trailer = NULL;
	size_t n;

	fz_var(trailer);

	/* Record the current file read offset so that we can reinstate it */
	ofs = fz_tell(ctx, doc->file);

	fz_skip_space(ctx, doc->file);
	if (fz_skip_string(ctx, doc->file, "xref"))
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find xref marker");
	fz_skip_space(ctx, doc->file);

	while (1)
	{
		c = fz_peek_byte(ctx, doc->file);
		if (!isdigit(c))
			break;

		fz_read_line(ctx, doc->file, buf->scratch, buf->size);
		s = buf->scratch;
		fz_strsep(&s, " "); /* ignore start */
		if (!s)
			fz_throw(ctx, FZ_ERROR_GENERIC, "xref subsection length missing");
		len = fz_atoi(fz_strsep(&s, " "));
		if (len < 0)
			fz_throw(ctx, FZ_ERROR_GENERIC, "xref subsection length must be positive");

		/* broken pdfs where the section is not on a separate line */
		if (s && *s != '\0')
			fz_seek(ctx, doc->file, -(2 + (int)strlen(s)), SEEK_CUR);

		t = fz_tell(ctx, doc->file);
		if (t < 0)
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot tell in file");

		/* Spec says xref entries should be 20 bytes, but it's not infrequent
		 * to see 19, in particular for some PCLm drivers. Cope. */
		if (len > 0)
		{
			n = fz_read(ctx, doc->file, (unsigned char *)buf->scratch, 20);
			if (n < 19)
				fz_throw(ctx, FZ_ERROR_GENERIC, "malformed xref table");
			if (n == 20 && buf->scratch[19] > 32)
				n = 19;
		}
		else
			n = 20;

		if (len > (int64_t)((INT64_MAX - t) / n))
			fz_throw(ctx, FZ_ERROR_GENERIC, "xref has too many entries");

		fz_seek(ctx, doc->file, t + n * len, SEEK_SET);
	}

	fz_try(ctx)
	{
		tok = pdf_lex(ctx, doc->file, buf);
		if (tok != PDF_TOK_TRAILER)
			fz_throw(ctx, FZ_ERROR_GENERIC, "expected trailer marker");

		tok = pdf_lex(ctx, doc->file, buf);
		if (tok != PDF_TOK_OPEN_DICT)
			fz_throw(ctx, FZ_ERROR_GENERIC, "expected trailer dictionary");

		trailer = pdf_parse_dict(ctx, doc, doc->file, buf);

		size = pdf_dict_get_int(ctx, trailer, PDF_NAME(Size));
		if (size < 0 || size > PDF_MAX_OBJECT_NUMBER + 1)
			fz_throw(ctx, FZ_ERROR_GENERIC, "trailer Size entry out of range");
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, trailer);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	fz_seek(ctx, doc->file, ofs, SEEK_SET);

	return size;
}

static pdf_xref_entry *
pdf_xref_find_subsection(fz_context *ctx, pdf_document *doc, int start, int len)
{
	pdf_xref *xref = &doc->xref_sections[doc->num_xref_sections-1];
	pdf_xref_subsec *sub;
	int num_objects;

	/* Different cases here. Case 1) We might be asking for a
	 * subsection (or a subset of a subsection) that we already
	 * have - Just return it. Case 2) We might be asking for a
	 * completely new subsection - Create it and return it.
	 * Case 3) We might have an overlapping one - Create a 'solid'
	 * subsection and return that. */

	/* Sanity check */
	for (sub = xref->subsec; sub != NULL; sub = sub->next)
	{
		if (start >= sub->start && start + len <= sub->start + sub->len)
			return &sub->table[start-sub->start]; /* Case 1 */
		if (start + len > sub->start && start <= sub->start + sub->len)
			break; /* Case 3 */
	}

	num_objects = xref->num_objects;
	if (num_objects < start + len)
		num_objects = start + len;

	if (sub == NULL)
	{
		/* Case 2 */
		sub = fz_malloc_struct(ctx, pdf_xref_subsec);
		fz_try(ctx)
		{
			sub->table = fz_calloc(ctx, len, sizeof(pdf_xref_entry));
			sub->start = start;
			sub->len = len;
			sub->next = xref->subsec;
			xref->subsec = sub;
		}
		fz_catch(ctx)
		{
			fz_free(ctx, sub);
			fz_rethrow(ctx);
		}
		xref->num_objects = num_objects;
		if (doc->max_xref_len < num_objects)
			extend_xref_index(ctx, doc, num_objects);
	}
	else
	{
		/* Case 3 */
		ensure_solid_xref(ctx, doc, num_objects, doc->num_xref_sections-1);
		xref = &doc->xref_sections[doc->num_xref_sections-1];
		sub = xref->subsec;
	}
	return &sub->table[start-sub->start];
}

static pdf_obj *
pdf_read_old_xref(fz_context *ctx, pdf_document *doc, pdf_lexbuf *buf)
{
	int start, len, c, i, xref_len, carried;
	fz_stream *file = doc->file;
	pdf_xref_entry *table;
	pdf_token tok;
	size_t n;
	char *s, *e;

	xref_len = pdf_xref_size_from_old_trailer(ctx, doc, buf);

	fz_skip_space(ctx, doc->file);
	if (fz_skip_string(ctx, doc->file, "xref"))
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find xref marker");
	fz_skip_space(ctx, doc->file);

	while (1)
	{
		c = fz_peek_byte(ctx, file);
		if (!isdigit(c))
			break;

		fz_read_line(ctx, file, buf->scratch, buf->size);
		s = buf->scratch;
		start = fz_atoi(fz_strsep(&s, " "));
		len = fz_atoi(fz_strsep(&s, " "));

		/* broken pdfs where the section is not on a separate line */
		if (s && *s != '\0')
		{
			fz_warn(ctx, "broken xref subsection. proceeding anyway.");
			fz_seek(ctx, file, -(2 + (int)strlen(s)), SEEK_CUR);
		}

		if (start < 0 || start > PDF_MAX_OBJECT_NUMBER
				|| len < 0 || len > PDF_MAX_OBJECT_NUMBER
				|| start + len - 1 > PDF_MAX_OBJECT_NUMBER)
		{
			fz_throw(ctx, FZ_ERROR_GENERIC, "xref subsection object numbers are out of range");
		}
		/* broken pdfs where size in trailer undershoots entries in xref sections */
		if (start + len > xref_len)
		{
			fz_warn(ctx, "broken xref subsection, proceeding anyway.");
		}

		table = pdf_xref_find_subsection(ctx, doc, start, len);

		/* Xref entries SHOULD be 20 bytes long, but we see 19 byte
		 * ones more frequently than we'd like (e.g. PCLm drivers).
		 * Cope with this by 'carrying' data forward. */
		carried = 0;
		for (i = 0; i < len; i++)
		{
			pdf_xref_entry *entry = &table[i];
			n = fz_read(ctx, file, (unsigned char *) buf->scratch + carried, 20-carried);
			if (n != 20-carried)
				fz_throw(ctx, FZ_ERROR_GENERIC, "unexpected EOF in xref table");
			n += carried;
			buf->scratch[n] = '\0';
			if (!entry->type)
			{
				s = buf->scratch;
				e = s + n;

				entry->num = start + i;

				/* broken pdfs where line start with white space */
				while (s < e && iswhite(*s))
					s++;

				if (s == e || !isdigit(*s))
					fz_throw(ctx, FZ_ERROR_GENERIC, "xref offset missing");
				while (s < e && isdigit(*s))
					entry->ofs = entry->ofs * 10 + *s++ - '0';

				while (s < e && iswhite(*s))
					s++;
				if (s == e || !isdigit(*s))
					fz_throw(ctx, FZ_ERROR_GENERIC, "xref generation number missing");
				while (s < e && isdigit(*s))
					entry->gen = entry->gen * 10 + *s++ - '0';

				while (s < e && iswhite(*s))
					s++;
				if (s == e || (*s != 'f' && *s != 'n' && *s != 'o'))
					fz_throw(ctx, FZ_ERROR_GENERIC, "unexpected xref type: 0x%x (%d %d R)", s == e ? 0 : *s, entry->num, entry->gen);
				entry->type = *s++;

				/* If the last byte of our buffer isn't an EOL (or space), carry one byte forward */
				carried = buf->scratch[19] > 32;
				if (carried)
					buf->scratch[0] = buf->scratch[19];
			}
		}
		if (carried)
			fz_unread_byte(ctx, file);
	}

	tok = pdf_lex(ctx, file, buf);
	if (tok != PDF_TOK_TRAILER)
		fz_throw(ctx, FZ_ERROR_GENERIC, "expected trailer marker");

	tok = pdf_lex(ctx, file, buf);
	if (tok != PDF_TOK_OPEN_DICT)
		fz_throw(ctx, FZ_ERROR_GENERIC, "expected trailer dictionary");

	doc->has_old_style_xrefs = 1;

	return pdf_parse_dict(ctx, doc, file, buf);
}

static void
pdf_read_new_xref_section(fz_context *ctx, pdf_document *doc, fz_stream *stm, int i0, int i1, int w0, int w1, int w2)
{
	pdf_xref_entry *table;
	int i, n;

	if (i0 < 0 || i0 > PDF_MAX_OBJECT_NUMBER || i1 < 0 || i1 > PDF_MAX_OBJECT_NUMBER || i0 + i1 - 1 > PDF_MAX_OBJECT_NUMBER)
		fz_throw(ctx, FZ_ERROR_GENERIC, "xref subsection object numbers are out of range");

	table = pdf_xref_find_subsection(ctx, doc, i0, i1);
	for (i = i0; i < i0 + i1; i++)
	{
		pdf_xref_entry *entry = &table[i-i0];
		int a = 0;
		int64_t b = 0;
		int c = 0;

		if (fz_is_eof(ctx, stm))
			fz_throw(ctx, FZ_ERROR_GENERIC, "truncated xref stream");

		for (n = 0; n < w0; n++)
			a = (a << 8) + fz_read_byte(ctx, stm);
		for (n = 0; n < w1; n++)
			b = (b << 8) + fz_read_byte(ctx, stm);
		for (n = 0; n < w2; n++)
			c = (c << 8) + fz_read_byte(ctx, stm);

		if (!entry->type)
		{
			int t = w0 ? a : 1;
			entry->type = t == 0 ? 'f' : t == 1 ? 'n' : t == 2 ? 'o' : 0;
			entry->ofs = w1 ? b : 0;
			entry->gen = w2 ? c : 0;
			entry->num = i;
		}
	}

	doc->has_xref_streams = 1;
}

/* Entered with file locked, remains locked throughout. */
static pdf_obj *
pdf_read_new_xref(fz_context *ctx, pdf_document *doc, pdf_lexbuf *buf)
{
	fz_stream *stm = NULL;
	pdf_obj *trailer = NULL;
	pdf_obj *index = NULL;
	pdf_obj *obj = NULL;
	int gen, num = 0;
	int64_t ofs, stm_ofs;
	int size, w0, w1, w2;
	int t;

	fz_var(trailer);
	fz_var(stm);

	fz_try(ctx)
	{
		ofs = fz_tell(ctx, doc->file);
		trailer = pdf_parse_ind_obj(ctx, doc, doc->file, buf, &num, &gen, &stm_ofs, NULL);
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, trailer);
		fz_rethrow(ctx);
	}

	fz_try(ctx)
	{
		pdf_xref_entry *entry;

		obj = pdf_dict_get(ctx, trailer, PDF_NAME(Size));
		if (!obj)
			fz_throw(ctx, FZ_ERROR_GENERIC, "xref stream missing Size entry (%d 0 R)", num);

		size = pdf_to_int(ctx, obj);

		obj = pdf_dict_get(ctx, trailer, PDF_NAME(W));
		if (!obj)
			fz_throw(ctx, FZ_ERROR_GENERIC, "xref stream missing W entry (%d  R)", num);
		w0 = pdf_array_get_int(ctx, obj, 0);
		w1 = pdf_array_get_int(ctx, obj, 1);
		w2 = pdf_array_get_int(ctx, obj, 2);

		if (w0 < 0)
			fz_warn(ctx, "xref stream objects have corrupt type");
		if (w1 < 0)
			fz_warn(ctx, "xref stream objects have corrupt offset");
		if (w2 < 0)
			fz_warn(ctx, "xref stream objects have corrupt generation");

		w0 = w0 < 0 ? 0 : w0;
		w1 = w1 < 0 ? 0 : w1;
		w2 = w2 < 0 ? 0 : w2;

		index = pdf_dict_get(ctx, trailer, PDF_NAME(Index));

		stm = pdf_open_stream_with_offset(ctx, doc, num, trailer, stm_ofs);

		if (!index)
		{
			pdf_read_new_xref_section(ctx, doc, stm, 0, size, w0, w1, w2);
		}
		else
		{
			int n = pdf_array_len(ctx, index);
			for (t = 0; t < n; t += 2)
			{
				int i0 = pdf_array_get_int(ctx, index, t + 0);
				int i1 = pdf_array_get_int(ctx, index, t + 1);
				pdf_read_new_xref_section(ctx, doc, stm, i0, i1, w0, w1, w2);
			}
		}
		entry = pdf_get_populating_xref_entry(ctx, doc, num);
		entry->ofs = ofs;
		entry->gen = gen;
		entry->num = num;
		entry->stm_ofs = stm_ofs;
		pdf_drop_obj(ctx, entry->obj);
		entry->obj = pdf_keep_obj(ctx, trailer);
		entry->type = 'n';
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, trailer);
		fz_rethrow(ctx);
	}

	return trailer;
}

static pdf_obj *
pdf_read_xref(fz_context *ctx, pdf_document *doc, int64_t ofs, pdf_lexbuf *buf)
{
	pdf_obj *trailer;
	int c;

	fz_seek(ctx, doc->file, ofs, SEEK_SET);

	while (iswhite(fz_peek_byte(ctx, doc->file)))
		fz_read_byte(ctx, doc->file);

	c = fz_peek_byte(ctx, doc->file);
	if (c == 'x')
		trailer = pdf_read_old_xref(ctx, doc, buf);
	else if (isdigit(c))
		trailer = pdf_read_new_xref(ctx, doc, buf);
	else
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot recognize xref format");

	return trailer;
}

static int64_t
read_xref_section(fz_context *ctx, pdf_document *doc, int64_t ofs, pdf_lexbuf *buf)
{
	pdf_obj *trailer = NULL;
	pdf_obj *prevobj;
	int64_t xrefstmofs = 0;
	int64_t prevofs = 0;

	trailer = pdf_read_xref(ctx, doc, ofs, buf);
	fz_try(ctx)
	{
		pdf_set_populating_xref_trailer(ctx, doc, trailer);

		/* FIXME: do we overwrite free entries properly? */
		/* FIXME: Does this work properly with progression? */
		xrefstmofs = pdf_to_int64(ctx, pdf_dict_get(ctx, trailer, PDF_NAME(XRefStm)));
		if (xrefstmofs)
		{
			if (xrefstmofs < 0)
				fz_throw(ctx, FZ_ERROR_GENERIC, "negative xref stream offset");

			/*
				Read the XRefStm stream, but throw away the resulting trailer. We do not
				follow any Prev tag therein, as specified on Page 108 of the PDF reference
				1.7
			*/
			pdf_drop_obj(ctx, pdf_read_xref(ctx, doc, xrefstmofs, buf));
		}

		prevobj = pdf_dict_get(ctx, trailer, PDF_NAME(Prev));
		if (pdf_is_int(ctx, prevobj))
		{
			prevofs = pdf_to_int64(ctx, prevobj);
			if (prevofs <= 0)
				fz_throw(ctx, FZ_ERROR_GENERIC, "invalid offset for previous xref section");
		}
	}
	fz_always(ctx)
		pdf_drop_obj(ctx, trailer);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return prevofs;
}

static void
pdf_read_xref_sections(fz_context *ctx, pdf_document *doc, int64_t ofs, pdf_lexbuf *buf, int read_previous)
{
	int i, len, cap;
	int64_t *offsets;

	len = 0;
	cap = 10;
	offsets = fz_malloc_array(ctx, cap, sizeof(*offsets));

	fz_try(ctx)
	{
		while(ofs)
		{
			for (i = 0; i < len; i ++)
			{
				if (offsets[i] == ofs)
					break;
			}
			if (i < len)
			{
				fz_warn(ctx, "ignoring xref section recursion at offset %d", (int)ofs);
				break;
			}
			if (len == cap)
			{
				cap *= 2;
				offsets = fz_resize_array(ctx, offsets, cap, sizeof(*offsets));
			}
			offsets[len++] = ofs;

			pdf_populate_next_xref_level(ctx, doc);
			ofs = read_xref_section(ctx, doc, ofs, buf);
			if (!read_previous)
				break;
		}
	}
	fz_always(ctx)
	{
		fz_free(ctx, offsets);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void
pdf_prime_xref_index(fz_context *ctx, pdf_document *doc)
{
	int i, j;
	int *idx = doc->xref_index;

	for (i = doc->num_xref_sections-1; i >= 0; i--)
	{
		pdf_xref *xref = &doc->xref_sections[i];
		pdf_xref_subsec *subsec = xref->subsec;
		while (subsec != NULL)
		{
			int start = subsec->start;
			int end = subsec->start + subsec->len;
			for (j = start; j < end; j++)
			{
				char t = subsec->table[j-start].type;
				if (t != 0 && t != 'f')
					idx[j] = i;
			}

			subsec = subsec->next;
		}
	}
}

/*
 * load xref tables from pdf
 *
 * File locked on entry, throughout and on exit.
 */

static void
pdf_load_xref(fz_context *ctx, pdf_document *doc, pdf_lexbuf *buf)
{
	int i;
	int xref_len;
	pdf_xref_entry *entry;

	pdf_read_start_xref(ctx, doc);

	pdf_read_xref_sections(ctx, doc, doc->startxref, buf, 1);

	if (pdf_xref_len(ctx, doc) == 0)
		fz_throw(ctx, FZ_ERROR_GENERIC, "found xref was empty");

	pdf_prime_xref_index(ctx, doc);

	entry = pdf_get_xref_entry(ctx, doc, 0);
	/* broken pdfs where first object is missing */
	if (!entry->type)
	{
		entry->type = 'f';
		entry->gen = 65535;
		entry->num = 0;
	}
	/* broken pdfs where first object is not free */
	else if (entry->type != 'f')
		fz_warn(ctx, "first object in xref is not free");

	/* broken pdfs where object offsets are out of range */
	xref_len = pdf_xref_len(ctx, doc);
	for (i = 0; i < xref_len; i++)
	{
		entry = pdf_get_xref_entry(ctx, doc, i);
		if (entry->type == 'n')
		{
			/* Special case code: "0000000000 * n" means free,
			 * according to some producers (inc Quartz) */
			if (entry->ofs == 0)
				entry->type = 'f';
			else if (entry->ofs <= 0 || entry->ofs >= doc->file_size)
				fz_throw(ctx, FZ_ERROR_GENERIC, "object offset out of range: %d (%d 0 R)", (int)entry->ofs, i);
		}
		if (entry->type == 'o')
		{
			/* Read this into a local variable here, because pdf_get_xref_entry
			 * may solidify the xref, hence invalidating "entry", meaning we
			 * need a stashed value for the throw. */
			int64_t ofs = entry->ofs;
			if (ofs <= 0 || ofs >= xref_len || pdf_get_xref_entry(ctx, doc, ofs)->type != 'n')
				fz_throw(ctx, FZ_ERROR_GENERIC, "invalid reference to an objstm that does not exist: %d (%d 0 R)", (int)ofs, i);
		}
	}
}

/*
 * Initialize and load xref tables.
 * If password is not null, try to decrypt.
 */

static void
pdf_init_document(fz_context *ctx, pdf_document *doc)
{
	pdf_obj *encrypt, *id;
	pdf_obj *dict = NULL;
	pdf_obj *obj;
	pdf_obj *nobj = NULL;
	int i, repaired = 0;

	fz_var(dict);
	fz_var(nobj);

	fz_try(ctx)
	{
		pdf_load_version(ctx, doc);
		pdf_load_xref(ctx, doc, &doc->lexbuf.base);
	}
	fz_catch(ctx)
	{
		pdf_drop_xref_sections(ctx, doc);
		fz_warn(ctx, "trying to repair broken xref");
		repaired = 1;
	}

	fz_try(ctx)
	{
		int hasroot, hasinfo;

		if (repaired)
		{
			/* pdf_repair_xref may access xref_index, so reset it properly */
			if (doc->xref_index)
				memset(doc->xref_index, 0, sizeof(int) * doc->max_xref_len);
			pdf_repair_xref(ctx, doc);
			pdf_prime_xref_index(ctx, doc);
		}

		encrypt = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Encrypt));
		id = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(ID));
		if (pdf_is_dict(ctx, encrypt))
			doc->crypt = pdf_new_crypt(ctx, encrypt, id);

		/* Allow lazy clients to read encrypted files with a blank password */
		pdf_authenticate_password(ctx, doc, "");

		if (repaired)
		{
			int xref_len = pdf_xref_len(ctx, doc);
			pdf_repair_obj_stms(ctx, doc);

			hasroot = (pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root)) != NULL);
			hasinfo = (pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info)) != NULL);

			for (i = 1; i < xref_len && !hasinfo && !hasroot; ++i)
			{
				pdf_xref_entry *entry = pdf_get_xref_entry(ctx, doc, i);
				if (entry->type == 0 || entry->type == 'f')
					continue;

				fz_try(ctx)
				{
					dict = pdf_load_object(ctx, doc, i);
				}
				fz_catch(ctx)
				{
					fz_warn(ctx, "ignoring broken object (%d 0 R)", i);
					continue;
				}

				if (!hasroot)
				{
					obj = pdf_dict_get(ctx, dict, PDF_NAME(Type));
					if (pdf_name_eq(ctx, obj, PDF_NAME(Catalog)))
					{
						nobj = pdf_new_indirect(ctx, doc, i, 0);
						pdf_dict_put_drop(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root), nobj);
						hasroot = 1;
					}
				}

				if (!hasinfo)
				{
					if (pdf_dict_get(ctx, dict, PDF_NAME(Creator)) || pdf_dict_get(ctx, dict, PDF_NAME(Producer)))
					{
						nobj = pdf_new_indirect(ctx, doc, i, 0);
						pdf_dict_put_drop(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info), nobj);
						hasinfo = 1;
					}
				}

				pdf_drop_obj(ctx, dict);
				dict = NULL;
			}

			/* ensure that strings are not used in their repaired, non-decrypted form */
			if (doc->crypt)
				pdf_clear_xref(ctx, doc);
		}
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, dict);
		fz_rethrow(ctx);
	}

	fz_try(ctx)
	{
		pdf_read_ocg(ctx, doc);
	}
	fz_catch(ctx)
	{
		fz_warn(ctx, "Ignoring Broken Optional Content");
	}

	fz_try(ctx)
	{
		const char *version_str;
		obj = pdf_dict_getl(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root), PDF_NAME(Version), NULL);
		version_str = pdf_to_name(ctx, obj);
		if (*version_str)
		{
			int version = 10 * (fz_atof(version_str) + 0.05f);
			if (version > doc->version)
				doc->version = version;
		}
	}
	fz_catch(ctx) { }
}

static void
pdf_drop_document_imp(fz_context *ctx, pdf_document *doc)
{
	int i;

	fz_defer_reap_start(ctx);

	/* Type3 glyphs in the glyph cache can contain pdf_obj pointers
	 * that we are about to destroy. Simplest solution is to bin the
	 * glyph cache at this point. */
	fz_try(ctx)
		fz_purge_glyph_cache(ctx);
	fz_catch(ctx)
	{
		/* Swallow error, but continue dropping */
	}

	pdf_drop_js(ctx, doc->js);

	pdf_drop_xref_sections(ctx, doc);
	fz_free(ctx, doc->xref_index);

	fz_drop_stream(ctx, doc->file);
	pdf_drop_crypt(ctx, doc->crypt);

	for (i=0; i < doc->num_type3_fonts; i++)
	{
		fz_try(ctx)
			fz_decouple_type3_font(ctx, doc->type3_fonts[i], (void *)doc);
		fz_always(ctx)
			fz_drop_font(ctx, doc->type3_fonts[i]);
		fz_catch(ctx)
		{
			/* Swallow error, but continue dropping */
		}
	}

	fz_free(ctx, doc->type3_fonts);

	pdf_drop_ocg(ctx, doc);

	pdf_empty_store(ctx, doc);

	pdf_lexbuf_fin(ctx, &doc->lexbuf.base);

	pdf_drop_resource_tables(ctx, doc);

	fz_drop_colorspace(ctx, doc->oi);

	for (i = 0; i < doc->orphans_count; i++)
		pdf_drop_obj(ctx, doc->orphans[i]);

	fz_free(ctx, doc->orphans);

	fz_free(ctx, doc->rev_page_map);

	fz_defer_reap_end(ctx);
}

/*
	Closes and frees an opened PDF document.

	The resource store in the context associated with pdf_document
	is emptied.
*/
void
pdf_drop_document(fz_context *ctx, pdf_document *doc)
{
	fz_drop_document(ctx, &doc->super);
}

pdf_document *
pdf_keep_document(fz_context *ctx, pdf_document *doc)
{
	return (pdf_document *)fz_keep_document(ctx, &doc->super);
}

/*
 * compressed object streams
 */

static pdf_xref_entry *
pdf_load_obj_stm(fz_context *ctx, pdf_document *doc, int num, pdf_lexbuf *buf, int target)
{
	fz_stream *stm = NULL;
	pdf_obj *objstm = NULL;
	int *numbuf = NULL;
	int64_t *ofsbuf = NULL;

	pdf_obj *obj;
	int64_t first;
	int count;
	int i;
	pdf_token tok;
	pdf_xref_entry *ret_entry = NULL;
	int xref_len;
	int found;

	fz_var(numbuf);
	fz_var(ofsbuf);
	fz_var(objstm);
	fz_var(stm);

	fz_try(ctx)
	{
		objstm = pdf_load_object(ctx, doc, num);

		if (pdf_obj_marked(ctx, objstm))
			fz_throw(ctx, FZ_ERROR_GENERIC, "recursive object stream lookup");
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, objstm);
		fz_rethrow(ctx);
	}

	fz_try(ctx)
	{
		pdf_mark_obj(ctx, objstm);

		count = pdf_dict_get_int(ctx, objstm, PDF_NAME(N));
		first = pdf_dict_get_int(ctx, objstm, PDF_NAME(First));

		if (count < 0 || count > PDF_MAX_OBJECT_NUMBER)
			fz_throw(ctx, FZ_ERROR_GENERIC, "number of objects in object stream out of range");
		if (first < 0 || first > PDF_MAX_OBJECT_NUMBER
				|| count < 0 || count > PDF_MAX_OBJECT_NUMBER
				|| first + count - 1 > PDF_MAX_OBJECT_NUMBER)
			fz_throw(ctx, FZ_ERROR_GENERIC, "object stream object numbers are out of range");

		numbuf = fz_calloc(ctx, count, sizeof(*numbuf));
		ofsbuf = fz_calloc(ctx, count, sizeof(*ofsbuf));

		xref_len = pdf_xref_len(ctx, doc);

		found = 0;

		stm = pdf_open_stream_number(ctx, doc, num);
		for (i = 0; i < count; i++)
		{
			tok = pdf_lex(ctx, stm, buf);
			if (tok != PDF_TOK_INT)
				fz_throw(ctx, FZ_ERROR_GENERIC, "corrupt object stream (%d 0 R)", num);
			numbuf[found] = buf->i;

			tok = pdf_lex(ctx, stm, buf);
			if (tok != PDF_TOK_INT)
				fz_throw(ctx, FZ_ERROR_GENERIC, "corrupt object stream (%d 0 R)", num);
			ofsbuf[found] = buf->i;

			if (numbuf[found] <= 0 || numbuf[found] >= xref_len)
				fz_warn(ctx, "object stream object out of range, skipping");
			else
				found++;
		}

		for (i = 0; i < found; i++)
		{
			pdf_xref_entry *entry;

			fz_seek(ctx, stm, first + ofsbuf[i], SEEK_SET);

			obj = pdf_parse_stm_obj(ctx, doc, stm, buf);

			entry = pdf_get_xref_entry(ctx, doc, numbuf[i]);

			pdf_set_obj_parent(ctx, obj, numbuf[i]);

			if (entry->type == 'o' && entry->ofs == num)
			{
				/* If we already have an entry for this object,
				 * we'd like to drop it and use the new one -
				 * but this means that anyone currently holding
				 * a pointer to the old one will be left with a
				 * stale pointer. Instead, we drop the new one
				 * and trust that the old one is correct. */
				if (entry->obj)
				{
					if (pdf_objcmp(ctx, entry->obj, obj))
						fz_warn(ctx, "Encountered new definition for object %d - keeping the original one", numbuf[i]);
					pdf_drop_obj(ctx, obj);
				}
				else
				{
					entry->obj = obj;
					fz_drop_buffer(ctx, entry->stm_buf);
					entry->stm_buf = NULL;
				}
				if (numbuf[i] == target)
					ret_entry = entry;
			}
			else
			{
				pdf_drop_obj(ctx, obj);
			}
		}
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
		fz_free(ctx, ofsbuf);
		fz_free(ctx, numbuf);
		pdf_unmark_obj(ctx, objstm);
		pdf_drop_obj(ctx, objstm);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
	return ret_entry;
}

pdf_xref_entry *
pdf_cache_object(fz_context *ctx, pdf_document *doc, int num)
{
	pdf_xref_entry *x;
	int rnum, rgen, try_repair;

	fz_var(try_repair);

	if (num <= 0 || num >= pdf_xref_len(ctx, doc))
		fz_throw(ctx, FZ_ERROR_GENERIC, "object out of range (%d 0 R); xref size %d", num, pdf_xref_len(ctx, doc));

object_updated:
	try_repair = 0;
	rnum = num;

	x = pdf_get_xref_entry(ctx, doc, num);

	if (x->obj != NULL)
		return x;

	if (x->type == 'f')
	{
		x->obj = PDF_NULL;
	}
	else if (x->type == 'n')
	{
		fz_seek(ctx, doc->file, x->ofs, SEEK_SET);

		fz_try(ctx)
		{
			x->obj = pdf_parse_ind_obj(ctx, doc, doc->file, &doc->lexbuf.base,
					&rnum, &rgen, &x->stm_ofs, &try_repair);
		}
		fz_catch(ctx)
		{
			if (!try_repair)
				fz_rethrow(ctx);
		}

		if (!try_repair && rnum != num)
		{
			pdf_drop_obj(ctx, x->obj);
			x->type = 'f';
			x->ofs = -1;
			x->gen = 0;
			x->num = 0;
			x->stm_ofs = 0;
			x->obj = NULL;
			try_repair = (doc->repair_attempted == 0);
		}

		if (try_repair)
		{
			fz_try(ctx)
			{
				pdf_repair_xref(ctx, doc);
				pdf_prime_xref_index(ctx, doc);
				pdf_repair_obj_stms(ctx, doc);
			}
			fz_catch(ctx)
			{
				if (rnum == num)
					fz_throw(ctx, FZ_ERROR_GENERIC, "cannot parse object (%d 0 R)", num);
				else
					fz_throw(ctx, FZ_ERROR_GENERIC, "found object (%d 0 R) instead of (%d 0 R)", rnum, num);
			}
			goto object_updated;
		}

		if (doc->crypt)
			pdf_crypt_obj(ctx, doc->crypt, x->obj, x->num, x->gen);
	}
	else if (x->type == 'o')
	{
		if (!x->obj)
		{
			x = pdf_load_obj_stm(ctx, doc, x->ofs, &doc->lexbuf.base, num);
			if (x == NULL)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot load object stream containing object (%d 0 R)", num);
			if (!x->obj)
				fz_throw(ctx, FZ_ERROR_GENERIC, "object (%d 0 R) was not found in its object stream", num);
		}
	}
	else
	{
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find object in xref (%d 0 R)", num);
	}

	pdf_set_obj_parent(ctx, x->obj, num);
	return x;
}

pdf_obj *
pdf_load_object(fz_context *ctx, pdf_document *doc, int num)
{
	pdf_xref_entry *entry = pdf_cache_object(ctx, doc, num);
	return pdf_keep_obj(ctx, entry->obj);
}

pdf_obj *
pdf_resolve_indirect(fz_context *ctx, pdf_obj *ref)
{
	if (pdf_is_indirect(ctx, ref))
	{
		pdf_document *doc = pdf_get_indirect_document(ctx, ref);
		int num = pdf_to_num(ctx, ref);
		pdf_xref_entry *entry;

		if (!doc)
			return NULL;
		if (num <= 0)
		{
			fz_warn(ctx, "invalid indirect reference (%d 0 R)", num);
			return NULL;
		}

		fz_try(ctx)
			entry = pdf_cache_object(ctx, doc, num);
		fz_catch(ctx)
		{
			fz_warn(ctx, "cannot load object (%d 0 R) into cache", num);
			return NULL;
		}

		ref = entry->obj;
	}
	return ref;
}

pdf_obj *
pdf_resolve_indirect_chain(fz_context *ctx, pdf_obj *ref)
{
	int sanity = 10;

	while (pdf_is_indirect(ctx, ref))
	{
		if (--sanity == 0)
		{
			fz_warn(ctx, "too many indirections (possible indirection cycle involving %d 0 R)", pdf_to_num(ctx, ref));
			return NULL;
		}

		ref = pdf_resolve_indirect(ctx, ref);
	}

	return ref;
}

int
pdf_count_objects(fz_context *ctx, pdf_document *doc)
{
	return pdf_xref_len(ctx, doc);
}

/*
	Allocate a slot in the xref table and return a fresh unused object number.
*/
int
pdf_create_object(fz_context *ctx, pdf_document *doc)
{
	/* TODO: reuse free object slots by properly linking free object chains in the ofs field */
	pdf_xref_entry *entry;
	int num = pdf_xref_len(ctx, doc);

	if (num > PDF_MAX_OBJECT_NUMBER)
		fz_throw(ctx, FZ_ERROR_GENERIC, "too many objects stored in pdf");

	entry = pdf_get_incremental_xref_entry(ctx, doc, num);
	entry->type = 'f';
	entry->ofs = -1;
	entry->gen = 0;
	entry->num = num;
	entry->stm_ofs = 0;
	entry->stm_buf = NULL;
	entry->obj = NULL;
	return num;
}

/*
	Remove object from xref table, marking the slot as free.
*/
void
pdf_delete_object(fz_context *ctx, pdf_document *doc, int num)
{
	pdf_xref_entry *x;

	if (num <= 0 || num >= pdf_xref_len(ctx, doc))
	{
		fz_warn(ctx, "object out of range (%d 0 R); xref size %d", num, pdf_xref_len(ctx, doc));
		return;
	}

	x = pdf_get_incremental_xref_entry(ctx, doc, num);

	fz_drop_buffer(ctx, x->stm_buf);
	pdf_drop_obj(ctx, x->obj);

	x->type = 'f';
	x->ofs = 0;
	x->gen += 1;
	x->num = 0;
	x->stm_ofs = 0;
	x->stm_buf = NULL;
	x->obj = NULL;
}

/*
	Replace object in xref table with the passed in object.
*/
void
pdf_update_object(fz_context *ctx, pdf_document *doc, int num, pdf_obj *newobj)
{
	pdf_xref_entry *x;

	if (num <= 0 || num >= pdf_xref_len(ctx, doc))
	{
		fz_warn(ctx, "object out of range (%d 0 R); xref size %d", num, pdf_xref_len(ctx, doc));
		return;
	}

	if (!newobj)
	{
		pdf_delete_object(ctx, doc, num);
		return;
	}

	x = pdf_get_incremental_xref_entry(ctx, doc, num);

	pdf_drop_obj(ctx, x->obj);

	x->type = 'n';
	x->ofs = 0;
	x->obj = pdf_keep_obj(ctx, newobj);

	pdf_set_obj_parent(ctx, newobj, num);
}

/*
	Replace stream contents for object in xref table with the passed in buffer.

	The buffer contents must match the /Filter setting if 'compressed' is true.
	If 'compressed' is false, the /Filter and /DecodeParms entries are deleted.
	The /Length entry is updated.
*/
void
pdf_update_stream(fz_context *ctx, pdf_document *doc, pdf_obj *obj, fz_buffer *newbuf, int compressed)
{
	int num;
	pdf_xref_entry *x;

	if (pdf_is_indirect(ctx, obj))
		num = pdf_to_num(ctx, obj);
	else
		num = pdf_obj_parent_num(ctx, obj);
	if (num <= 0 || num >= pdf_xref_len(ctx, doc))
	{
		fz_warn(ctx, "object out of range (%d 0 R); xref size %d", num, pdf_xref_len(ctx, doc));
		return;
	}

	x = pdf_get_xref_entry(ctx, doc, num);

	fz_drop_buffer(ctx, x->stm_buf);
	x->stm_buf = fz_keep_buffer(ctx, newbuf);

	pdf_dict_put_int(ctx, obj, PDF_NAME(Length), (int)fz_buffer_storage(ctx, newbuf, NULL));
	if (!compressed)
	{
		pdf_dict_del(ctx, obj, PDF_NAME(Filter));
		pdf_dict_del(ctx, obj, PDF_NAME(DecodeParms));
	}
}

int
pdf_lookup_metadata(fz_context *ctx, pdf_document *doc, const char *key, char *buf, int size)
{
	if (!strcmp(key, "format"))
		return (int)fz_snprintf(buf, size, "PDF %d.%d", doc->version/10, doc->version % 10);

	if (!strcmp(key, "encryption"))
	{
		if (doc->crypt)
			return (int)fz_snprintf(buf, size, "Standard V%d R%d %d-bit %s",
					pdf_crypt_version(ctx, doc),
					pdf_crypt_revision(ctx, doc),
					pdf_crypt_length(ctx, doc),
					pdf_crypt_method(ctx, doc));
		else
			return (int)fz_strlcpy(buf, "None", size);
	}

	if (strstr(key, "info:") == key)
	{
		pdf_obj *info;
		const char *s;
		int n;

		info = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Info));
		if (!info)
			return -1;

		info = pdf_dict_gets(ctx, info, key + 5);
		if (!info)
			return -1;

		s = pdf_to_text_string(ctx, info);
		n = (int)fz_strlcpy(buf, s, size);
		return n;
	}

	return -1;
}

/*
	Initializers for the fz_document interface.

	The functions are split across two files to allow calls to a
	version of the constructor that does not link in the interpreter.
	The interpreter references the built-in font and cmap resources
	which are quite big. Not linking those into the mutool binary
	saves roughly 6MB of space.
*/

static pdf_document *
pdf_new_document(fz_context *ctx, fz_stream *file)
{
	pdf_document *doc = fz_new_derived_document(ctx, pdf_document);

	doc->super.drop_document = (fz_document_drop_fn*)pdf_drop_document_imp;
	doc->super.get_output_intent = (fz_document_output_intent_fn*)pdf_document_output_intent;
	doc->super.needs_password = (fz_document_needs_password_fn*)pdf_needs_password;
	doc->super.authenticate_password = (fz_document_authenticate_password_fn*)pdf_authenticate_password;
	doc->super.has_permission = (fz_document_has_permission_fn*)pdf_has_permission;
	doc->super.load_outline = (fz_document_load_outline_fn*)pdf_load_outline;
	doc->super.resolve_link = (fz_document_resolve_link_fn*)pdf_resolve_link;
	doc->super.count_pages = (fz_document_count_pages_fn*)pdf_count_pages;
	doc->super.load_page = (fz_document_load_page_fn*)pdf_load_page;
	doc->super.lookup_metadata = (fz_document_lookup_metadata_fn*)pdf_lookup_metadata;

	pdf_lexbuf_init(ctx, &doc->lexbuf.base, PDF_LEXBUF_LARGE);
	doc->file = fz_keep_stream(ctx, file);

	return doc;
}

/*
	Opens a PDF document.

	Same as pdf_open_document, but takes a stream instead of a
	filename to locate the PDF document to open. Increments the
	reference count of the stream. See fz_open_file,
	fz_open_file_w or fz_open_fd for opening a stream, and
	fz_drop_stream for closing an open stream.
*/
pdf_document *
pdf_open_document_with_stream(fz_context *ctx, fz_stream *file)
{
	pdf_document *doc = pdf_new_document(ctx, file);
	fz_try(ctx)
	{
		pdf_init_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		fz_drop_document(ctx, &doc->super);
		fz_rethrow(ctx);
	}
	return doc;
}

/*
	Open a PDF document.

	Open a PDF document by reading its cross reference table, so
	MuPDF can locate PDF objects inside the file. Upon an broken
	cross reference table or other parse errors MuPDF will restart
	parsing the file from the beginning to try to rebuild a
	(hopefully correct) cross reference table to allow further
	processing of the file.

	The returned pdf_document should be used when calling most
	other PDF functions. Note that it wraps the context, so those
	functions implicitly get access to the global state in
	context.

	filename: a path to a file as it would be given to open(2).
*/
pdf_document *
pdf_open_document(fz_context *ctx, const char *filename)
{
	fz_stream *file = NULL;
	pdf_document *doc = NULL;

	fz_var(file);
	fz_var(doc);

	fz_try(ctx)
	{
		file = fz_open_file(ctx, filename);
		doc = pdf_new_document(ctx, file);
		pdf_init_document(ctx, doc);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, file);
	}
	fz_catch(ctx)
	{
		fz_drop_document(ctx, &doc->super);
		fz_rethrow(ctx);
	}
	return doc;
}

/*
		Down-cast generic fitz objects into pdf specific variants.
		Returns NULL if the objects are not from a PDF document.
*/
pdf_document *pdf_document_from_fz_document(fz_context *ctx, fz_document *ptr)
{
	return (pdf_document *)((ptr && ptr->count_pages == (fz_document_count_pages_fn*)pdf_count_pages) ? ptr : NULL);
}

pdf_page *pdf_page_from_fz_page(fz_context *ctx, fz_page *ptr)
{
	return (pdf_page *)((ptr && ptr->bound_page == (fz_page_bound_page_fn*)pdf_bound_page) ? ptr : NULL);
}

/*
	down-cast a fz_document to a pdf_document.
	Returns NULL if underlying document is not PDF
*/
pdf_document *pdf_specifics(fz_context *ctx, fz_document *doc)
{
	return pdf_document_from_fz_document(ctx, doc);
}

pdf_obj *
pdf_add_object(fz_context *ctx, pdf_document *doc, pdf_obj *obj)
{
	pdf_document *orig_doc;
	int num;

	orig_doc = pdf_get_bound_document(ctx, obj);
	if (orig_doc && orig_doc != doc)
		fz_throw(ctx, FZ_ERROR_GENERIC, "tried to add an object belonging to a different document");
	if (pdf_is_indirect(ctx, obj))
		return pdf_keep_obj(ctx, obj);
	num = pdf_create_object(ctx, doc);
	pdf_update_object(ctx, doc, num, obj);
	return pdf_new_indirect(ctx, doc, num, 0);
}

pdf_obj *
pdf_add_object_drop(fz_context *ctx, pdf_document *doc, pdf_obj *obj)
{
	pdf_obj *ind = NULL;
	fz_try(ctx)
		ind = pdf_add_object(ctx, doc, obj);
	fz_always(ctx)
		pdf_drop_obj(ctx, obj);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return ind;
}

pdf_obj *
pdf_add_new_dict(fz_context *ctx, pdf_document *doc, int initial)
{
	return pdf_add_object_drop(ctx, doc, pdf_new_dict(ctx, doc, initial));
}

pdf_obj *
pdf_add_new_array(fz_context *ctx, pdf_document *doc, int initial)
{
	return pdf_add_object_drop(ctx, doc, pdf_new_array(ctx, doc, initial));
}

pdf_obj *
pdf_add_stream(fz_context *ctx, pdf_document *doc, fz_buffer *buf, pdf_obj *obj, int compressed)
{
	pdf_obj *ind;
	if (!obj)
		ind = pdf_add_new_dict(ctx, doc, 4);
	else
		ind = pdf_add_object(ctx, doc, obj);
	fz_try(ctx)
		pdf_update_stream(ctx, doc, ind, buf, compressed);
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, ind);
		fz_rethrow(ctx);
	}
	return ind;
}

pdf_document *pdf_create_document(fz_context *ctx)
{
	pdf_document *doc;
	pdf_obj *root;
	pdf_obj *pages;
	pdf_obj *trailer = NULL;

	fz_var(trailer);

	doc = pdf_new_document(ctx, NULL);
	fz_try(ctx)
	{
		doc->version = 14;
		doc->file_size = 0;
		doc->startxref = 0;
		doc->num_xref_sections = 0;
		doc->num_incremental_sections = 0;
		doc->xref_base = 0;
		doc->disallow_new_increments = 0;
		pdf_get_populating_xref_entry(ctx, doc, 0);

		trailer = pdf_new_dict(ctx, doc, 2);
		pdf_dict_put_int(ctx, trailer, PDF_NAME(Size), 3);
		pdf_dict_put_drop(ctx, trailer, PDF_NAME(Root), root = pdf_add_new_dict(ctx, doc, 2));
		pdf_dict_put(ctx, root, PDF_NAME(Type), PDF_NAME(Catalog));
		pdf_dict_put_drop(ctx, root, PDF_NAME(Pages), pages = pdf_add_new_dict(ctx, doc, 3));
		pdf_dict_put(ctx, pages, PDF_NAME(Type), PDF_NAME(Pages));
		pdf_dict_put_int(ctx, pages, PDF_NAME(Count), 0);
		pdf_dict_put_array(ctx, pages, PDF_NAME(Kids), 1);

		/* Set the trailer of the final xref section. */
		doc->xref_sections[0].trailer = trailer;
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, trailer);
		fz_drop_document(ctx, &doc->super);
		fz_rethrow(ctx);
	}
	return doc;
}

static const char *pdf_extensions[] =
{
	"pdf",
	"pclm",
	"ai",
	NULL
};

static const char *pdf_mimetypes[] =
{
	"application/pdf",
	"application/PCLm",
	NULL
};

fz_document_handler pdf_document_handler =
{
	NULL,
	(fz_document_open_fn*)pdf_open_document,
	(fz_document_open_with_stream_fn*)pdf_open_document_with_stream,
	pdf_extensions,
	pdf_mimetypes
};

void pdf_mark_xref(fz_context *ctx, pdf_document *doc)
{
	int x, e;

	for (x = 0; x < doc->num_xref_sections; x++)
	{
		pdf_xref *xref = &doc->xref_sections[x];
		pdf_xref_subsec *sub;

		for (sub = xref->subsec; sub != NULL; sub = sub->next)
		{
			for (e = 0; e < sub->len; e++)
			{
				pdf_xref_entry *entry = &sub->table[e];
				if (entry->obj)
				{
					entry->marked = 1;
				}
			}
		}
	}
}

void pdf_clear_xref(fz_context *ctx, pdf_document *doc)
{
	int x, e;

	for (x = 0; x < doc->num_xref_sections; x++)
	{
		pdf_xref *xref = &doc->xref_sections[x];
		pdf_xref_subsec *sub;

		for (sub = xref->subsec; sub != NULL; sub = sub->next)
		{
			for (e = 0; e < sub->len; e++)
			{
				pdf_xref_entry *entry = &sub->table[e];
				/* We cannot drop objects if the stream
				 * buffer has been updated */
				if (entry->obj != NULL && entry->stm_buf == NULL)
				{
					if (pdf_obj_refs(ctx, entry->obj) == 1)
					{
						pdf_drop_obj(ctx, entry->obj);
						entry->obj = NULL;
					}
				}
			}
		}
	}
}

void pdf_clear_xref_to_mark(fz_context *ctx, pdf_document *doc)
{
	int x, e;

	for (x = 0; x < doc->num_xref_sections; x++)
	{
		pdf_xref *xref = &doc->xref_sections[x];
		pdf_xref_subsec *sub;

		for (sub = xref->subsec; sub != NULL; sub = sub->next)
		{
			for (e = 0; e < sub->len; e++)
			{
				pdf_xref_entry *entry = &sub->table[e];

				/* We cannot drop objects if the stream buffer has
				 * been updated */
				if (entry->obj != NULL && entry->stm_buf == NULL)
				{
					if (!entry->marked && pdf_obj_refs(ctx, entry->obj) == 1)
					{
						pdf_drop_obj(ctx, entry->obj);
						entry->obj = NULL;
					}
				}
			}
		}
	}
}
