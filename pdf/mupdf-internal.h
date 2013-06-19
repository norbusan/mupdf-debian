#ifndef MUPDF_INTERNAL_H
#define MUPDF_INTERNAL_H

#include "mupdf.h"
#include "fitz-internal.h"

void pdf_set_str_len(pdf_obj *obj, int newlen);
void *pdf_get_indirect_document(pdf_obj *obj);
void pdf_set_int(pdf_obj *obj, int i);

/*
 * PDF Images
 */

typedef struct pdf_image_s pdf_image;

struct pdf_image_s
{
	fz_image base;
	fz_pixmap *tile;
	int n, bpc;
	fz_compressed_buffer *buffer;
	int colorkey[FZ_MAX_COLORS * 2];
	float decode[FZ_MAX_COLORS * 2];
	int imagemask;
	int interpolate;
	int usecolorkey;
};

/*
 * tokenizer and low-level object parser
 */

typedef enum
{
	PDF_TOK_ERROR, PDF_TOK_EOF,
	PDF_TOK_OPEN_ARRAY, PDF_TOK_CLOSE_ARRAY,
	PDF_TOK_OPEN_DICT, PDF_TOK_CLOSE_DICT,
	PDF_TOK_OPEN_BRACE, PDF_TOK_CLOSE_BRACE,
	PDF_TOK_NAME, PDF_TOK_INT, PDF_TOK_REAL, PDF_TOK_STRING, PDF_TOK_KEYWORD,
	PDF_TOK_R, PDF_TOK_TRUE, PDF_TOK_FALSE, PDF_TOK_NULL,
	PDF_TOK_OBJ, PDF_TOK_ENDOBJ,
	PDF_TOK_STREAM, PDF_TOK_ENDSTREAM,
	PDF_TOK_XREF, PDF_TOK_TRAILER, PDF_TOK_STARTXREF,
	PDF_NUM_TOKENS
} pdf_token;

enum
{
	PDF_LEXBUF_SMALL = 256,
	PDF_LEXBUF_LARGE = 65536
};

typedef struct pdf_lexbuf_s pdf_lexbuf;
typedef struct pdf_lexbuf_large_s pdf_lexbuf_large;

struct pdf_lexbuf_s
{
	fz_context *ctx;
	int size;
	int base_size;
	int len;
	int i;
	float f;
	char *scratch;
	char buffer[PDF_LEXBUF_SMALL];
};

struct pdf_lexbuf_large_s
{
	pdf_lexbuf base;
	char buffer[PDF_LEXBUF_LARGE - PDF_LEXBUF_SMALL];
};

void pdf_lexbuf_init(fz_context *ctx, pdf_lexbuf *lexbuf, int size);
void pdf_lexbuf_fin(pdf_lexbuf *lexbuf);
ptrdiff_t pdf_lexbuf_grow(pdf_lexbuf *lexbuf);

int pdf_lex(fz_stream *f, pdf_lexbuf *lexbuf);

pdf_obj *pdf_parse_array(pdf_document *doc, fz_stream *f, pdf_lexbuf *buf);
pdf_obj *pdf_parse_dict(pdf_document *doc, fz_stream *f, pdf_lexbuf *buf);
pdf_obj *pdf_parse_stm_obj(pdf_document *doc, fz_stream *f, pdf_lexbuf *buf);
pdf_obj *pdf_parse_ind_obj(pdf_document *doc, fz_stream *f, pdf_lexbuf *buf, int *num, int *gen, int *stm_ofs);

/*
	pdf_print_token: print a lexed token to a buffer, growing if necessary
*/
void pdf_print_token(fz_context *ctx, fz_buffer *buf, int tok, pdf_lexbuf *lex);

/*
 * xref and object / stream api
 */

typedef struct pdf_xref_entry_s pdf_xref_entry;

struct pdf_xref_entry_s
{
	char type;	/* 0=unset (f)ree i(n)use (o)bjstm */
	int ofs;	/* file offset / objstm object number */
	int gen;	/* generation / objstm index */
	int stm_ofs;	/* on-disk stream */
	fz_buffer *stm_buf; /* in-memory stream (for updated objects) */
	pdf_obj *obj;	/* stored/cached object */
};

