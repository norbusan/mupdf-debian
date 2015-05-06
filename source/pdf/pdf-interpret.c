#include "mupdf/pdf.h"

void *
pdf_new_processor(fz_context *ctx, int size)
{
	return Memento_label(fz_calloc(ctx, 1, size), "pdf_processor");
}

void
pdf_drop_processor(fz_context *ctx, pdf_processor *proc)
{
	if (proc && proc->drop_imp)
		proc->drop_imp(ctx, proc);
	fz_free(ctx, proc);
}

static void
pdf_init_csi(fz_context *ctx, pdf_csi *csi, pdf_document *doc, pdf_obj *rdb, pdf_lexbuf *buf, fz_cookie *cookie)
{
	memset(csi, 0, sizeof *csi);
	csi->doc = doc;
	csi->rdb = rdb;
	csi->buf = buf;
	csi->cookie = cookie;
}

static void
pdf_clear_stack(fz_context *ctx, pdf_csi *csi)
{
	int i;

	pdf_drop_obj(ctx, csi->obj);
	csi->obj = NULL;

	csi->name[0] = 0;
	csi->string_len = 0;
	for (i = 0; i < csi->top; i++)
		csi->stack[i] = 0;

	csi->top = 0;
}

static pdf_font_desc *
load_font_or_hail_mary(fz_context *ctx, pdf_document *doc, pdf_obj *rdb, pdf_obj *font, int depth, fz_cookie *cookie)
{
	pdf_font_desc *desc;

	fz_try(ctx)
	{
		desc = pdf_load_font(ctx, doc, rdb, font, depth);
	}
	fz_catch(ctx)
	{
		if (fz_caught(ctx) == FZ_ERROR_TRYLATER && cookie && cookie->incomplete_ok)
		{
			desc = NULL;
			cookie->incomplete++;
		}
		else
		{
			fz_rethrow(ctx);
		}
	}
	if (desc == NULL)
		desc = pdf_load_hail_mary_font(ctx, doc);
	return desc;
}

static int
ocg_intents_include(fz_context *ctx, pdf_ocg_descriptor *desc, char *name)
{
	int i, len;

	if (strcmp(name, "All") == 0)
		return 1;

	/* In the absence of a specified intent, it's 'View' */
	if (!desc->intent)
		return (strcmp(name, "View") == 0);

	if (pdf_is_name(ctx, desc->intent))
	{
		char *intent = pdf_to_name(ctx, desc->intent);
		if (strcmp(intent, "All") == 0)
			return 1;
		return (strcmp(intent, name) == 0);
	}
	if (!pdf_is_array(ctx, desc->intent))
		return 0;

	len = pdf_array_len(ctx, desc->intent);
	for (i=0; i < len; i++)
	{
		char *intent = pdf_to_name(ctx, pdf_array_get(ctx, desc->intent, i));
		if (strcmp(intent, "All") == 0)
			return 1;
		if (strcmp(intent, name) == 0)
			return 1;
	}
	return 0;
}

