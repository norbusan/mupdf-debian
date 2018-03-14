#ifndef PDF_INTERPRET_H
#define PDF_INTERPRET_H

#include "mupdf/pdf/font.h"
#include "mupdf/pdf/resource.h"

typedef struct pdf_csi_s pdf_csi;
typedef struct pdf_gstate_s pdf_gstate;
typedef struct pdf_processor_s pdf_processor;

void *pdf_new_processor(fz_context *ctx, int size);
void pdf_close_processor(fz_context *ctx, pdf_processor *proc);
void pdf_drop_processor(fz_context *ctx, pdf_processor *proc);

struct pdf_processor_s
{
	void (*close_processor)(fz_context *ctx, pdf_processor *proc);
	void (*drop_processor)(fz_context *ctx, pdf_processor *proc);

	/* general graphics state */
	void (*op_w)(fz_context *ctx, pdf_processor *proc, float linewidth);
	void (*op_j)(fz_context *ctx, pdf_processor *proc, int linejoin);
	void (*op_J)(fz_context *ctx, pdf_processor *proc, int linecap);
	void (*op_M)(fz_context *ctx, pdf_processor *proc, float miterlimit);
	void (*op_d)(fz_context *ctx, pdf_processor *proc, pdf_obj *array, float phase);
	void (*op_ri)(fz_context *ctx, pdf_processor *proc, const char *intent);
	void (*op_i)(fz_context *ctx, pdf_processor *proc, float flatness);

	void (*op_gs_begin)(fz_context *ctx, pdf_processor *proc, const char *name, pdf_obj *extgstate);
	void (*op_gs_BM)(fz_context *ctx, pdf_processor *proc, const char *blendmode);
	void (*op_gs_ca)(fz_context *ctx, pdf_processor *proc, float alpha);
	void (*op_gs_CA)(fz_context *ctx, pdf_processor *proc, float alpha);
	void (*op_gs_SMask)(fz_context *ctx, pdf_processor *proc, pdf_xobject *smask, pdf_obj *page_resources, float *bc, int luminosity);
	void (*op_gs_end)(fz_context *ctx, pdf_processor *proc);

	/* special graphics state */
	void (*op_q)(fz_context *ctx, pdf_processor *proc);
	void (*op_Q)(fz_context *ctx, pdf_processor *proc);
	void (*op_cm)(fz_context *ctx, pdf_processor *proc, float a, float b, float c, float d, float e, float f);

	/* path construction */
	void (*op_m)(fz_context *ctx, pdf_processor *proc, float x, float y);
	void (*op_l)(fz_context *ctx, pdf_processor *proc, float x, float y);
	void (*op_c)(fz_context *ctx, pdf_processor *proc, float x1, float y1, float x2, float y2, float x3, float y3);
	void (*op_v)(fz_context *ctx, pdf_processor *proc, float x2, float y2, float x3, float y3);
	void (*op_y)(fz_context *ctx, pdf_processor *proc, float x1, float y1, float x3, float y3);
	void (*op_h)(fz_context *ctx, pdf_processor *proc);
	void (*op_re)(fz_context *ctx, pdf_processor *proc, float x, float y, float w, float h);

	/* path painting */
	void (*op_S)(fz_context *ctx, pdf_processor *proc);
	void (*op_s)(fz_context *ctx, pdf_processor *proc);
	void (*op_F)(fz_context *ctx, pdf_processor *proc);
	void (*op_f)(fz_context *ctx, pdf_processor *proc);
	void (*op_fstar)(fz_context *ctx, pdf_processor *proc);
	void (*op_B)(fz_context *ctx, pdf_processor *proc);
	void (*op_Bstar)(fz_context *ctx, pdf_processor *proc);
	void (*op_b)(fz_context *ctx, pdf_processor *proc);
	void (*op_bstar)(fz_context *ctx, pdf_processor *proc);
	void (*op_n)(fz_context *ctx, pdf_processor *proc);