typedef struct pdf_crypt_s pdf_crypt;
typedef struct pdf_ocg_descriptor_s pdf_ocg_descriptor;
typedef struct pdf_ocg_entry_s pdf_ocg_entry;
typedef struct pdf_hotspot_s pdf_hotspot;

struct pdf_ocg_entry_s
{
	int num;
	int gen;
	int state;
};

struct pdf_ocg_descriptor_s
{
	int len;
	pdf_ocg_entry *ocgs;
	pdf_obj *intent;
};

enum
{
	HOTSPOT_POINTER_DOWN = 0x1,
	HOTSPOT_POINTER_OVER = 0x2
};

struct pdf_hotspot_s
{
	int num;
	int gen;
	int state;
};

typedef struct pdf_js_s pdf_js;

struct pdf_document_s
{
	fz_document super;

	fz_context *ctx;
	fz_stream *file;

	int version;
	int startxref;
	int file_size;
	pdf_crypt *crypt;
	pdf_obj *trailer;
	pdf_ocg_descriptor *ocg;
	pdf_hotspot hotspot;

	int len;
	pdf_xref_entry *table;

	int page_len;
	int page_cap;
	pdf_obj **page_objs;
	pdf_obj **page_refs;
	int resources_localised;

	pdf_lexbuf_large lexbuf;

	pdf_annot *focus;
	pdf_obj *focus_obj;

	pdf_js *js;
	int recalculating;
	int dirty;

	fz_doc_event_cb *event_cb;
	void *event_cb_data;
};

pdf_document *pdf_open_document_no_run(fz_context *ctx, const char *filename);
pdf_document *pdf_open_document_no_run_with_stream(fz_context *ctx, fz_stream *file);

void pdf_localise_page_resources(pdf_document *xref);

void pdf_cache_object(pdf_document *doc, int num, int gen);

fz_stream *pdf_open_inline_stream(pdf_document *doc, pdf_obj *stmobj, int length, fz_stream *chain, fz_compression_params *params);
fz_compressed_buffer *pdf_load_compressed_stream(pdf_document *doc, int num, int gen);
fz_stream *pdf_open_stream_with_offset(pdf_document *doc, int num, int gen, pdf_obj *dict, int stm_ofs);
fz_stream *pdf_open_compressed_stream(fz_context *ctx, fz_compressed_buffer *);
fz_stream *pdf_open_contents_stream(pdf_document *xref, pdf_obj *obj);
fz_buffer *pdf_load_raw_renumbered_stream(pdf_document *doc, int num, int gen, int orig_num, int orig_gen);
fz_buffer *pdf_load_renumbered_stream(pdf_document *doc, int num, int gen, int orig_num, int orig_gen, int *truncated);
fz_stream *pdf_open_raw_renumbered_stream(pdf_document *doc, int num, int gen, int orig_num, int orig_gen);

void pdf_repair_xref(pdf_document *doc, pdf_lexbuf *buf);
void pdf_repair_obj_stms(pdf_document *doc);
void pdf_resize_xref(pdf_document *doc, int newcap);
pdf_obj *pdf_new_ref(pdf_document *doc, pdf_obj *obj);

void pdf_print_xref(pdf_document *);

/*
 * Encryption
 */

pdf_crypt *pdf_new_crypt(fz_context *ctx, pdf_obj *enc, pdf_obj *id);
void pdf_free_crypt(fz_context *ctx, pdf_crypt *crypt);

void pdf_crypt_obj(fz_context *ctx, pdf_crypt *crypt, pdf_obj *obj, int num, int gen);
void pdf_crypt_buffer(fz_context *ctx, pdf_crypt *crypt, fz_buffer *buf, int num, int gen);
fz_stream *pdf_open_crypt(fz_stream *chain, pdf_crypt *crypt, int num, int gen);
fz_stream *pdf_open_crypt_with_filter(fz_stream *chain, pdf_crypt *crypt, char *name, int num, int gen);

