#ifndef PDF_INTERPRET_H
#define PDF_INTERPRET_H

typedef struct pdf_csi_s pdf_csi;
typedef struct pdf_gstate_s pdf_gstate;
typedef struct pdf_processor_s pdf_processor;

void *pdf_new_processor(fz_context *ctx, int size);
void pdf_drop_processor(fz_context *ctx, pdf_processor *proc);

struct pdf_processor_s
{
	void (*drop_imp)(fz_context *ctx, pdf_processor *proc);

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
	void (*op_DP)(fz_context *ctx, pdf_processor *proc, const char *tag, pdf_obj *properties);
	void (*op_BMC)(fz_context *ctx, pdf_processor *proc, const char *tag);
	void (*op_BDC)(fz_context *ctx, pdf_processor *proc, const char *tag, pdf_obj *properties);
	void (*op_EMC)(fz_context *ctx, pdf_processor *proc);

	/* compatibility */
	void (*op_BX)(fz_context *ctx, pdf_processor *proc);
	void (*op_EX)(fz_context *ctx, pdf_processor *proc);

	/* END is used to signify end of stream (finalise and close down) */
	void (*op_END)(fz_context *ctx, pdf_processor *proc);

	/* interpreter state that persists across content streams */
	const char *event;
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
pdf_processor *pdf_new_run_processor(fz_context *ctx, fz_device *dev, const fz_matrix *ctm, const char *event, pdf_gstate *gstate, int nested);
pdf_processor *pdf_new_buffer_processor(fz_context *ctx, fz_buffer *buffer, int ahxencode);
pdf_processor *pdf_new_filter_processor(fz_context *ctx, pdf_processor *chain, pdf_document *doc, pdf_obj *old_res, pdf_obj *new_res);

/* Functions to actually process annotations, glyphs and general stream objects */
void pdf_process_contents(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_obj *obj, pdf_obj *res, fz_cookie *cookie);
void pdf_process_annot(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_page *page, pdf_annot *annot, fz_cookie *cookie);
void pdf_process_glyph(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_obj *resources, fz_buffer *contents);

#endif