	/* clipping paths */
	void (*op_W)(fz_context *ctx, pdf_processor *proc);
	void (*op_Wstar)(fz_context *ctx, pdf_processor *proc);

	/* text objects */
	void (*op_BT)(fz_context *ctx, pdf_processor *proc);
	void (*op_ET)(fz_context *ctx, pdf_processor *proc);

	/* text state */
	void (*op_Tc)(fz_context *ctx, pdf_processor *proc, float charspace);
	void (*op_Tw)(fz_context *ctx, pdf_processor *proc, float wordspace);
	void (*op_Tz)(fz_context *ctx, pdf_processor *proc, float scale);
	void (*op_TL)(fz_context *ctx, pdf_processor *proc, float leading);
	void (*op_Tf)(fz_context *ctx, pdf_processor *proc, const char *name, pdf_font_desc *font, float size);
	void (*op_Tr)(fz_context *ctx, pdf_processor *proc, int render);
	void (*op_Ts)(fz_context *ctx, pdf_processor *proc, float rise);

	/* text positioning */
	void (*op_Td)(fz_context *ctx, pdf_processor *proc, float tx, float ty);
	void (*op_TD)(fz_context *ctx, pdf_processor *proc, float tx, float ty);
	void (*op_Tm)(fz_context *ctx, pdf_processor *proc, float a, float b, float c, float d, float e, float f);
	void (*op_Tstar)(fz_context *ctx, pdf_processor *proc);

	/* text showing */
	void (*op_TJ)(fz_context *ctx, pdf_processor *proc, pdf_obj *array);
	void (*op_Tj)(fz_context *ctx, pdf_processor *proc, char *str, int len);
	void (*op_squote)(fz_context *ctx, pdf_processor *proc, char *str, int len);
	void (*op_dquote)(fz_context *ctx, pdf_processor *proc, float aw, float ac, char *str, int len);

	/* type 3 fonts */
	void (*op_d0)(fz_context *ctx, pdf_processor *proc, float wx, float wy);
	void (*op_d1)(fz_context *ctx, pdf_processor *proc, float wx, float wy, float llx, float lly, float urx, float ury);

	/* color */
	void (*op_CS)(fz_context *ctx, pdf_processor *proc, const char *name, fz_colorspace *cs);
	void (*op_cs)(fz_context *ctx, pdf_processor *proc, const char *name, fz_colorspace *cs);
	void (*op_SC_pattern)(fz_context *ctx, pdf_processor *proc, const char *name, pdf_pattern *pat, int n, float *color);
	void (*op_sc_pattern)(fz_context *ctx, pdf_processor *proc, const char *name, pdf_pattern *pat, int n, float *color);
	void (*op_SC_shade)(fz_context *ctx, pdf_processor *proc, const char *name, fz_shade *shade);
	void (*op_sc_shade)(fz_context *ctx, pdf_processor *proc, const char *name, fz_shade *shade);
	void (*op_SC_color)(fz_context *ctx, pdf_processor *proc, int n, float *color);
	void (*op_sc_color)(fz_context *ctx, pdf_processor *proc, int n, float *color);

	void (*op_G)(fz_context *ctx, pdf_processor *proc, float g);
	void (*op_g)(fz_context *ctx, pdf_processor *proc, float g);
	void (*op_RG)(fz_context *ctx, pdf_processor *proc, float r, float g, float b);
	void (*op_rg)(fz_context *ctx, pdf_processor *proc, float r, float g, float b);
	void (*op_K)(fz_context *ctx, pdf_processor *proc, float c, float m, float y, float k);
	void (*op_k)(fz_context *ctx, pdf_processor *proc, float c, float m, float y, float k);

	/* shadings, images, xobjects */
	void (*op_BI)(fz_context *ctx, pdf_processor *proc, fz_image *image);
	void (*op_sh)(fz_context *ctx, pdf_processor *proc, const char *name, fz_shade *shade);
	void (*op_Do_image)(fz_context *ctx, pdf_processor *proc, const char *name, fz_image *image);
	void (*op_Do_form)(fz_context *ctx, pdf_processor *proc, const char *name, pdf_xobject *form, pdf_obj *page_resources);