int pdf_crypt_version(pdf_document *doc);
int pdf_crypt_revision(pdf_document *doc);
char *pdf_crypt_method(pdf_document *doc);
int pdf_crypt_length(pdf_document *doc);
unsigned char *pdf_crypt_key(pdf_document *doc);

#ifndef NDEBUG
void pdf_print_crypt(pdf_crypt *crypt);
#endif

/*
 * Functions, Colorspaces, Shadings and Images
 */

typedef struct pdf_function_s pdf_function;

pdf_function *pdf_load_function(pdf_document *doc, pdf_obj *ref, int in, int out);
void pdf_eval_function(fz_context *ctx, pdf_function *func, float *in, int inlen, float *out, int outlen);
pdf_function *pdf_keep_function(fz_context *ctx, pdf_function *func);
void pdf_drop_function(fz_context *ctx, pdf_function *func);
unsigned int pdf_function_size(pdf_function *func);

fz_colorspace *pdf_load_colorspace(pdf_document *doc, pdf_obj *obj);
fz_pixmap *pdf_expand_indexed_pixmap(fz_context *ctx, fz_pixmap *src);

fz_shade *pdf_load_shading(pdf_document *doc, pdf_obj *obj);

fz_image *pdf_load_inline_image(pdf_document *doc, pdf_obj *rdb, pdf_obj *dict, fz_stream *file);
int pdf_is_jpx_image(fz_context *ctx, pdf_obj *dict);

/*
 * Pattern
 */

typedef struct pdf_pattern_s pdf_pattern;

struct pdf_pattern_s
{
	fz_storable storable;
	int ismask;
	float xstep;
	float ystep;
	fz_matrix matrix;
	fz_rect bbox;
	pdf_obj *resources;
	pdf_obj *contents;
};

pdf_pattern *pdf_load_pattern(pdf_document *doc, pdf_obj *obj);
pdf_pattern *pdf_keep_pattern(fz_context *ctx, pdf_pattern *pat);
void pdf_drop_pattern(fz_context *ctx, pdf_pattern *pat);

/*
 * XObject
 */

typedef struct pdf_xobject_s pdf_xobject;

struct pdf_xobject_s
{
	fz_storable storable;
	fz_matrix matrix;
	fz_rect bbox;
	int isolated;
	int knockout;
	int transparency;
	fz_colorspace *colorspace;
	pdf_obj *resources;
	pdf_obj *contents;
	pdf_obj *me;
	int iteration;
};

pdf_xobject *pdf_load_xobject(pdf_document *doc, pdf_obj *obj);
pdf_obj *pdf_new_xobject(pdf_document *doc, const fz_rect *bbox, const fz_matrix *mat);
pdf_xobject *pdf_keep_xobject(fz_context *ctx, pdf_xobject *xobj);
void pdf_drop_xobject(fz_context *ctx, pdf_xobject *xobj);
void pdf_update_xobject_contents(pdf_document *xref, pdf_xobject *form, fz_buffer *buffer);

void pdf_update_appearance(pdf_document *doc, pdf_obj *obj);

/*
 * CMap
 */

typedef struct pdf_cmap_s pdf_cmap;
typedef struct pdf_range_s pdf_range;

enum { PDF_CMAP_SINGLE, PDF_CMAP_RANGE, PDF_CMAP_TABLE, PDF_CMAP_MULTI };

struct pdf_range_s
{
	unsigned short low;
	/* Next, we pack 2 fields into the same unsigned short. Top 14 bits
	 * are the extent, bottom 2 bits are flags: single, range, table,
	 * multi */
	unsigned short extent_flags;
	unsigned short offset;	/* range-delta or table-index */
};

struct pdf_cmap_s
{
	fz_storable storable;
	char cmap_name[32];

	char usecmap_name[32];
	pdf_cmap *usecmap;

	int wmode;

	int codespace_len;
	struct
	{
		unsigned short n;
		unsigned short low;
		unsigned short high;
	} codespace[40];

	int rlen, rcap;
	pdf_range *ranges;

	int tlen, tcap;
	unsigned short *table;
};