static int
pdf_is_hidden_ocg(fz_context *ctx, pdf_ocg_descriptor *desc, pdf_obj *rdb, const char *event, pdf_obj *ocg)
{
	char event_state[16];
	pdf_obj *obj, *obj2, *type;

	/* Avoid infinite recursions */
	if (pdf_obj_marked(ctx, ocg))
		return 0;

	/* If no event, everything is visible */
	if (!event)
		return 0;

	/* If no ocg descriptor, everything is visible */
	if (!desc)
		return 0;

	/* If we've been handed a name, look it up in the properties. */
	if (pdf_is_name(ctx, ocg))
	{
		ocg = pdf_dict_get(ctx, pdf_dict_get(ctx, rdb, PDF_NAME_Properties), ocg);
	}
	/* If we haven't been given an ocg at all, then we're visible */
	if (!ocg)
		return 0;

	fz_strlcpy(event_state, event, sizeof event_state);
	fz_strlcat(event_state, "State", sizeof event_state);

	type = pdf_dict_get(ctx, ocg, PDF_NAME_Type);

	if (pdf_name_eq(ctx, type, PDF_NAME_OCG))
	{
		/* An Optional Content Group */
		int default_value = 0;
		int num = pdf_to_num(ctx, ocg);
		int gen = pdf_to_gen(ctx, ocg);
		int len = desc->len;
		int i;
		pdf_obj *es;

		/* by default an OCG is visible, unless it's explicitly hidden */
		for (i = 0; i < len; i++)
		{
			if (desc->ocgs[i].num == num && desc->ocgs[i].gen == gen)
			{
				default_value = desc->ocgs[i].state == 0;
				break;
			}
		}

		/* Check Intents; if our intent is not part of the set given
		 * by the current config, we should ignore it. */
		obj = pdf_dict_get(ctx, ocg, PDF_NAME_Intent);
		if (pdf_is_name(ctx, obj))
		{
			/* If it doesn't match, it's hidden */
			if (ocg_intents_include(ctx, desc, pdf_to_name(ctx, obj)) == 0)
				return 1;
		}
		else if (pdf_is_array(ctx, obj))
		{
			int match = 0;
			len = pdf_array_len(ctx, obj);
			for (i=0; i<len; i++) {
				match |= ocg_intents_include(ctx, desc, pdf_to_name(ctx, pdf_array_get(ctx, obj, i)));
				if (match)
					break;
			}
			/* If we don't match any, it's hidden */
			if (match == 0)
				return 1;
		}
		else
		{
			/* If it doesn't match, it's hidden */
			if (ocg_intents_include(ctx, desc, "View") == 0)
				return 1;
		}

		/* FIXME: Currently we do a very simple check whereby we look
		 * at the Usage object (an Optional Content Usage Dictionary)
		 * and check to see if the corresponding 'event' key is on
		 * or off.
		 *
		 * Really we should only look at Usage dictionaries that
		 * correspond to entries in the AS list in the OCG config.
		 * Given that we don't handle Zoom or User, or Language
		 * dicts, this is not really a problem. */
		obj = pdf_dict_get(ctx, ocg, PDF_NAME_Usage);
		if (!pdf_is_dict(ctx, obj))
			return default_value;
		/* FIXME: Should look at Zoom (and return hidden if out of
		 * max/min range) */
		/* FIXME: Could provide hooks to the caller to check if
		 * User is appropriate - if not return hidden. */
		obj2 = pdf_dict_gets(ctx, obj, event);
		es = pdf_dict_gets(ctx, obj2, event_state);
		if (pdf_name_eq(ctx, es, PDF_NAME_OFF))
		{
			return 1;
		}
		if (pdf_name_eq(ctx, es, PDF_NAME_ON))
		{
			return 0;
		}
		return default_value;
	}
	else if (pdf_name_eq(ctx, type, PDF_NAME_OCMD))
	{
		/* An Optional Content Membership Dictionary */
		pdf_obj *name;
		int combine, on;

		obj = pdf_dict_get(ctx, ocg, PDF_NAME_VE);
		if (pdf_is_array(ctx, obj)) {
			/* FIXME: Calculate visibility from array */
			return 0;
		}
		name = pdf_dict_get(ctx, ocg, PDF_NAME_P);
		/* Set combine; Bit 0 set => AND, Bit 1 set => true means
		 * Off, otherwise true means On */
		if (pdf_name_eq(ctx, name, PDF_NAME_AllOn))
		{
			combine = 1;
		}
		else if (pdf_name_eq(ctx, name, PDF_NAME_AnyOff))
		{
			combine = 2;
		}
		else if (pdf_name_eq(ctx, name, PDF_NAME_AllOff))
		{
			combine = 3;
		}
		else /* Assume it's the default (AnyOn) */
		{
			combine = 0;
		}

		if (pdf_mark_obj(ctx, ocg))
			return 0; /* Should never happen */
		fz_try(ctx)
		{
			obj = pdf_dict_get(ctx, ocg, PDF_NAME_OCGs);
			on = combine & 1;
			if (pdf_is_array(ctx, obj)) {
				int i, len;
				len = pdf_array_len(ctx, obj);
				for (i = 0; i < len; i++)
				{
					int hidden = pdf_is_hidden_ocg(ctx, desc, rdb, event, pdf_array_get(ctx, obj, i));
					if ((combine & 1) == 0)
						hidden = !hidden;
					if (combine & 2)
						on &= hidden;
					else
						on |= hidden;
				}
			}
			else
			{
				on = pdf_is_hidden_ocg(ctx, desc, rdb, event, obj);
				if ((combine & 1) == 0)
					on = !on;
			}
		}
		fz_always(ctx)
		{
			pdf_unmark_obj(ctx, ocg);
		}
		fz_catch(ctx)
		{
			fz_rethrow(ctx);
		}
		return !on;
	}
	/* No idea what sort of object this is - be visible */
	return 0;
}

static fz_image *
parse_inline_image(fz_context *ctx, pdf_csi *csi, fz_stream *stm)
{
	pdf_document *doc = csi->doc;
	pdf_obj *rdb = csi->rdb;
	pdf_obj *obj = NULL;
	fz_image *img = NULL;
	int ch, found;

	fz_var(obj);
	fz_var(img);

	fz_try(ctx)
	{
		obj = pdf_parse_dict(ctx, doc, stm, &doc->lexbuf.base);

		/* read whitespace after ID keyword */
		ch = fz_read_byte(ctx, stm);
		if (ch == '\r')
			if (fz_peek_byte(ctx, stm) == '\n')
				fz_read_byte(ctx, stm);

		img = pdf_load_inline_image(ctx, doc, rdb, obj, stm);

		/* find EI */
		found = 0;
		ch = fz_read_byte(ctx, stm);
		do
		{
			while (ch != 'E' && ch != EOF)
				ch = fz_read_byte(ctx, stm);
			if (ch == 'E')
			{
				ch = fz_read_byte(ctx, stm);
				if (ch == 'I')
				{
					ch = fz_peek_byte(ctx, stm);
					if (ch == ' ' || ch <= 32 || ch == EOF || ch == '<' || ch == '/')
					{
						found = 1;
						break;
					}
				}
			}
		} while (ch != EOF);
		if (!found)
			fz_throw(ctx, FZ_ERROR_GENERIC, "syntax error after inline image");
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(ctx, obj);
		fz_drop_image(ctx, img);
		fz_rethrow(ctx);
	}

	return img;
}