	/* marked content */
	void (*op_MP)(fz_context *ctx, pdf_processor *proc, const char *tag);
	void (*op_DP)(fz_context *ctx, pdf_processor *proc, const char *tag, pdf_obj *raw, pdf_obj *cooked);
	void (*op_BMC)(fz_context *ctx, pdf_processor *proc, const char *tag);
	void (*op_BDC)(fz_context *ctx, pdf_processor *proc, const char *tag, pdf_obj *raw, pdf_obj *cooked);
	void (*op_EMC)(fz_context *ctx, pdf_processor *proc);

	/* compatibility */
	void (*op_BX)(fz_context *ctx, pdf_processor *proc);
	void (*op_EX)(fz_context *ctx, pdf_processor *proc);

	/* Virtual ops for ExtGState entries */
	void (*op_gs_OP)(fz_context *ctx, pdf_processor *proc, int b);
	void (*op_gs_op)(fz_context *ctx, pdf_processor *proc, int b);
	void (*op_gs_OPM)(fz_context *ctx, pdf_processor *proc, int i);
	void (*op_gs_UseBlackPtComp)(fz_context *ctx, pdf_processor *proc, pdf_obj *name);

	/* END is used to signify end of stream (finalise and close down) */
	void (*op_END)(fz_context *ctx, pdf_processor *proc);

	/* interpreter state that persists across content streams */
	const char *usage;
	int hidden;
};

struct pdf_csi_s
{
	/* input */
	pdf_document *doc;
	pdf_obj *rdb;
	pdf_lexbuf *buf;
	fz_cookie *cookie;

	/* state */
	int gstate;
	int xbalance;
	int in_text;
	fz_rect d1_rect;

	/* stack */
	pdf_obj *obj;
	char name[256];
	char string[256];
	int string_len;
	int top;
	float stack[32];
};

/* Functions to set up pdf_process structures */

/*
	pdf_new_run_processor: Create a new "run" processor. This maps
	from PDF operators to fz_device level calls.

	dev: The device to which the resulting device calls are to be
	sent.

	ctm: The initial transformation matrix to use.

	usage: A NULL terminated string that describes the 'usage' of
	this interpretation. Typically 'View', though 'Print' is also
	defined within the PDF reference manual, and others are possible.

	gstate: The initial graphics state.

	nested: The nested depth of this interpreter. This should be
	0 for an initial call, and will be incremented in nested calls
	due to Type 3 fonts.
*/
pdf_processor *pdf_new_run_processor(fz_context *ctx, fz_device *dev, const fz_matrix *ctm, const char *usage, pdf_gstate *gstate, int nested, fz_default_colorspaces *default_cs);

/*
	pdf_new_buffer_processor: Create a buffer processor. This
	collects the incoming PDF operator stream into an fz_buffer.

	buffer: The (possibly empty) buffer to which operators will be
	appended.

	ahxencode: If 0, then image streams will be send as binary,
	otherwise they will be asciihexencoded.
*/
pdf_processor *pdf_new_buffer_processor(fz_context *ctx, fz_buffer *buffer, int ahxencode);

/*
	pdf_new_output_processor: Create an output processor. This
	sends the incoming PDF operator stream to an fz_output stream.

	out: The output stream to which operators will be sent.

	ahxencode: If 0, then image streams will be send as binary,
	otherwise they will be asciihexencoded.
*/
pdf_processor *pdf_new_output_processor(fz_context *ctx, fz_output *out, int ahxencode);