pdf_cmap *pdf_new_cmap(fz_context *ctx);
pdf_cmap *pdf_keep_cmap(fz_context *ctx, pdf_cmap *cmap);
void pdf_drop_cmap(fz_context *ctx, pdf_cmap *cmap);
void pdf_free_cmap_imp(fz_context *ctx, fz_storable *cmap);
unsigned int pdf_cmap_size(fz_context *ctx, pdf_cmap *cmap);

int pdf_cmap_wmode(fz_context *ctx, pdf_cmap *cmap);
void pdf_set_cmap_wmode(fz_context *ctx, pdf_cmap *cmap, int wmode);
void pdf_set_usecmap(fz_context *ctx, pdf_cmap *cmap, pdf_cmap *usecmap);

void pdf_add_codespace(fz_context *ctx, pdf_cmap *cmap, int low, int high, int n);
void pdf_map_range_to_table(fz_context *ctx, pdf_cmap *cmap, int low, int *map, int len);
void pdf_map_range_to_range(fz_context *ctx, pdf_cmap *cmap, int srclo, int srchi, int dstlo);
void pdf_map_one_to_many(fz_context *ctx, pdf_cmap *cmap, int one, int *many, int len);
void pdf_sort_cmap(fz_context *ctx, pdf_cmap *cmap);

int pdf_lookup_cmap(pdf_cmap *cmap, int cpt);
int pdf_lookup_cmap_full(pdf_cmap *cmap, int cpt, int *out);
int pdf_decode_cmap(pdf_cmap *cmap, unsigned char *s, int *cpt);

pdf_cmap *pdf_new_identity_cmap(fz_context *ctx, int wmode, int bytes);
pdf_cmap *pdf_load_cmap(fz_context *ctx, fz_stream *file);
pdf_cmap *pdf_load_system_cmap(fz_context *ctx, char *name);
pdf_cmap *pdf_load_builtin_cmap(fz_context *ctx, char *name);
pdf_cmap *pdf_load_embedded_cmap(pdf_document *doc, pdf_obj *ref);

#ifndef NDEBUG
void pdf_print_cmap(fz_context *ctx, pdf_cmap *cmap);
#endif

/*
 * Font
 */

enum
{
	PDF_FD_FIXED_PITCH = 1 << 0,
	PDF_FD_SERIF = 1 << 1,
	PDF_FD_SYMBOLIC = 1 << 2,
	PDF_FD_SCRIPT = 1 << 3,
	PDF_FD_NONSYMBOLIC = 1 << 5,
	PDF_FD_ITALIC = 1 << 6,
	PDF_FD_ALL_CAP = 1 << 16,
	PDF_FD_SMALL_CAP = 1 << 17,
	PDF_FD_FORCE_BOLD = 1 << 18
};

enum { PDF_ROS_CNS, PDF_ROS_GB, PDF_ROS_JAPAN, PDF_ROS_KOREA };

void pdf_load_encoding(char **estrings, char *encoding);
int pdf_lookup_agl(char *name);
const char **pdf_lookup_agl_duplicates(int ucs);

extern const unsigned short pdf_doc_encoding[256];
extern const char * const pdf_mac_roman[256];
extern const char * const pdf_mac_expert[256];
extern const char * const pdf_win_ansi[256];
extern const char * const pdf_standard[256];

typedef struct pdf_font_desc_s pdf_font_desc;
typedef struct pdf_hmtx_s pdf_hmtx;
typedef struct pdf_vmtx_s pdf_vmtx;

struct pdf_hmtx_s
{
	unsigned short lo;
	unsigned short hi;
	int w;	/* type3 fonts can be big! */
};

struct pdf_vmtx_s
{
	unsigned short lo;
	unsigned short hi;
	short x;
	short y;
	short w;
};

struct pdf_font_desc_s
{
	fz_storable storable;
	unsigned int size;

	fz_font *font;

	/* FontDescriptor */
	int flags;
	float italic_angle;
	float ascent;
	float descent;
	float cap_height;
	float x_height;
	float missing_width;

	/* Encoding (CMap) */
	pdf_cmap *encoding;
	pdf_cmap *to_ttf_cmap;
	int cid_to_gid_len;
	unsigned short *cid_to_gid;