static void
pdf_process_extgstate(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, pdf_obj *dict)
{
	pdf_obj *obj;

	obj = pdf_dict_get(ctx, dict, PDF_NAME_LW);
	if (pdf_is_number(ctx, obj) && proc->op_w)
		proc->op_w(ctx, proc, pdf_to_real(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_LC);
	if (pdf_is_int(ctx, obj) && proc->op_J)
		proc->op_J(ctx, proc, pdf_to_int(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_LJ);
	if (pdf_is_int(ctx, obj) && proc->op_j)
		proc->op_j(ctx, proc, pdf_to_int(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_ML);
	if (pdf_is_number(ctx, obj) && proc->op_M)
		proc->op_M(ctx, proc, pdf_to_real(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_D);
	if (pdf_is_array(ctx, obj) && proc->op_d)
	{
		pdf_obj *dash_array = pdf_array_get(ctx, obj, 0);
		pdf_obj *dash_phase = pdf_array_get(ctx, obj, 1);
		proc->op_d(ctx, proc, dash_array, pdf_to_real(ctx, dash_phase));
	}

	obj = pdf_dict_get(ctx, dict, PDF_NAME_RI);
	if (pdf_is_name(ctx, obj) && proc->op_ri)
		proc->op_ri(ctx, proc, pdf_to_name(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_FL);
	if (pdf_is_number(ctx, obj) && proc->op_i)
		proc->op_i(ctx, proc, pdf_to_real(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_Font);
	if (pdf_is_array(ctx, obj) && proc->op_Tf)
	{
		pdf_obj *font_ref = pdf_array_get(ctx, obj, 0);
		pdf_obj *font_size = pdf_array_get(ctx, obj, 1);
		pdf_font_desc *font = load_font_or_hail_mary(ctx, csi->doc, csi->rdb, font_ref, 0, csi->cookie);
		fz_try(ctx)
			proc->op_Tf(ctx, proc, "ExtGState", font, pdf_to_real(ctx, font_size));
		fz_always(ctx)
			pdf_drop_font(ctx, font);
		fz_catch(ctx)
			fz_rethrow(ctx);
	}

	/* transfer functions */

	obj = pdf_dict_get(ctx, dict, PDF_NAME_TR2);
	if (pdf_is_name(ctx, obj))
		if (!pdf_name_eq(ctx, obj, PDF_NAME_Identity) && !pdf_name_eq(ctx, obj, PDF_NAME_Default))
			fz_warn(ctx, "ignoring transfer function");
	if (!obj) /* TR is ignored in the presence of TR2 */
	{
		pdf_obj *tr = pdf_dict_get(ctx, dict, PDF_NAME_TR);
		if (pdf_is_name(ctx, tr))
			if (!pdf_name_eq(ctx, tr, PDF_NAME_Identity))
				fz_warn(ctx, "ignoring transfer function");
	}

	/* transparency state */

	obj = pdf_dict_get(ctx, dict, PDF_NAME_CA);
	if (pdf_is_number(ctx, obj) && proc->op_gs_CA)
		proc->op_gs_CA(ctx, proc, pdf_to_real(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_ca);
	if (pdf_is_number(ctx, obj) && proc->op_gs_ca)
		proc->op_gs_ca(ctx, proc, pdf_to_real(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_BM);
	if (pdf_is_array(ctx, obj))
		obj = pdf_array_get(ctx, obj, 0);
	if (pdf_is_name(ctx, obj) && proc->op_gs_BM)
		proc->op_gs_BM(ctx, proc, pdf_to_name(ctx, obj));

	obj = pdf_dict_get(ctx, dict, PDF_NAME_SMask);
	if (proc->op_gs_SMask)
	{
		if (pdf_is_dict(ctx, obj))
		{
			pdf_xobject *xobj;
			pdf_obj *group, *s, *bc, *tr;
			float softmask_bc[FZ_MAX_COLORS];
			fz_colorspace *colorspace;
			int k, luminosity;

			fz_var(xobj);

			group = pdf_dict_get(ctx, obj, PDF_NAME_G);
			if (!group)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot load softmask xobject (%d %d R)", pdf_to_num(ctx, obj), pdf_to_gen(ctx, obj));
			xobj = pdf_load_xobject(ctx, csi->doc, group);

			fz_try(ctx)
			{
				colorspace = xobj->colorspace;
				if (!colorspace)
					colorspace = fz_device_gray(ctx);

				for (k = 0; k < colorspace->n; k++)
					softmask_bc[k] = 0;

				bc = pdf_dict_get(ctx, obj, PDF_NAME_BC);
				if (pdf_is_array(ctx, bc))
				{
					for (k = 0; k < colorspace->n; k++)
						softmask_bc[k] = pdf_to_real(ctx, pdf_array_get(ctx, bc, k));
				}

				s = pdf_dict_get(ctx, obj, PDF_NAME_S);
				if (pdf_name_eq(ctx, s, PDF_NAME_Luminosity))
					luminosity = 1;
				else
					luminosity = 0;

				tr = pdf_dict_get(ctx, obj, PDF_NAME_TR);
				if (tr && !pdf_name_eq(ctx, tr, PDF_NAME_Identity))
					fz_warn(ctx, "ignoring transfer function");

				proc->op_gs_SMask(ctx, proc, xobj, csi->rdb, softmask_bc, luminosity);
			}
			fz_always(ctx)
			{
				pdf_drop_xobject(ctx, xobj);
			}
			fz_catch(ctx)
			{
				fz_rethrow(ctx);
			}
		}
		else if (pdf_is_name(ctx, obj) && pdf_name_eq(ctx, obj, PDF_NAME_None))
		{
			proc->op_gs_SMask(ctx, proc, NULL, NULL, NULL, 0);
		}
	}
}

static void
pdf_process_Do(fz_context *ctx, pdf_processor *proc, pdf_csi *csi)
{
	pdf_obj *xres, *xobj, *subtype;

	xres = pdf_dict_get(ctx, csi->rdb, PDF_NAME_XObject);
	if (!xres)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find XObject dictionary");
	xobj = pdf_dict_gets(ctx, xres, csi->name);
	if (!xobj)
		fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find XObject resource '%s'", csi->name);
	subtype = pdf_dict_get(ctx, xobj, PDF_NAME_Subtype);
	if (pdf_name_eq(ctx, subtype, PDF_NAME_Form))
	{
		pdf_obj *st = pdf_dict_get(ctx, xobj, PDF_NAME_Subtype2);
		if (st)
			subtype = st;
	}
	if (!pdf_is_name(ctx, subtype))
		fz_throw(ctx, FZ_ERROR_GENERIC, "no XObject subtype specified");

	if (pdf_is_hidden_ocg(ctx, csi->doc->ocg, csi->rdb, proc->event, pdf_dict_get(ctx, xobj, PDF_NAME_OC)))
		return;

	if (pdf_name_eq(ctx, subtype, PDF_NAME_Form))
	{
		if (proc->op_Do_form)
		{
			pdf_xobject *form = pdf_load_xobject(ctx, csi->doc, xobj);

			fz_try(ctx)
				proc->op_Do_form(ctx, proc, csi->name, form, csi->rdb);
			fz_always(ctx)
				pdf_drop_xobject(ctx, form);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}
	}

	else if (pdf_name_eq(ctx, subtype, PDF_NAME_Image))
	{
		if (proc->op_Do_image)
		{
			fz_image *image = pdf_load_image(ctx, csi->doc, xobj);
			fz_try(ctx)
				proc->op_Do_image(ctx, proc, csi->name, image);
			fz_always(ctx)
				fz_drop_image(ctx, image);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}
	}

	else if (!strcmp(pdf_to_name(ctx, subtype), "PS"))
		fz_warn(ctx, "ignoring XObject with subtype PS");
	else
		fz_warn(ctx, "ignoring XObject with unknown subtype: '%s'", pdf_to_name(ctx, subtype));
}

static void
pdf_process_CS(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, int stroke)
{
	if (!proc->op_CS || !proc->op_cs)
		return;

	if (!strcmp(csi->name, "Pattern"))
	{
		if (stroke)
			proc->op_CS(ctx, proc, "Pattern", NULL);
		else
			proc->op_cs(ctx, proc, "Pattern", NULL);
	}
	else
	{
		fz_colorspace *cs;

		if (!strcmp(csi->name, "DeviceGray"))
			cs = fz_keep_colorspace(ctx, fz_device_gray(ctx));
		else if (!strcmp(csi->name, "DeviceRGB"))
			cs = fz_keep_colorspace(ctx, fz_device_rgb(ctx));
		else if (!strcmp(csi->name, "DeviceCMYK"))
			cs = fz_keep_colorspace(ctx, fz_device_cmyk(ctx));
		else
		{
			pdf_obj *csres, *csobj;
			csres = pdf_dict_get(ctx, csi->rdb, PDF_NAME_ColorSpace);
			if (!csres)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find ColorSpace dictionary");
			csobj = pdf_dict_gets(ctx, csres, csi->name);
			if (!csobj)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find ColorSpace resource '%s'", csi->name);
			cs = pdf_load_colorspace(ctx, csi->doc, csobj);
		}

		fz_try(ctx)
		{
			if (stroke)
				proc->op_CS(ctx, proc, csi->name, cs);
			else
				proc->op_cs(ctx, proc, csi->name, cs);
		}
		fz_always(ctx)
			fz_drop_colorspace(ctx, cs);
		fz_catch(ctx)
			fz_rethrow(ctx);
	}
}

static void
pdf_process_SC(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, int stroke)
{
	if (csi->name[0])
	{
		pdf_obj *patres, *patobj, *type;

		patres = pdf_dict_get(ctx, csi->rdb, PDF_NAME_Pattern);
		if (!patres)
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find Pattern dictionary");
		patobj = pdf_dict_gets(ctx, patres, csi->name);
		if (!patobj)
			fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find Pattern resource '%s'", csi->name);

		type = pdf_dict_get(ctx, patobj, PDF_NAME_PatternType);

		if (pdf_to_int(ctx, type) == 1)
		{
			if (proc->op_SC_pattern && proc->op_sc_pattern)
			{
				pdf_pattern *pat = pdf_load_pattern(ctx, csi->doc, patobj);
				fz_try(ctx)
				{
					if (stroke)
						proc->op_SC_pattern(ctx, proc, csi->name, pat, csi->top, csi->stack);
					else
						proc->op_sc_pattern(ctx, proc, csi->name, pat, csi->top, csi->stack);
				}
				fz_always(ctx)
					pdf_drop_pattern(ctx, pat);
				fz_catch(ctx)
					fz_rethrow(ctx);
			}
		}

		else if (pdf_to_int(ctx, type) == 2)
		{
			if (proc->op_SC_shade && proc->op_sc_shade)
			{
				fz_shade *shade = pdf_load_shading(ctx, csi->doc, patobj);
				fz_try(ctx)
				{
					if (stroke)
						proc->op_SC_shade(ctx, proc, csi->name, shade);
					else
						proc->op_sc_shade(ctx, proc, csi->name, shade);
				}
				fz_always(ctx)
					fz_drop_shade(ctx, shade);
				fz_catch(ctx)
					fz_rethrow(ctx);
			}
		}

		else
		{
			fz_throw(ctx, FZ_ERROR_GENERIC, "unknown pattern type: %d", pdf_to_int(ctx, type));
		}
	}

	else
	{
		if (proc->op_SC_color && proc->op_sc_color)
		{
			if (stroke)
				proc->op_SC_color(ctx, proc, csi->top, csi->stack);
			else
				proc->op_sc_color(ctx, proc, csi->top, csi->stack);
		}
	}
}

static pdf_obj *
resolve_properties(fz_context *ctx, pdf_csi *csi, pdf_obj *obj)
{
	if (pdf_is_name(ctx, obj))
		return pdf_dict_get(ctx, pdf_dict_get(ctx, csi->rdb, PDF_NAME_Properties), obj);
	else
		return obj;
}

static void
pdf_process_BDC(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, const char *name, pdf_obj *properties)
{
	if (proc->op_BDC)
		proc->op_BDC(ctx, proc, name, properties);

	/* Already hidden, no need to look further */
	if (proc->hidden > 0)
	{
		++proc->hidden;
		return;
	}

	/* We only look at OC groups here */
	if (strcmp(name, "OC"))
		return;

	/* No Properties array, or name not found, means visible. */
	if (!properties)
		return;

	/* Wrong type of property */
	if (!pdf_name_eq(ctx, pdf_dict_get(ctx, properties, PDF_NAME_Type), PDF_NAME_OCG))
		return;

	if (pdf_is_hidden_ocg(ctx, csi->doc->ocg, csi->rdb, proc->event, properties))
		++proc->hidden;
}

static void
pdf_process_BMC(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, const char *name)
{
	if (proc->op_BMC)
		proc->op_BMC(ctx, proc, name);
	if (proc->hidden > 0)
		++proc->hidden;
}

static void
pdf_process_EMC(fz_context *ctx, pdf_processor *proc, pdf_csi *csi)
{
	if (proc->op_EMC)
		proc->op_EMC(ctx, proc);
	if (proc->hidden > 0)
		--proc->hidden;
}

static void
pdf_process_gsave(fz_context *ctx, pdf_processor *proc, pdf_csi *csi)
{
	if (proc->op_q)
		proc->op_q(ctx, proc);
	++csi->gstate;
}

static void
pdf_process_grestore(fz_context *ctx, pdf_processor *proc, pdf_csi *csi)
{
	if (csi->gstate > 0)
	{
		if (proc->op_Q)
			proc->op_Q(ctx, proc);
		--csi->gstate;
	}
}

static void
pdf_process_end(fz_context *ctx, pdf_processor *proc, pdf_csi *csi)
{
	while (csi->gstate > 0)
		pdf_process_grestore(ctx, proc, csi);
	if (proc->op_END)
		proc->op_END(ctx, proc);
}

#define A(a) (a)
#define B(a,b) (a | b << 8)
#define C(a,b,c) (a | b << 8 | c << 16)

static int
pdf_process_keyword(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, fz_stream *stm, char *word)
{
	float *s = csi->stack;
	int key;

	key = word[0];
	if (word[1])
	{
		key |= word[1] << 8;
		if (word[2])
		{
			key |= word[2] << 16;
			if (word[3])
				key = 0;
		}
	}

	switch (key)
	{
	default:
		if (!csi->xbalance)
		{
			fz_warn(ctx, "unknown keyword: '%s'", word);
			return 1;
		}
		break;

	/* general graphics state */
	case A('w'): if (proc->op_w) proc->op_w(ctx, proc, s[0]); break;
	case A('j'): if (proc->op_j) proc->op_j(ctx, proc, s[0]); break;
	case A('J'): if (proc->op_J) proc->op_J(ctx, proc, s[0]); break;
	case A('M'): if (proc->op_M) proc->op_M(ctx, proc, s[0]); break;
	case A('d'): if (proc->op_d) proc->op_d(ctx, proc, csi->obj, s[0]); break;
	case B('r','i'): if (proc->op_ri) proc->op_ri(ctx, proc, csi->name); break;
	case A('i'): if (proc->op_i) proc->op_i(ctx, proc, s[0]); break;

	case B('g','s'):
		{
			pdf_obj *gsres, *gsobj;
			gsres = pdf_dict_get(ctx, csi->rdb, PDF_NAME_ExtGState);
			if (!gsres)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find ExtGState dictionary");
			gsobj = pdf_dict_gets(ctx, gsres, csi->name);
			if (!gsobj)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find ExtGState resource '%s'", csi->name);
			if (proc->op_gs_begin)
				proc->op_gs_begin(ctx, proc, csi->name, gsobj);
			pdf_process_extgstate(ctx, proc, csi, gsobj);
			if (proc->op_gs_end)
				proc->op_gs_end(ctx, proc);
		}
		break;

	/* special graphics state */
	case A('q'): pdf_process_gsave(ctx, proc, csi); break;
	case A('Q'): pdf_process_grestore(ctx, proc, csi); break;
	case B('c','m'): if (proc->op_cm) proc->op_cm(ctx, proc, s[0], s[1], s[2], s[3], s[4], s[5]); break;

	/* path construction */
	case A('m'): if (proc->op_m) proc->op_m(ctx, proc, s[0], s[1]); break;
	case A('l'): if (proc->op_l) proc->op_l(ctx, proc, s[0], s[1]); break;
	case A('c'): if (proc->op_c) proc->op_c(ctx, proc, s[0], s[1], s[2], s[3], s[4], s[5]); break;
	case A('v'): if (proc->op_v) proc->op_v(ctx, proc, s[0], s[1], s[2], s[3]); break;
	case A('y'): if (proc->op_y) proc->op_y(ctx, proc, s[0], s[1], s[2], s[3]); break;
	case A('h'): if (proc->op_h) proc->op_h(ctx, proc); break;
	case B('r','e'): if (proc->op_re) proc->op_re(ctx, proc, s[0], s[1], s[2], s[3]); break;

	/* path painting */
	case A('S'): if (proc->op_S) proc->op_S(ctx, proc); break;
	case A('s'): if (proc->op_s) proc->op_s(ctx, proc); break;
	case A('F'): if (proc->op_F) proc->op_F(ctx, proc); break;
	case A('f'): if (proc->op_f) proc->op_f(ctx, proc); break;
	case B('f','*'): if (proc->op_fstar) proc->op_fstar(ctx, proc); break;
	case A('B'): if (proc->op_B) proc->op_B(ctx, proc); break;
	case B('B','*'): if (proc->op_Bstar) proc->op_Bstar(ctx, proc); break;
	case A('b'): if (proc->op_b) proc->op_b(ctx, proc); break;
	case B('b','*'): if (proc->op_bstar) proc->op_bstar(ctx, proc); break;
	case A('n'): if (proc->op_n) proc->op_n(ctx, proc); break;

	/* path clipping */
	case A('W'): if (proc->op_W) proc->op_W(ctx, proc); break;
	case B('W','*'): if (proc->op_Wstar) proc->op_Wstar(ctx, proc); break;

	/* text objects */
	case B('B','T'): csi->in_text = 1; if (proc->op_BT) proc->op_BT(ctx, proc); break;
	case B('E','T'): csi->in_text = 0; if (proc->op_ET) proc->op_ET(ctx, proc); break;

	/* text state */
	case B('T','c'): if (proc->op_Tc) proc->op_Tc(ctx, proc, s[0]); break;
	case B('T','w'): if (proc->op_Tw) proc->op_Tw(ctx, proc, s[0]); break;
	case B('T','z'): if (proc->op_Tz) proc->op_Tz(ctx, proc, s[0]); break;
	case B('T','L'): if (proc->op_TL) proc->op_TL(ctx, proc, s[0]); break;
	case B('T','r'): if (proc->op_Tr) proc->op_Tr(ctx, proc, s[0]); break;
	case B('T','s'): if (proc->op_Ts) proc->op_Ts(ctx, proc, s[0]); break;

	case B('T','f'):
		if (proc->op_Tf)
		{
			pdf_obj *fontres, *fontobj;
			pdf_font_desc *font;
			fontres = pdf_dict_get(ctx, csi->rdb, PDF_NAME_Font);
			if (!fontres)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find Font dictionary");
			fontobj = pdf_dict_gets(ctx, fontres, csi->name);
			if (!fontobj)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find Font resource '%s'", csi->name);
			font = load_font_or_hail_mary(ctx, csi->doc, csi->rdb, fontobj, 0, csi->cookie);
			fz_try(ctx)
				proc->op_Tf(ctx, proc, csi->name, font, s[0]); break;
			fz_always(ctx)
				pdf_drop_font(ctx, font);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}
		break;

	/* text positioning */
	case B('T','d'): if (proc->op_Td) proc->op_Td(ctx, proc, s[0], s[1]); break;
	case B('T','D'): if (proc->op_TD) proc->op_TD(ctx, proc, s[0], s[1]); break;
	case B('T','m'): if (proc->op_Tm) proc->op_Tm(ctx, proc, s[0], s[1], s[2], s[3], s[4], s[5]); break;
	case B('T','*'): if (proc->op_Tstar) proc->op_Tstar(ctx, proc); break;

	/* text showing */
	case B('T','J'): if (proc->op_TJ) proc->op_TJ(ctx, proc, csi->obj); break;
	case B('T','j'):
		if (proc->op_Tj)
		{
			if (csi->string_len > 0)
				proc->op_Tj(ctx, proc, csi->string, csi->string_len);
			else
				proc->op_Tj(ctx, proc, pdf_to_str_buf(ctx, csi->obj), pdf_to_str_len(ctx, csi->obj));
		}
		break;
	case A('\''):
		if (proc->op_squote)
		{
			if (csi->string_len > 0)
				proc->op_squote(ctx, proc, csi->string, csi->string_len);
			else
				proc->op_squote(ctx, proc, pdf_to_str_buf(ctx, csi->obj), pdf_to_str_len(ctx, csi->obj));
		}
		break;
	case A('"'):
		if (proc->op_dquote)
		{
			if (csi->string_len > 0)
				proc->op_dquote(ctx, proc, s[0], s[1], csi->string, csi->string_len);
			else
				proc->op_dquote(ctx, proc, s[0], s[1], pdf_to_str_buf(ctx, csi->obj), pdf_to_str_len(ctx, csi->obj));
		}
		break;

	/* type 3 fonts */
	case B('d','0'): if (proc->op_d0) proc->op_d0(ctx, proc, s[0], s[1]); break;
	case B('d','1'): if (proc->op_d1) proc->op_d1(ctx, proc, s[0], s[1], s[2], s[3], s[4], s[5]); break;

	/* color */
	case B('C','S'): pdf_process_CS(ctx, proc, csi, 1); break;
	case B('c','s'): pdf_process_CS(ctx, proc, csi, 0); break;
	case B('S','C'): pdf_process_SC(ctx, proc, csi, 1); break;
	case B('s','c'): pdf_process_SC(ctx, proc, csi, 0); break;
	case C('S','C','N'): pdf_process_SC(ctx, proc, csi, 1); break;
	case C('s','c','n'): pdf_process_SC(ctx, proc, csi, 0); break;

	case A('G'): if (proc->op_G) proc->op_G(ctx, proc, s[0]); break;
	case A('g'): if (proc->op_g) proc->op_g(ctx, proc, s[0]); break;
	case B('R','G'): if (proc->op_RG) proc->op_RG(ctx, proc, s[0], s[1], s[2]); break;
	case B('r','g'): if (proc->op_rg) proc->op_rg(ctx, proc, s[0], s[1], s[2]); break;
	case A('K'): if (proc->op_K) proc->op_K(ctx, proc, s[0], s[1], s[2], s[3]); break;
	case A('k'): if (proc->op_k) proc->op_k(ctx, proc, s[0], s[1], s[2], s[3]); break;

	/* shadings, images, xobjects */
	case B('B','I'):
		{
			fz_image *img = parse_inline_image(ctx, csi, stm);
			fz_try(ctx)
			{
				if (proc->op_BI)
					proc->op_BI(ctx, proc, img);
			}
			fz_always(ctx)
				fz_drop_image(ctx, img);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}
		break;

	case B('s','h'):
		if (proc->op_sh)
		{
			pdf_obj *shaderes, *shadeobj;
			fz_shade *shade;
			shaderes = pdf_dict_get(ctx, csi->rdb, PDF_NAME_Shading);
			if (!shaderes)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find Shading dictionary");
			shadeobj = pdf_dict_gets(ctx, shaderes, csi->name);
			if (!shadeobj)
				fz_throw(ctx, FZ_ERROR_GENERIC, "cannot find Shading resource '%s'", csi->name);
			shade = pdf_load_shading(ctx, csi->doc, shadeobj);
			fz_try(ctx)
				proc->op_sh(ctx, proc, csi->name, shade);
			fz_always(ctx)
				fz_drop_shade(ctx, shade);
			fz_catch(ctx)
				fz_rethrow(ctx);
		}
		break;

	case B('D','o'): pdf_process_Do(ctx, proc, csi); break;

	/* marked content */
	case B('M','P'): if (proc->op_MP) proc->op_MP(ctx, proc, csi->name); break;
	case B('D','P'): if (proc->op_DP) proc->op_DP(ctx, proc, csi->name, resolve_properties(ctx, csi, csi->obj)); break;
	case C('B','M','C'): pdf_process_BMC(ctx, proc, csi, csi->name); break;
	case C('B','D','C'): pdf_process_BDC(ctx, proc, csi, csi->name, resolve_properties(ctx, csi, csi->obj)); break;
	case C('E','M','C'): pdf_process_EMC(ctx, proc, csi); break;

	/* compatibility */
	case B('B','X'): ++csi->xbalance; if (proc->op_BX) proc->op_BX(ctx, proc); break;
	case B('E','X'): --csi->xbalance; if (proc->op_EX) proc->op_EX(ctx, proc); break;
	}

	return 0;
}

static void
pdf_process_stream(fz_context *ctx, pdf_processor *proc, pdf_csi *csi, fz_stream *stm)
{
	pdf_document *doc = csi->doc;
	pdf_lexbuf *buf = csi->buf;
	fz_cookie *cookie = csi->cookie;

	pdf_token tok = PDF_TOK_ERROR;
	int in_text_array = 0;
	int ignoring_errors = 0;

	/* make sure we have a clean slate if we come here from flush_text */
	pdf_clear_stack(ctx, csi);

	fz_var(in_text_array);
	fz_var(tok);

	if (cookie)
	{
		cookie->progress_max = -1;
		cookie->progress = 0;
	}

	do
	{
		fz_try(ctx)
		{
			do
			{
				/* Check the cookie */
				if (cookie)
				{
					if (cookie->abort)
					{
						tok = PDF_TOK_EOF;
						break;
					}
					cookie->progress++;
				}

				tok = pdf_lex(ctx, stm, buf);

				if (in_text_array)
				{
					switch(tok)
					{
					case PDF_TOK_CLOSE_ARRAY:
						in_text_array = 0;
						break;
					case PDF_TOK_REAL:
						pdf_array_push_drop(ctx, csi->obj, pdf_new_real(ctx, doc, buf->f));
						break;
					case PDF_TOK_INT:
						pdf_array_push_drop(ctx, csi->obj, pdf_new_int(ctx, doc, buf->i));
						break;
					case PDF_TOK_STRING:
						pdf_array_push_drop(ctx, csi->obj, pdf_new_string(ctx, doc, buf->scratch, buf->len));
						break;
					case PDF_TOK_EOF:
						break;
					case PDF_TOK_KEYWORD:
						if (!strcmp(buf->scratch, "Tw") || !strcmp(buf->scratch, "Tc"))
						{
							int l = pdf_array_len(ctx, csi->obj);
							if (l > 0)
							{
								pdf_obj *o = pdf_array_get(ctx, csi->obj, l-1);
								if (pdf_is_number(ctx, o))
								{
									csi->stack[0] = pdf_to_real(ctx, o);
									pdf_array_delete(ctx, csi->obj, l-1);
									if (pdf_process_keyword(ctx, proc, csi, stm, buf->scratch) == 0)
										break;
								}
							}
						}
						/* Deliberate Fallthrough! */
					default:
						fz_throw(ctx, FZ_ERROR_GENERIC, "syntax error in array");
					}
				}
				else switch (tok)
				{
				case PDF_TOK_ENDSTREAM:
				case PDF_TOK_EOF:
					tok = PDF_TOK_EOF;
					break;

				case PDF_TOK_OPEN_ARRAY:
					if (csi->obj)
					{
						pdf_drop_obj(ctx, csi->obj);
						csi->obj = NULL;
					}
					if (csi->in_text)
					{
						in_text_array = 1;
						csi->obj = pdf_new_array(ctx, doc, 4);
					}
					else
					{
						csi->obj = pdf_parse_array(ctx, doc, stm, buf);
					}
					break;

				case PDF_TOK_OPEN_DICT:
					if (csi->obj)
					{
						pdf_drop_obj(ctx, csi->obj);
						csi->obj = NULL;
					}
					csi->obj = pdf_parse_dict(ctx, doc, stm, buf);
					break;

				case PDF_TOK_NAME:
					if (csi->name[0])
					{
						pdf_drop_obj(ctx, csi->obj);
						csi->obj = NULL;
						csi->obj = pdf_new_name(ctx, doc, buf->scratch);
					}
					else
						fz_strlcpy(csi->name, buf->scratch, sizeof(csi->name));
					break;

				case PDF_TOK_INT:
					if (csi->top < nelem(csi->stack)) {
						csi->stack[csi->top] = buf->i;
						csi->top ++;
					}
					else
						fz_throw(ctx, FZ_ERROR_GENERIC, "stack overflow");
					break;

				case PDF_TOK_REAL:
					if (csi->top < nelem(csi->stack)) {
						csi->stack[csi->top] = buf->f;
						csi->top ++;
					}
					else
						fz_throw(ctx, FZ_ERROR_GENERIC, "stack overflow");
					break;

				case PDF_TOK_STRING:
					if (buf->len <= sizeof(csi->string))
					{
						memcpy(csi->string, buf->scratch, buf->len);
						csi->string_len = buf->len;
					}
					else
					{
						if (csi->obj)
						{
							pdf_drop_obj(ctx, csi->obj);
							csi->obj = NULL;
						}
						csi->obj = pdf_new_string(ctx, doc, buf->scratch, buf->len);
					}
					break;

				case PDF_TOK_KEYWORD:
					if (pdf_process_keyword(ctx, proc, csi, stm, buf->scratch))
					{
						tok = PDF_TOK_EOF;
					}
					pdf_clear_stack(ctx, csi);
					break;

				default:
					fz_throw(ctx, FZ_ERROR_GENERIC, "syntax error in content stream");
				}
			}
			while (tok != PDF_TOK_EOF);
		}
		fz_always(ctx)
		{
			pdf_clear_stack(ctx, csi);
		}
		fz_catch(ctx)
		{
			int caught;

			if (!cookie)
			{
				fz_rethrow_if(ctx, FZ_ERROR_TRYLATER);
			}
			else if ((caught = fz_caught(ctx)) == FZ_ERROR_TRYLATER)
			{
				if (cookie->incomplete_ok)
					cookie->incomplete++;
				else
					fz_rethrow(ctx);
			}
			else if (caught == FZ_ERROR_ABORT)
			{
				fz_rethrow(ctx);
			}
			else
			{
				 cookie->errors++;
			}
			if (!ignoring_errors)
			{
				fz_warn(ctx, "Ignoring errors during rendering");
				ignoring_errors = 1;
			}
			/* If we do catch an error, then reset ourselves to a
			 * base lexing state */
			in_text_array = 0;
		}
	}
	while (tok != PDF_TOK_EOF);
}

void
pdf_process_contents(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_obj *rdb, pdf_obj *stmobj, fz_cookie *cookie)
{
	pdf_csi csi;
	pdf_lexbuf buf;
	fz_stream *stm = NULL;

	if (!stmobj)
		return;

	fz_var(stm);

	pdf_lexbuf_init(ctx, &buf, PDF_LEXBUF_SMALL);
	pdf_init_csi(ctx, &csi, doc, rdb, &buf, cookie);

	fz_try(ctx)
	{
		stm = pdf_open_contents_stream(ctx, doc, stmobj);
		pdf_process_stream(ctx, proc, &csi, stm);
		pdf_process_end(ctx, proc, &csi);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
		pdf_clear_stack(ctx, &csi);
		pdf_lexbuf_fin(ctx, &buf);
	}
	fz_catch(ctx)
	{
		fz_rethrow_if(ctx, FZ_ERROR_ABORT);
		fz_rethrow_message(ctx, "cannot parse content stream");
	}
}

void
pdf_process_annot(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_page *page, pdf_annot *annot, fz_cookie *cookie)
{
	int flags = pdf_to_int(ctx, pdf_dict_get(ctx, annot->obj, PDF_NAME_F));

	if (flags & (F_Invisible | F_Hidden))
		return;

	if (proc->event)
	{
		if (!strcmp(proc->event, "Print") && !(flags & F_Print))
			return;
		if (!strcmp(proc->event, "View") && (flags & F_NoView))
			return;
	}

	/* TODO: NoZoom and NoRotate */

	/* XXX what resources, if any, to use for this check? */
	if (pdf_is_hidden_ocg(ctx, doc->ocg, NULL, proc->event, pdf_dict_get(ctx, annot->obj, PDF_NAME_OC)))
		return;

	if (proc->op_q && proc->op_cm && proc->op_Do_form && proc->op_Q)
	{
		proc->op_q(ctx, proc);
		proc->op_cm(ctx, proc,
				annot->matrix.a, annot->matrix.b, annot->matrix.c,
				annot->matrix.d, annot->matrix.e, annot->matrix.f);
		proc->op_Do_form(ctx, proc, "Annot", annot->ap, page->resources);
		proc->op_Q(ctx, proc);
	}
}

void
pdf_process_glyph(fz_context *ctx, pdf_processor *proc, pdf_document *doc, pdf_obj *rdb, fz_buffer *contents)
{
	pdf_csi csi;
	pdf_lexbuf buf;
	fz_stream *stm = NULL;

	fz_var(stm);

	if (!contents)
		return;

	pdf_lexbuf_init(ctx, &buf, PDF_LEXBUF_SMALL);
	pdf_init_csi(ctx, &csi, doc, rdb, &buf, NULL);

	fz_try(ctx)
	{
		fz_stream *stm = fz_open_buffer(ctx, contents);
		pdf_process_stream(ctx, proc, &csi, stm);
		pdf_process_end(ctx, proc, &csi);
	}
	fz_always(ctx)
	{
		fz_drop_stream(ctx, stm);
		pdf_clear_stack(ctx, &csi);
		pdf_lexbuf_fin(ctx, &buf);
	}
	fz_catch(ctx)
	{
		fz_rethrow_if(ctx, FZ_ERROR_ABORT);
		fz_rethrow_message(ctx, "cannot parse glyph content stream");
	}
}