/*
	pdf_new_filter_processor: Create a filter processor. This
	filters the PDF operators it is fed, and passes them down
	(with some changes) to the child filter.

	The changes made by the filter are:

	* No operations are allowed to change the top level gstate.
	Additional q/Q operators are inserted to prevent this.

	* Repeated/unnecessary colour operators are removed (so,
	for example, "0 0 0 rg 0 1 rg 0.5 g" would be sanitised to
	"0.5 g")

	The intention of these changes is to provide a simpler,
	but equivalent stream, repairing problems with mismatched
	operators, maintaining structure (such as BMC, EMC calls)
	and leaving the graphics state in an known (default) state
	so that subsequent operations (such as synthesising new
	operators to be appended to the stream) are easier.

	The net graphical effect of the filtered operator stream
	should be identical to the incoming operator stream.

	chain: The child processor to which the filtered operators
	will be fed.

	old_res: The incoming resource dictionary.

	new_res: An (initially empty) resource dictionary that will
	be populated by copying entries from the old dictionary to
	the new one as they are used. At the end therefore, this
	contains exactly those resource objects actually required.

*/
pdf_processor *pdf_new_filter_processor(fz_context *ctx, pdf_document *doc, pdf_processor *chain, pdf_obj *old_res, pdf_obj *new_res);

typedef int (pdf_text_filter_fn)(fz_context *ctx, void *opaque, int *ucsbuf, int ucslen, fz_matrix *trm, fz_rect *bbox);

typedef void (pdf_after_text_object_fn)(fz_context *ctx, void *opaque, pdf_document *doc, pdf_processor *chain, const fz_matrix *ctm);

/*
	pdf_new_filter_processor_with_text_filter: Create a filter
	processor with a filter function for text. This filters the
	PDF operators it is fed, and passes them down (with some
	changes) to the child filter.

	See pdf_new_filter_processor for documentation.

	text_filter: A function called to assess whether a given
	character should be removed or not.

	after_text_object: A function to be called after each text object.
	This allows the caller to insert some extra content if
	required.

	text_filter_opaque: Opaque value to be passed to the
	text_filter function.
*/
pdf_processor *
pdf_new_filter_processor_with_text_filter(fz_context *ctx, pdf_document *doc, pdf_processor *chain, pdf_obj *old_rdb, pdf_obj *new_rdb, pdf_text_filter_fn *text_filter, pdf_after_text_object_fn *after, void *text_filter_opaque);

/* Functions to actually process annotations, glyphs and general stream objects */
void pdf_process_contents(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_obj *obj, pdf_obj *res, fz_cookie *cookie);
void pdf_process_annot(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_page *page, pdf_annot *annot, fz_cookie *cookie);
void pdf_process_glyph(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_obj *resources, fz_buffer *contents);

/* Text handling helper functions */
typedef struct pdf_text_state_s
{
	float char_space;
	float word_space;
	float scale;
	float leading;
	pdf_font_desc *font;
	float size;
	int render;
	float rise;
} pdf_text_state;

typedef struct pdf_text_object_state_s
{
	fz_text *text;
	fz_rect text_bbox;
	fz_matrix tlm;
	fz_matrix tm;
	int text_mode;

	int cid;
	int gid;
	fz_rect char_bbox;
	pdf_font_desc *fontdesc;
	float char_tx;
	float char_ty;
} pdf_text_object_state;

void pdf_tos_save(fz_context *ctx, pdf_text_object_state *tos, fz_matrix save[2]);
void pdf_tos_restore(fz_context *ctx, pdf_text_object_state *tos, fz_matrix save[2]);
fz_text *pdf_tos_get_text(fz_context *ctx, pdf_text_object_state *tos);
void pdf_tos_reset(fz_context *ctx, pdf_text_object_state *tos, int render);
int pdf_tos_make_trm(fz_context *ctx, pdf_text_object_state *tos, pdf_text_state *text, pdf_font_desc *fontdesc, int cid, fz_matrix *trm);
void pdf_tos_move_after_char(fz_context *ctx, pdf_text_object_state *tos);
void pdf_tos_translate(pdf_text_object_state *tos, float tx, float ty);
void pdf_tos_set_matrix(pdf_text_object_state *tos, float a, float b, float c, float d, float e, float f);
void pdf_tos_newline(pdf_text_object_state *tos, float leading);

#endif