	/* ToUnicode */
	pdf_cmap *to_unicode;
	int cid_to_ucs_len;
	unsigned short *cid_to_ucs;

	/* Metrics (given in the PDF file) */
	int wmode;

	int hmtx_len, hmtx_cap;
	pdf_hmtx dhmtx;
	pdf_hmtx *hmtx;

	int vmtx_len, vmtx_cap;
	pdf_vmtx dvmtx;
	pdf_vmtx *vmtx;

	int is_embedded;
};

void pdf_set_font_wmode(fz_context *ctx, pdf_font_desc *font, int wmode);
void pdf_set_default_hmtx(fz_context *ctx, pdf_font_desc *font, int w);
void pdf_set_default_vmtx(fz_context *ctx, pdf_font_desc *font, int y, int w);
void pdf_add_hmtx(fz_context *ctx, pdf_font_desc *font, int lo, int hi, int w);
void pdf_add_vmtx(fz_context *ctx, pdf_font_desc *font, int lo, int hi, int x, int y, int w);
void pdf_end_hmtx(fz_context *ctx, pdf_font_desc *font);
void pdf_end_vmtx(fz_context *ctx, pdf_font_desc *font);
pdf_hmtx pdf_lookup_hmtx(fz_context *ctx, pdf_font_desc *font, int cid);
pdf_vmtx pdf_lookup_vmtx(fz_context *ctx, pdf_font_desc *font, int cid);

void pdf_load_to_unicode(pdf_document *doc, pdf_font_desc *font, char **strings, char *collection, pdf_obj *cmapstm);

int pdf_font_cid_to_gid(fz_context *ctx, pdf_font_desc *fontdesc, int cid);

unsigned char *pdf_lookup_builtin_font(char *name, unsigned int *len);
unsigned char *pdf_lookup_substitute_font(int mono, int serif, int bold, int italic, unsigned int *len);
unsigned char *pdf_lookup_substitute_cjk_font(int ros, int serif, unsigned int *len);

pdf_font_desc *pdf_load_type3_font(pdf_document *doc, pdf_obj *rdb, pdf_obj *obj);
void pdf_load_type3_glyphs(pdf_document *doc, pdf_font_desc *fontdesc, int nestedDepth);
pdf_font_desc *pdf_load_font(pdf_document *doc, pdf_obj *rdb, pdf_obj *obj, int nestedDepth);

pdf_font_desc *pdf_new_font_desc(fz_context *ctx);
pdf_font_desc *pdf_keep_font(fz_context *ctx, pdf_font_desc *fontdesc);
void pdf_drop_font(fz_context *ctx, pdf_font_desc *font);

#ifndef NDEBUG
void pdf_print_font(fz_context *ctx, pdf_font_desc *fontdesc);
#endif

fz_rect *pdf_measure_text(fz_context *ctx, pdf_font_desc *fontdesc, unsigned char *buf, int len, fz_rect *rect);
float pdf_text_stride(fz_context *ctx, pdf_font_desc *fontdesc, float fontsize, unsigned char *buf, int len, float room, int *count);

/*
 * Interactive features
 */

struct pdf_annot_s
{
	pdf_page *page;
	pdf_obj *obj;
	fz_rect rect;
	fz_rect pagerect;
	pdf_xobject *ap;
	int ap_iteration;
	fz_matrix matrix;
	pdf_annot *next;
	pdf_annot *next_changed;
	int type;
};

fz_link_dest pdf_parse_link_dest(pdf_document *doc, pdf_obj *dest);
fz_link_dest pdf_parse_action(pdf_document *doc, pdf_obj *action);
pdf_obj *pdf_lookup_dest(pdf_document *doc, pdf_obj *needle);
pdf_obj *pdf_lookup_name(pdf_document *doc, char *which, pdf_obj *needle);
pdf_obj *pdf_load_name_tree(pdf_document *doc, char *which);

fz_link *pdf_load_link_annots(pdf_document *, pdf_obj *annots, const fz_matrix *page_ctm);

pdf_annot *pdf_load_annots(pdf_document *, pdf_obj *annots, pdf_page *page);
void pdf_update_annot(pdf_document *, pdf_annot *annot);
void pdf_free_annot(fz_context *ctx, pdf_annot *link);

int pdf_field_type(pdf_document *doc, pdf_obj *field);
char *pdf_field_value(pdf_document *doc, pdf_obj *field);
int pdf_field_set_value(pdf_document *doc, pdf_obj *field, char *text);
char *pdf_field_border_style(pdf_document *doc, pdf_obj *field);
void pdf_field_set_border_style(pdf_document *doc, pdf_obj *field, char *text);
void pdf_field_set_button_caption(pdf_document *doc, pdf_obj *field, char *text);
void pdf_field_set_fill_color(pdf_document *doc, pdf_obj *field, pdf_obj *col);
void pdf_field_set_text_color(pdf_document *doc, pdf_obj *field, pdf_obj *col);
int pdf_field_display(pdf_document *doc, pdf_obj *field);
char *pdf_field_name(pdf_document *doc, pdf_obj *field);
void pdf_field_set_display(pdf_document *doc, pdf_obj *field, int d);
pdf_obj *pdf_lookup_field(pdf_obj *form, char *name);
void pdf_field_reset(pdf_document *doc, pdf_obj *field);

/*
 * Page tree, pages and related objects
 */

struct pdf_page_s
{
	fz_matrix ctm; /* calculated from mediabox and rotate */
	fz_rect mediabox;
	int rotate;
	int transparency;
	pdf_obj *resources;
	pdf_obj *contents;
	fz_link *links;
	pdf_annot *annots;
	pdf_annot *changed_annots;
	pdf_obj *me;
	float duration;
	int transition_present;
	fz_transition transition;
};

/*
 * Content stream parsing
 */

void pdf_run_glyph(pdf_document *doc, pdf_obj *resources, fz_buffer *contents, fz_device *dev, const fz_matrix *ctm, void *gstate, int nestedDepth);

/*
 * PDF interface to store
 */
void pdf_store_item(fz_context *ctx, pdf_obj *key, void *val, unsigned int itemsize);
void *pdf_find_item(fz_context *ctx, fz_store_free_fn *free, pdf_obj *key);
void pdf_remove_item(fz_context *ctx, fz_store_free_fn *free, pdf_obj *key);

/*
 * PDF interaction interface
 */
int pdf_has_unsaved_changes(pdf_document *doc);
int pdf_pass_event(pdf_document *doc, pdf_page *page, fz_ui_event *ui_event);
void pdf_update_page(pdf_document *doc, pdf_page *page);
pdf_annot *pdf_poll_changed_annot(pdf_document *idoc, pdf_page *page);
fz_widget *pdf_focused_widget(pdf_document *doc);
fz_widget *pdf_first_widget(pdf_document *doc, pdf_page *page);
fz_widget *pdf_next_widget(fz_widget *previous);
char *pdf_text_widget_text(pdf_document *doc, fz_widget *tw);
int pdf_text_widget_max_len(pdf_document *doc, fz_widget *tw);
int pdf_text_widget_content_type(pdf_document *doc, fz_widget *tw);
int pdf_text_widget_set_text(pdf_document *doc, fz_widget *tw, char *text);
int pdf_choice_widget_options(pdf_document *doc, fz_widget *tw, char *opts[]);
int pdf_choice_widget_is_multiselect(pdf_document *doc, fz_widget *tw);
int pdf_choice_widget_value(pdf_document *doc, fz_widget *tw, char *opts[]);
void pdf_choice_widget_set_value(pdf_document *doc, fz_widget *tw, int n, char *opts[]);
pdf_annot *pdf_create_annot(pdf_document *doc, pdf_page *page, fz_annot_type type);
void pdf_set_annot_appearance(pdf_document *doc, pdf_annot *annot, fz_display_list *disp_list);
void pdf_set_doc_event_callback(pdf_document *doc, fz_doc_event_cb *event_cb, void *data);

void pdf_event_issue_alert(pdf_document *doc, fz_alert_event *event);
void pdf_event_issue_print(pdf_document *doc);
void pdf_event_issue_exec_menu_item(pdf_document *doc, char *item);
void pdf_event_issue_exec_dialog(pdf_document *doc);
void pdf_event_issue_launch_url(pdf_document *doc, char *url, int new_frame);
void pdf_event_issue_mail_doc(pdf_document *doc, fz_mail_doc_event *event);

/*
 * Javascript handler
 */
typedef struct pdf_js_event_s
{
	pdf_obj *target;
	char *value;
	int rc;
} pdf_js_event;

int pdf_js_supported();
pdf_js *pdf_new_js(pdf_document *doc);
void pdf_drop_js(pdf_js *js);
void pdf_js_load_document_level(pdf_js *js);
void pdf_js_setup_event(pdf_js *js, pdf_js_event *e);
pdf_js_event *pdf_js_get_event(pdf_js *js);
void pdf_js_execute(pdf_js *js, char *code);
void pdf_js_execute_count(pdf_js *js, char *code, int count);

/*
 * Javascript engine interface
 */
typedef struct pdf_jsimp_s pdf_jsimp;
typedef struct pdf_jsimp_type_s pdf_jsimp_type;
typedef struct pdf_jsimp_obj_s pdf_jsimp_obj;

typedef void (pdf_jsimp_dtr)(void *jsctx, void *obj);
typedef pdf_jsimp_obj *(pdf_jsimp_method)(void *jsctx, void *obj, int argc, pdf_jsimp_obj *args[]);
typedef pdf_jsimp_obj *(pdf_jsimp_getter)(void *jsctx, void *obj);
typedef void (pdf_jsimp_setter)(void *jsctx, void *obj, pdf_jsimp_obj *val);

enum
{
	JS_TYPE_UNKNOWN,
	JS_TYPE_NULL,
	JS_TYPE_STRING,
	JS_TYPE_NUMBER,
	JS_TYPE_ARRAY,
	JS_TYPE_BOOLEAN
};

pdf_jsimp *pdf_new_jsimp(fz_context *ctx, void *jsctx);
void pdf_drop_jsimp(pdf_jsimp *imp);

pdf_jsimp_type *pdf_jsimp_new_type(pdf_jsimp *imp, pdf_jsimp_dtr *dtr);
void pdf_jsimp_drop_type(pdf_jsimp *imp, pdf_jsimp_type *type);
void pdf_jsimp_addmethod(pdf_jsimp *imp, pdf_jsimp_type *type, char *name, pdf_jsimp_method *meth);
void pdf_jsimp_addproperty(pdf_jsimp *imp, pdf_jsimp_type *type, char *name, pdf_jsimp_getter *get, pdf_jsimp_setter *set);
void pdf_jsimp_set_global_type(pdf_jsimp *imp, pdf_jsimp_type *type);

pdf_jsimp_obj *pdf_jsimp_new_obj(pdf_jsimp *imp, pdf_jsimp_type *type, void *obj);
void pdf_jsimp_drop_obj(pdf_jsimp *imp, pdf_jsimp_obj *obj);

int pdf_jsimp_to_type(pdf_jsimp *imp, pdf_jsimp_obj *obj);

pdf_jsimp_obj *pdf_jsimp_from_string(pdf_jsimp *imp, char *str);
char *pdf_jsimp_to_string(pdf_jsimp *imp, pdf_jsimp_obj *obj);

pdf_jsimp_obj *pdf_jsimp_from_number(pdf_jsimp *imp, double num);
double pdf_jsimp_to_number(pdf_jsimp *imp, pdf_jsimp_obj *obj);

int pdf_jsimp_array_len(pdf_jsimp *imp, pdf_jsimp_obj *obj);
pdf_jsimp_obj *pdf_jsimp_array_item(pdf_jsimp *imp, pdf_jsimp_obj *obj, int i);

pdf_jsimp_obj *pdf_jsimp_property(pdf_jsimp *imp, pdf_jsimp_obj *obj, char *prop);

void pdf_jsimp_execute(pdf_jsimp *imp, char *code);
void pdf_jsimp_execute_count(pdf_jsimp *imp, char *code, int count);

#endif
