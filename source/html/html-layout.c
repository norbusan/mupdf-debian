#include "mupdf/html.h"

#include "hb.h"
#include "hb-ft.h"
#include <ft2build.h>

#undef DEBUG_HARFBUZZ

enum { T, R, B, L };

static const char *default_css =
"@page{margin:2em 1em}"
"a{color:#06C;text-decoration:underline}"
"address{display:block;font-style:italic}"
"b{font-weight:bold}"
"bdo{direction:rtl;unicode-bidi:bidi-override}"
"blockquote{display:block;margin:1em 40px}"
"body{display:block;margin:1em}"
"cite{font-style:italic}"
"code{font-family:monospace}"
"dd{display:block;margin:0 0 0 40px}"
"del{text-decoration:line-through}"
"div{display:block}"
"dl{display:block;margin:1em 0}"
"dt{display:block}"
"em{font-style:italic}"
"h1{display:block;font-size:2em;font-weight:bold;margin:0.67em 0;page-break-after:avoid}"
"h2{display:block;font-size:1.5em;font-weight:bold;margin:0.83em 0;page-break-after:avoid}"
"h3{display:block;font-size:1.17em;font-weight:bold;margin:1em 0;page-break-after:avoid}"
"h4{display:block;font-size:1em;font-weight:bold;margin:1.33em 0;page-break-after:avoid}"
"h5{display:block;font-size:0.83em;font-weight:bold;margin:1.67em 0;page-break-after:avoid}"
"h6{display:block;font-size:0.67em;font-weight:bold;margin:2.33em 0;page-break-after:avoid}"
"head{display:none}"
"hr{border-style:solid;border-width:1px;display:block;margin-bottom:0.5em;margin-top:0.5em;text-align:center}"
"html{display:block}"
"i{font-style:italic}"
"ins{text-decoration:underline}"
"kbd{font-family:monospace}"
"li{display:list-item}"
"menu{display:block;list-style-type:disc;margin:1em 0;padding:0 0 0 30pt}"
"ol{display:block;list-style-type:decimal;margin:1em 0;padding:0 0 0 30pt}"
"p{display:block;margin:1em 0}"
"pre{display:block;font-family:monospace;margin:1em 0;white-space:pre}"
"samp{font-family:monospace}"
"script{display:none}"
"small{font-size:0.83em}"
"strong{font-weight:bold}"
"style{display:none}"
"sub{font-size:0.83em;vertical-align:sub}"
"sup{font-size:0.83em;vertical-align:super}"
"table{display:table}"
"tbody{display:table-row-group}"
"td{display:table-cell;padding:1px}"
"tfoot{display:table-footer-group}"
"th{display:table-cell;font-weight:bold;padding:1px;text-align:center}"
"thead{display:table-header-group}"
"tr{display:table-row}"
"ul{display:block;list-style-type:disc;margin:1em 0;padding:0 0 0 30pt}"
"ul ul{list-style-type:circle}"
"ul ul ul{list-style-type:square}"
"var{font-style:italic}"
"svg{display:none}"
;

#define DEFAULT_DIR FZ_BIDI_LTR

struct genstate
{
	fz_pool *pool;
	fz_html_font_set *set;
	fz_archive *zip;
	const char *base_uri;
	fz_css_rule *css;
	int at_bol;
	int emit_white;
	int last_brk_cls;
};

static int iswhite(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int is_all_white(const char *s)
{
	while (*s)
	{
		if (!iswhite(*s))
			return 0;
		++s;
	}
	return 1;
}

/* TODO: pool allocator for flow nodes */
/* TODO: store text by pointing to a giant buffer */

static void fz_drop_html_flow(fz_context *ctx, fz_html_flow *flow)
{
	while (flow)
	{
		fz_html_flow *next = flow->next;
		if (flow->type == FLOW_IMAGE)
			fz_drop_image(ctx, flow->content.image);
		flow = next;
	}
}

static fz_html_flow *add_flow(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box, int type)
{
	fz_html_flow *flow = fz_pool_alloc(ctx, pool, sizeof *flow);
	flow->type = type;
	flow->expand = 0;
	flow->bidi_level = 0;
	flow->breaks_line = 0;
	flow->box = inline_box;
	*top->flow_tail = flow;
	top->flow_tail = &flow->next;
	return flow;
}

static void add_flow_space(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box)
{
	fz_html_flow *flow = add_flow(ctx, pool, top, inline_box, FLOW_SPACE);
	flow->expand = 1;
}

static void add_flow_break(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box)
{
	(void)add_flow(ctx, pool, top, inline_box, FLOW_BREAK);
}

static void add_flow_sbreak(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box)
{
	(void)add_flow(ctx, pool, top, inline_box, FLOW_SBREAK);
}

static void add_flow_shyphen(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box)
{
	(void)add_flow(ctx, pool, top, inline_box, FLOW_SHYPHEN);
}

static void add_flow_word(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box, const char *a, const char *b)
{
	fz_html_flow *flow = add_flow(ctx, pool, top, inline_box, FLOW_WORD);
	flow->content.text = fz_pool_alloc(ctx, pool, b - a + 1);
	memcpy(flow->content.text, a, b - a);
	flow->content.text[b - a] = 0;
}

static void add_flow_image(fz_context *ctx, fz_pool *pool, fz_html *top, fz_html *inline_box, fz_image *img)
{
	fz_html_flow *flow = add_flow(ctx, pool, top, inline_box, FLOW_IMAGE);
	flow->content.image = fz_keep_image(ctx, img);
}

static fz_html_flow *split_flow(fz_context *ctx, fz_pool *pool, fz_html_flow *flow, size_t offset)
{
	fz_html_flow *new_flow;
	char *text;
	size_t len;

	if (offset == 0)
		return flow;
	new_flow = fz_pool_alloc(ctx, pool, sizeof *flow);
	*new_flow = *flow;
	new_flow->next = flow->next;
	flow->next = new_flow;

	text = flow->content.text;
	while (*text && offset)
	{
		int rune;
		text += fz_chartorune(&rune, text);
		offset--;
	}
	len = strlen(text);
	new_flow->content.text = fz_pool_alloc(ctx, pool, len+1);
	strcpy(new_flow->content.text, text);
	*text = 0;
	return new_flow;
}

static void flush_space(fz_context *ctx, fz_pool *pool, fz_html *flow, fz_html *inline_box, struct genstate *g)
{
	static const char *space = " ";
	int bsp = inline_box->style.white_space & WS_ALLOW_BREAK_SPACE;
	if (g->emit_white)
	{
		if (!g->at_bol)
		{
			if (bsp)
				add_flow_space(ctx, pool, flow, inline_box);
			else
				add_flow_word(ctx, pool, flow, inline_box, space, space+1);
		}
		g->emit_white = 0;
	}
}

/* pair-wise lookup table for UAX#14 linebreaks */
static const char *pairbrk[29] =
{
/*	-OCCQGNESIPPNAHIIHBBBZCWHHJJJR- */
/*	-PLPULSXYSROULLDNYAB2WMJ23LVTI- */
	"^^^^^^^^^^^^^^^^^^^^^^^^^^^^^", /* OP open punctuation */
	"_^^%%^^^^%%_____%%__^^^______", /* CL close punctuation */
	"_^^%%^^^^%%%%%__%%__^^^______", /* CP close parenthesis */
	"^^^%%%^^^%%%%%%%%%%%^^^%%%%%%", /* QU quotation */
	"%^^%%%^^^%%%%%%%%%%%^^^%%%%%%", /* GL non-breaking glue */
	"_^^%%%^^^_______%%__^^^______", /* NS nonstarters */
	"_^^%%%^^^______%%%__^^^______", /* EX exclamation/interrogation */
	"_^^%%%^^^__%_%__%%__^^^______", /* SY symbols allowing break after */
	"_^^%%%^^^__%%%__%%__^^^______", /* IS infix numeric separator */
	"%^^%%%^^^__%%%%_%%__^^^%%%%%_", /* PR prefix numeric */
	"%^^%%%^^^__%%%__%%__^^^______", /* PO postfix numeric */
	"%^^%%%^^^%%%%%_%%%__^^^______", /* NU numeric */
	"%^^%%%^^^__%%%_%%%__^^^______", /* AL ordinary alphabetic and symbol characters */
	"%^^%%%^^^__%%%_%%%__^^^______", /* HL hebrew letter */
	"_^^%%%^^^_%____%%%__^^^______", /* ID ideographic */
	"_^^%%%^^^______%%%__^^^______", /* IN inseparable characters */
	"_^^%_%^^^__%____%%__^^^______", /* HY hyphens */
	"_^^%_%^^^_______%%__^^^______", /* BA break after */
	"%^^%%%^^^%%%%%%%%%%%^^^%%%%%%", /* BB break before */
	"_^^%%%^^^_______%%_^^^^______", /* B2 break opportunity before and after */
	"____________________^________", /* ZW zero width space */
	"%^^%%%^^^__%%%_%%%__^^^______", /* CM combining mark */
	"%^^%%%^^^%%%%%%%%%%%^^^%%%%%%", /* WJ word joiner */
	"_^^%%%^^^_%____%%%__^^^___%%_", /* H2 hangul leading/vowel syllable */
	"_^^%%%^^^_%____%%%__^^^____%_", /* H3 hangul leading/voewl/trailing syllable */
	"_^^%%%^^^_%____%%%__^^^%%%%__", /* JL hangul leading jamo */
	"_^^%%%^^^_%____%%%__^^^___%%_", /* JV hangul vowel jamo */
	"_^^%%%^^^_%____%%%__^^^____%_", /* JT hangul trailing jamo */
	"_^^%%%^^^_______%%__^^^_____%", /* RI regional indicator */
};

static void generate_text(fz_context *ctx, fz_pool *pool, fz_html *box, const char *text, struct genstate *g)
{
	fz_html *flow;

	int collapse = box->style.white_space & WS_COLLAPSE;
	int bsp = box->style.white_space & WS_ALLOW_BREAK_SPACE;
	int bnl = box->style.white_space & WS_FORCE_BREAK_NEWLINE;

	static const char *space = " ";

	flow = box;
	while (flow->type != BOX_FLOW)
		flow = flow->up;

	while (*text)
	{
		if (bnl && (*text == '\n' || *text == '\r'))
		{
			if (text[0] == '\r' && text[1] == '\n')
				text += 2;
			else
				text += 1;
			add_flow_break(ctx, pool, flow, box);
			g->at_bol = 1;
		}
		else if (iswhite(*text))
		{
			if (collapse)
			{
				if (bnl)
					while (*text == ' ' || *text == '\t')
						++text;
				else
					while (iswhite(*text))
						++text;
				g->emit_white = 1;
			}
			else
			{
				// TODO: tabs
				if (bsp)
					add_flow_space(ctx, pool, flow, box);
				else
					add_flow_word(ctx, pool, flow, box, space, space+1);
				++text;
			}
			g->last_brk_cls = UCDN_LINEBREAK_CLASS_WJ; /* don't add sbreaks after a space */
		}
		else
		{
			const char *prev, *mark = text;
			int c;

			flush_space(ctx, pool, flow, box, g);

			if (g->at_bol)
				g->last_brk_cls = UCDN_LINEBREAK_CLASS_WJ;

			while (*text && !iswhite(*text))
			{
				prev = text;
				text += fz_chartorune(&c, text);
				if (c == 0xAD) /* soft hyphen */
				{
					if (mark != prev)
						add_flow_word(ctx, pool, flow, box, mark, prev);
					add_flow_shyphen(ctx, pool, flow, box);
					mark = text;
					g->last_brk_cls = UCDN_LINEBREAK_CLASS_WJ; /* don't add sbreaks after a soft hyphen */
				}
				else if (bsp) /* allow soft breaks */
				{
					int this_brk_cls = ucdn_get_resolved_linebreak_class(c);
					if (this_brk_cls < UCDN_LINEBREAK_CLASS_RI)
					{
						int brk = pairbrk[g->last_brk_cls][this_brk_cls];

						/* we handle spaces elsewhere, so ignore these classes */
						if (brk == '@') brk = '^';
						if (brk == '#') brk = '^';
						if (brk == '%') brk = '^';

						if (brk == '_')
						{
							if (mark != prev)
								add_flow_word(ctx, pool, flow, box, mark, prev);
							add_flow_sbreak(ctx, pool, flow, box);
							mark = prev;
						}

						g->last_brk_cls = this_brk_cls;
					}
				}
			}
			if (mark != text)
				add_flow_word(ctx, pool, flow, box, mark, text);

			g->at_bol = 0;
		}
	}
}

static void generate_image(fz_context *ctx, fz_pool *pool, fz_archive *zip, const char *base_uri, fz_html *box, const char *src, struct genstate *g)
{
	fz_image *img = NULL;
	fz_buffer *buf = NULL;
	char path[2048];

	fz_html *flow = box;
	while (flow->type != BOX_FLOW)
		flow = flow->up;

	fz_strlcpy(path, base_uri, sizeof path);
	fz_strlcat(path, "/", sizeof path);
	fz_strlcat(path, src, sizeof path);
	fz_urldecode(path);
	fz_cleanname(path);

	fz_var(buf);
	fz_var(img);

	flush_space(ctx, pool, flow, box, g);

	fz_try(ctx)
	{
		buf = fz_read_archive_entry(ctx, zip, path);
		img = fz_new_image_from_buffer(ctx, buf);
		add_flow_sbreak(ctx, pool, flow, box);
		add_flow_image(ctx, pool, flow, box, img);
		add_flow_sbreak(ctx, pool, flow, box);
	}
	fz_always(ctx)
	{
		fz_drop_buffer(ctx, buf);
		fz_drop_image(ctx, img);
	}
	fz_catch(ctx)
	{
		const char *alt = "[image]";
		fz_warn(ctx, "html: cannot add image src='%s'", src);
		add_flow_word(ctx, pool, flow, box, alt, alt + 7);
	}

	g->at_bol = 0;
}

static void init_box(fz_context *ctx, fz_html *box, fz_bidi_direction markup_dir)
{
	box->type = BOX_BLOCK;
	box->x = box->y = 0;
	box->w = box->h = 0;

	box->up = NULL;
	box->last = NULL;
	box->down = NULL;
	box->next = NULL;

	box->flow_head = NULL;
	box->flow_tail = &box->flow_head;
	box->markup_dir = markup_dir;

	fz_default_css_style(ctx, &box->style);
}

static void fz_drop_html_box(fz_context *ctx, fz_html *box)
{
	while (box)
	{
		fz_html *next = box->next;
		fz_drop_html_flow(ctx, box->flow_head);
		fz_drop_html_box(ctx, box->down);
		box = next;
	}
}

void fz_drop_html(fz_context *ctx, fz_html *box)
{
	fz_drop_html_box(ctx, box);
	fz_drop_pool(ctx, box->pool);
}

static fz_html *new_box(fz_context *ctx, fz_pool *pool, fz_bidi_direction markup_dir)
{
	fz_html *box = fz_pool_alloc(ctx, pool, sizeof *box);
	init_box(ctx, box, markup_dir);
	return box;
}

static void insert_box(fz_context *ctx, fz_html *box, int type, fz_html *top)
{
	box->type = type;

	box->up = top;

	if (top)
	{
		if (!top->last)
		{
			top->down = top->last = box;
		}
		else
		{
			top->last->next = box;
			top->last = box;
		}
	}
}

static fz_html *insert_block_box(fz_context *ctx, fz_html *box, fz_html *top)
{
	if (top->type == BOX_BLOCK)
	{
		insert_box(ctx, box, BOX_BLOCK, top);
	}
	else if (top->type == BOX_FLOW)
	{
		while (top->type != BOX_BLOCK)
			top = top->up;
		insert_box(ctx, box, BOX_BLOCK, top);
	}
	else if (top->type == BOX_INLINE)
	{
		while (top->type != BOX_BLOCK)
			top = top->up;
		insert_box(ctx, box, BOX_BLOCK, top);
	}
	return top;
}

static fz_html *insert_break_box(fz_context *ctx, fz_html *box, fz_html *top)
{
	if (top->type == BOX_BLOCK)
	{
		insert_box(ctx, box, BOX_BREAK, top);
	}
	else if (top->type == BOX_FLOW)
	{
		while (top->type != BOX_BLOCK)
			top = top->up;
		insert_box(ctx, box, BOX_BREAK, top);
	}
	else if (top->type == BOX_INLINE)
	{
		while (top->type != BOX_BLOCK)
			top = top->up;
		insert_box(ctx, box, BOX_BREAK, top);
	}
	return top;
}

static void insert_inline_box(fz_context *ctx, fz_pool *pool, fz_html *box, fz_html *top, int markup_dir, struct genstate *g)
{
	if (top->type == BOX_BLOCK)
	{
		if (top->last && top->last->type == BOX_FLOW)
		{
			insert_box(ctx, box, BOX_INLINE, top->last);
		}
		else
		{
			fz_html *flow = new_box(ctx, pool, markup_dir);
			flow->is_first_flow = !top->last;
			insert_box(ctx, flow, BOX_FLOW, top);
			insert_box(ctx, box, BOX_INLINE, flow);
			g->at_bol = 1;
		}
	}
	else if (top->type == BOX_FLOW)
	{
		insert_box(ctx, box, BOX_INLINE, top);
	}
	else if (top->type == BOX_INLINE)
	{
		insert_box(ctx, box, BOX_INLINE, top);
	}
}

static void generate_boxes(fz_context *ctx, fz_xml *node, fz_html *top,
		fz_css_match *up_match, int list_counter, int markup_dir, struct genstate *g)
{
	fz_css_match match;
	fz_html *box;
	const char *tag;
	int display;

	while (node)
	{
		match.up = up_match;
		match.count = 0;

		tag = fz_xml_tag(node);
		if (tag)
		{
			fz_match_css(ctx, &match, g->css, node);

			display = fz_get_css_match_display(&match);

			if (!strcmp(tag, "br"))
			{
				if (top->type == BOX_INLINE)
				{
					fz_html *flow = top;
					while (flow->type != BOX_FLOW)
						flow = flow->up;
					add_flow_break(ctx, g->pool, flow, top);
				}
				else
				{
					box = new_box(ctx, g->pool, markup_dir);
					fz_apply_css_style(ctx, g->set, &box->style, &match);
					top = insert_break_box(ctx, box, top);
				}
				g->at_bol = 1;
			}

			else if (!strcmp(tag, "img"))
			{
				const char *src = fz_xml_att(node, "src");
				if (src)
				{
					box = new_box(ctx, g->pool, markup_dir);
					fz_apply_css_style(ctx, g->set, &box->style, &match);
					insert_inline_box(ctx, g->pool, box, top, markup_dir, g);
					generate_image(ctx, g->pool, g->zip, g->base_uri, box, src, g);
				}
			}

			else if (display != DIS_NONE)
			{
				int child_dir = markup_dir;

				const char *dir = fz_xml_att(node, "dir");
				if (dir)
				{
					if (!strcmp(dir, "auto"))
						child_dir = FZ_BIDI_NEUTRAL;
					else if (!strcmp(dir, "rtl"))
						child_dir = FZ_BIDI_RTL;
					else if (!strcmp(dir, "ltr"))
						child_dir = FZ_BIDI_LTR;
					else
						child_dir = DEFAULT_DIR;
				}

				box = new_box(ctx, g->pool, child_dir);
				fz_apply_css_style(ctx, g->set, &box->style, &match);

				if (display == DIS_BLOCK || display == DIS_INLINE_BLOCK)
				{
					top = insert_block_box(ctx, box, top);
				}
				else if (display == DIS_LIST_ITEM)
				{
					top = insert_block_box(ctx, box, top);
					box->list_item = ++list_counter;
				}
				else if (display == DIS_INLINE)
				{
					insert_inline_box(ctx, g->pool, box, top, child_dir, g);
				}
				else
				{
					fz_warn(ctx, "unknown box display type");
					insert_box(ctx, box, BOX_BLOCK, top);
				}

				if (fz_xml_down(node))
				{
					int child_counter = list_counter;
					if (!strcmp(tag, "ul") || !strcmp(tag, "ol"))
						child_counter = 0;
					generate_boxes(ctx, fz_xml_down(node), box, &match, child_counter, child_dir, g);
				}
			}
		}
		else
		{
			const char *text = fz_xml_text(node);
			int collapse = top->style.white_space & WS_COLLAPSE;
			if (collapse && is_all_white(text))
			{
				g->emit_white = 1;
			}
			else
			{
				if (top->type != BOX_INLINE)
				{
					/* Create anonymous inline box, with the same style as the top block box. */
					box = new_box(ctx, g->pool, markup_dir);
					insert_inline_box(ctx, g->pool, box, top, markup_dir, g);
					box->style = top->style;
					/* Make sure not to recursively multiply font sizes. */
					box->style.font_size.value = 1;
					box->style.font_size.unit = N_SCALE;
					generate_text(ctx, g->pool, box, text, g);
				}
				else
				{
					generate_text(ctx, g->pool, top, text, g);
				}
			}
		}

		node = fz_xml_next(node);
	}
}

static void measure_image(fz_context *ctx, fz_html_flow *node, float max_w, float max_h)
{
	float xs = 1, ys = 1, s = 1;
	node->x = 0;
	node->y = 0;
	if (node->content.image->w > max_w)
		xs = max_w / node->content.image->w;
	if (node->content.image->h > max_h)
		ys = max_h / node->content.image->h;
	s = fz_min(xs, ys);
	node->w = node->content.image->w * s;
	node->h = node->content.image->h * s;
}

typedef struct string_walker
{
	fz_context *ctx;
	hb_buffer_t *hb_buf;
	int rtl;
	const char *start;
	const char *end;
	const char *s;
	fz_font *base_font;
	int script;
	fz_font *font;
	fz_font *next_font;
	hb_glyph_position_t *glyph_pos;
	hb_glyph_info_t *glyph_info;
	unsigned int glyph_count;
	int scale;
} string_walker;

static int quick_ligature_mov(fz_context *ctx, string_walker *walker, unsigned int i, unsigned int n, int unicode)
{
	unsigned int k;
	for (k = i + n + 1; k < walker->glyph_count; ++k)
	{
		walker->glyph_info[k-n] = walker->glyph_info[k];
		walker->glyph_pos[k-n] = walker->glyph_pos[k];
	}
	walker->glyph_count -= n;
	return unicode;
}

static int quick_ligature(fz_context *ctx, string_walker *walker, unsigned int i)
{
	if (walker->glyph_info[i].codepoint == 'f' && i + 1 < walker->glyph_count && !walker->font->is_mono)
	{
		if (walker->glyph_info[i+1].codepoint == 'f')
		{
			if (i + 2 < walker->glyph_count && walker->glyph_info[i+2].codepoint == 'i')
			{
				if (fz_encode_character(ctx, walker->font, 0xFB03))
					return quick_ligature_mov(ctx, walker, i, 2, 0xFB03);
			}
			if (i + 2 < walker->glyph_count && walker->glyph_info[i+2].codepoint == 'l')
			{
				if (fz_encode_character(ctx, walker->font, 0xFB04))
					return quick_ligature_mov(ctx, walker, i, 2, 0xFB04);
			}
			if (fz_encode_character(ctx, walker->font, 0xFB00))
				return quick_ligature_mov(ctx, walker, i, 1, 0xFB00);
		}
		if (walker->glyph_info[i+1].codepoint == 'i')
		{
			if (fz_encode_character(ctx, walker->font, 0xFB01))
				return quick_ligature_mov(ctx, walker, i, 1, 0xFB01);
		}
		if (walker->glyph_info[i+1].codepoint == 'l')
		{
			if (fz_encode_character(ctx, walker->font, 0xFB02))
				return quick_ligature_mov(ctx, walker, i, 1, 0xFB02);
		}
	}
	return walker->glyph_info[i].codepoint;
}

static void init_string_walker(fz_context *ctx, string_walker *walker, hb_buffer_t *hb_buf, int rtl, fz_font *font, int script, const char *text)
{
	walker->ctx = ctx;
	walker->hb_buf = hb_buf;
	walker->rtl = rtl;
	walker->start = text;
	walker->end = text;
	walker->s = text;
	walker->base_font = font;
	walker->script = script;
	walker->font = NULL;
	walker->next_font = NULL;
}

static int walk_string(string_walker *walker)
{
	fz_context *ctx = walker->ctx;
	FT_Face face;
	int fterr;
	int quickshape;

	walker->start = walker->end;
	walker->end = walker->s;
	walker->font = walker->next_font;

	if (*walker->start == 0)
		return 0;

	/* Run through the string, encoding chars until we find one
	 * that requires a different fallback font. */
	while (*walker->s)
	{
		int c;

		walker->s += fz_chartorune(&c, walker->s);
		(void)fz_encode_character_with_fallback(ctx, walker->base_font, c, walker->script, &walker->next_font);
		if (walker->next_font != walker->font)
		{
			if (walker->font != NULL)
				break;
			walker->font = walker->next_font;
		}
		walker->end = walker->s;
	}

	/* Disable harfbuzz shaping if script is common or LGC and there are no opentype tables. */
	quickshape = 0;
	if (walker->script <= 3 && !walker->rtl && !walker->font->has_opentype)
		quickshape = 1;

	hb_lock(ctx);
	fz_try(ctx)
	{
		face = walker->font->ft_face;
		walker->scale = face->units_per_EM;
		fterr = FT_Set_Char_Size(face, walker->scale, walker->scale, 72, 72);
		if (fterr)
			fz_throw(ctx, FZ_ERROR_GENERIC, "freetype setting character size: %s", ft_error_string(fterr));

		hb_buffer_clear_contents(walker->hb_buf);
		hb_buffer_set_direction(walker->hb_buf, walker->rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
		/* hb_buffer_set_script(hb_buf, hb_ucdn_script_translate(script)); */
		/* hb_buffer_set_language(hb_buf, hb_language_from_string("en", strlen("en"))); */
		/* hb_buffer_set_cluster_level(hb_buf, HB_BUFFER_CLUSTER_LEVEL_CHARACTERS); */

		hb_buffer_add_utf8(walker->hb_buf, walker->start, walker->end - walker->start, 0, -1);

		if (!quickshape)
		{
			if (walker->font->hb_font == NULL)
			{
				Memento_startLeaking(); /* HarfBuzz leaks harmlessly */
				walker->font->hb_font = hb_ft_font_create(face, NULL);
				Memento_stopLeaking();
			}

			Memento_startLeaking(); /* HarfBuzz leaks harmlessly */
			hb_buffer_guess_segment_properties(walker->hb_buf);
			Memento_stopLeaking();

			hb_shape(walker->font->hb_font, walker->hb_buf, NULL, 0);
		}

		walker->glyph_pos = hb_buffer_get_glyph_positions(walker->hb_buf, &walker->glyph_count);
		walker->glyph_info = hb_buffer_get_glyph_infos(walker->hb_buf, NULL);
	}
	fz_always(ctx)
	{
		hb_unlock(ctx);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}

	if (quickshape)
	{
		unsigned int i;
		for (i = 0; i < walker->glyph_count; ++i)
		{
			int unicode = quick_ligature(ctx, walker, i);
			int glyph = fz_encode_character(ctx, walker->font, unicode);
			walker->glyph_info[i].codepoint = glyph;
			walker->glyph_pos[i].x_offset = 0;
			walker->glyph_pos[i].y_offset = 0;
			walker->glyph_pos[i].x_advance = fz_advance_glyph(ctx, walker->font, glyph, 0) * face->units_per_EM;
			walker->glyph_pos[i].y_advance = 0;
		}
	}

	return 1;
}

static const char *get_node_text(fz_context *ctx, fz_html_flow *node)
{
	if (node->type == FLOW_WORD)
		return node->content.text;
	else if (node->type == FLOW_SPACE)
		return " ";
	else if (node->type == FLOW_SHYPHEN)
		return "-";
	else
		return "";
}

static void measure_string(fz_context *ctx, fz_html_flow *node, hb_buffer_t *hb_buf)
{
	string_walker walker;
	unsigned int i;
	const char *s;
	float em;

	em = node->box->em;
	node->x = 0;
	node->y = 0;
	node->w = 0;
	node->h = fz_from_css_number_scale(node->box->style.line_height, em, em, em);

	s = get_node_text(ctx, node);
	init_string_walker(ctx, &walker, hb_buf, node->bidi_level & 1, node->box->style.font, node->script, s);
	while (walk_string(&walker))
	{
		int x = 0;
		for (i = 0; i < walker.glyph_count; i++)
			x += walker.glyph_pos[i].x_advance;
		node->w += x * em / walker.scale;
	}
}

static float measure_line(fz_html_flow *node, fz_html_flow *end, float *baseline)
{
	float max_a = 0, max_d = 0, h = node->h;
	while (node != end)
	{
		if (node->type == FLOW_IMAGE)
		{
			if (node->h > max_a)
				max_a = node->h;
		}
		else
		{
			float a = node->box->em * 0.8;
			float d = node->box->em * 0.2;
			if (a > max_a) max_a = a;
			if (d > max_d) max_d = d;
		}
		if (node->h > h) h = node->h;
		if (max_a + max_d > h) h = max_a + max_d;
		node = node->next;
	}
	*baseline = max_a + (h - max_a - max_d) / 2;
	return h;
}

static void layout_line(fz_context *ctx, float indent, float page_w, float line_w, int align, fz_html_flow *start, fz_html_flow *end, fz_html *box, float baseline, float line_h)
{
	float x = box->x + indent;
	float y = box->y + box->h;
	float slop = page_w - line_w;
	float justify = 0;
	float va;
	int n, i;
	fz_html_flow *node;
	fz_html_flow **reorder;
	unsigned int min_level, max_level;

	/* Count the number of nodes on the line */
	for(i = 0, n = 0, node = start; node != end; node = node->next)
	{
		n++;
		if (node->type == FLOW_SPACE && node->expand && !node->breaks_line)
			i++;
	}

	if (align == TA_JUSTIFY)
	{
		justify = slop / i;
	}
	else if (align == TA_RIGHT)
		x += slop;
	else if (align == TA_CENTER)
		x += slop / 2;

	/* We need a block to hold the node pointers while we reorder */
	reorder = fz_malloc_array(ctx, n, sizeof(*reorder));
	min_level = start->bidi_level;
	max_level = start->bidi_level;
	for(i = 0, node = start; node != end; i++, node = node->next)
	{
		reorder[i] = node;
		if (node->bidi_level < min_level)
			min_level = node->bidi_level;
		if (node->bidi_level > max_level)
			max_level = node->bidi_level;
	}

	/* Do we need to do any reordering? */
	if (min_level != max_level || (min_level & 1))
	{
		/* The lowest level we swap is always a rtl one */
		min_level |= 1;
		/* Each time around the loop we swap runs of fragments that have
		 * levels >= max_level (and decrement max_level). */
		do
		{
			int start = 0;
			int end;
			do
			{
				/* Skip until we find a level that's >= max_level */
				while (start < n && reorder[start]->bidi_level < max_level)
					start++;
				/* If start >= n-1 then no more runs. */
				if (start >= n-1)
					break;
				/* Find the end of the match */
				i = start+1;
				while (i < n && reorder[i]->bidi_level >= max_level)
					i++;
				/* Reverse from start to i-1 */
				end = i-1;
				while (start < end)
				{
					fz_html_flow *t = reorder[start];
					reorder[start++] = reorder[end];
					reorder[end--] = t;
				}
				start = i+1;
			}
			while (start < n);
			max_level--;
		}
		while (max_level >= min_level);
	}

	for (i = 0; i < n; i++)
	{
		float w;

		node = reorder[i];
		w = node->w;

		if (node->type == FLOW_SPACE && node->breaks_line)
			w = 0;
		else if (node->type == FLOW_SPACE && !node->breaks_line)
			w += node->expand ? justify : 0;
		else if (node->type == FLOW_SHYPHEN && !node->breaks_line)
			w = 0;
		else if (node->type == FLOW_SHYPHEN && node->breaks_line)
			w = node->w;

		node->x = x;
		x += w;

		switch (node->box->style.vertical_align)
		{
		default:
		case VA_BASELINE:
			va = 0;
			break;
		case VA_SUB:
			va = node->box->em * 0.2f;
			break;
		case VA_SUPER:
			va = node->box->em * -0.3f;
			break;
		case VA_TOP:
		case VA_TEXT_TOP:
			va = -baseline + node->box->em * 0.8;
			break;
		case VA_BOTTOM:
		case VA_TEXT_BOTTOM:
			va = -baseline + line_h - node->box->em * 0.2;
			break;
		}

		if (node->type == FLOW_IMAGE)
			node->y = y + baseline - node->h;
		else
			node->y = y + baseline + va;
	}

	fz_free(ctx, reorder);
}

static void find_accumulated_margins(fz_context *ctx, fz_html *box, float *w, float *h)
{
	while (box)
	{
		/* TODO: take into account collapsed margins */
		*h += box->margin[T] + box->padding[T] + box->border[T];
		*h += box->margin[B] + box->padding[B] + box->border[B];
		*w += box->margin[L] + box->padding[L] + box->border[L];
		*w += box->margin[R] + box->padding[R] + box->border[R];
		box = box->up;
	}
}

static void flush_line(fz_context *ctx, fz_html *box, float page_h, float page_w, float line_w, int align, float indent, fz_html_flow *a, fz_html_flow *b)
{
	float avail, line_h, baseline;
	avail = page_h - fmodf(box->y + box->h, page_h);
	line_h = measure_line(a, b, &baseline);
	if (line_h > avail)
		box->h += avail;
	layout_line(ctx, indent, page_w, line_w, align, a, b, box, baseline, line_h);
	box->h += line_h;
}

static void layout_flow_inline(fz_context *ctx, fz_html *box, fz_html *top)
{
	while (box)
	{
		box->em = fz_from_css_number(box->style.font_size, top->em, top->em);
		if (box->down)
			layout_flow_inline(ctx, box->down, box);
		box = box->next;
	}
}

static void layout_flow(fz_context *ctx, fz_html *box, fz_html *top, float page_h, hb_buffer_t *hb_buf)
{
	fz_html_flow *node, *line, *candidate;
	float line_w, candidate_w, indent, break_w, nonbreak_w;
	int line_align, align;

	float em = box->em = fz_from_css_number(box->style.font_size, top->em, top->em);
	indent = box->is_first_flow ? fz_from_css_number(top->style.text_indent, em, top->w) : 0;
	align = top->style.text_align;

	if (box->markup_dir == FZ_BIDI_RTL)
	{
		if (align == TA_LEFT)
			align = TA_RIGHT;
		else if (align == TA_RIGHT)
			align = TA_LEFT;
	}

	box->x = top->x;
	box->y = top->y + top->h;
	box->w = top->w;
	box->h = 0;

	if (!box->flow_head)
		return;

	if (box->down)
		layout_flow_inline(ctx, box->down, box);

	for (node = box->flow_head; node; node = node->next)
	{
		if (node->type == FLOW_IMAGE)
		{
			float w = 0, h = 0;
			find_accumulated_margins(ctx, box, &w, &h);
			measure_image(ctx, node, top->w - w, page_h - h);
		}
		else
		{
			measure_string(ctx, node, hb_buf);
		}
	}

	node = box->flow_head;

	candidate = NULL;
	candidate_w = 0;
	line = node;
	line_w = indent;

	while (node)
	{
		switch (node->type)
		{
		default:
		case FLOW_WORD:
		case FLOW_IMAGE:
			nonbreak_w = break_w = node->w;
			break;

		case FLOW_SHYPHEN:
		case FLOW_SBREAK:
		case FLOW_SPACE:
			nonbreak_w = break_w = 0;

			/* Determine broken and unbroken widths of this node. */
			if (node->type == FLOW_SPACE)
				nonbreak_w = node->w;
			else if (node->type == FLOW_SHYPHEN)
				break_w = node->w;

			/* If the broken node fits, remember it. */
			/* Also remember it if we have no other candidate and need to break in desperation. */
			if (line_w + break_w <= box->w || !candidate)
			{
				candidate = node;
				candidate_w = line_w + break_w;
			}
			break;

		case FLOW_BREAK:
			nonbreak_w = break_w = 0;
			candidate = node;
			candidate_w = line_w;
			break;
		}

		/* The current node either does not fit or we saw a hard break. */
		/* Break the line if we have a candidate break point. */
		if (node->type == FLOW_BREAK || (line_w + nonbreak_w > box->w && candidate))
		{
			candidate->breaks_line = 1;
			if (candidate->type == FLOW_BREAK)
				line_align = (align == TA_JUSTIFY) ? TA_LEFT : align;
			else
				line_align = align;
			flush_line(ctx, box, page_h, box->w, candidate_w, line_align, indent, line, candidate->next);

			line = candidate->next;
			node = candidate->next;
			candidate = NULL;
			candidate_w = 0;
			indent = 0;
			line_w = 0;
		}
		else
		{
			line_w += nonbreak_w;
			node = node->next;
		}
	}

	if (line)
	{
		line_align = (align == TA_JUSTIFY) ? TA_LEFT : align;
		flush_line(ctx, box, page_h, box->w, line_w, line_align, indent, line, NULL);
	}
}

static float layout_block(fz_context *ctx, fz_html *box, fz_html *top, float page_h, float vertical, hb_buffer_t *hb_buf)
{
	fz_html *child;
	int first;

	fz_css_style *style = &box->style;
	float *margin = box->margin;
	float *border = box->border;
	float *padding = box->padding;

	float em = box->em = fz_from_css_number(style->font_size, top->em, top->em);

	margin[0] = fz_from_css_number(style->margin[0], em, top->w);
	margin[1] = fz_from_css_number(style->margin[1], em, top->w);
	margin[2] = fz_from_css_number(style->margin[2], em, top->w);
	margin[3] = fz_from_css_number(style->margin[3], em, top->w);

	padding[0] = fz_from_css_number(style->padding[0], em, top->w);
	padding[1] = fz_from_css_number(style->padding[1], em, top->w);
	padding[2] = fz_from_css_number(style->padding[2], em, top->w);
	padding[3] = fz_from_css_number(style->padding[3], em, top->w);

	border[0] = style->border_style[0] ? fz_from_css_number(style->border_width[0], em, top->w) : 0;
	border[1] = style->border_style[1] ? fz_from_css_number(style->border_width[1], em, top->w) : 0;
	border[2] = style->border_style[2] ? fz_from_css_number(style->border_width[2], em, top->w) : 0;
	border[3] = style->border_style[3] ? fz_from_css_number(style->border_width[3], em, top->w) : 0;

	box->x = top->x + margin[L] + border[L] + padding[L];
	box->w = top->w - (margin[L] + margin[R] + border[L] + border[R] + padding[L] + padding[R]);

	if (margin[T] > vertical)
		margin[T] -= vertical;
	else
		margin[T] = 0;

	if (padding[T] == 0 && border[T] == 0)
		vertical += margin[T];
	else
		vertical = 0;

	box->y = top->y + top->h + margin[T] + border[T] + padding[T];
	box->h = 0;

	first = 1;
	for (child = box->down; child; child = child->next)
	{
		if (child->type == BOX_BLOCK)
		{
			vertical = layout_block(ctx, child, box, page_h, vertical, hb_buf);
			if (first)
			{
				/* move collapsed parent/child top margins to parent */
				margin[T] += child->margin[T];
				box->y += child->margin[T];
				child->margin[T] = 0;
				first = 0;
			}
			box->h += child->h +
				child->padding[T] + child->padding[B] +
				child->border[T] + child->border[B] +
				child->margin[T] + child->margin[B];
		}
		else if (child->type == BOX_BREAK)
		{
			box->h += fz_from_css_number_scale(style->line_height, em, em, em);
			vertical = 0;
			first = 0;
		}
		else if (child->type == BOX_FLOW)
		{
			layout_flow(ctx, child, box, page_h, hb_buf);
			if (child->h > 0)
			{
				box->h += child->h;
				vertical = 0;
				first = 0;
			}
		}
	}

	/* reserve space for the list mark */
	if (box->list_item && box->h == 0)
	{
		box->h += fz_from_css_number_scale(style->line_height, em, em, em);
		vertical = 0;
	}

	if (box->h == 0)
	{
		if (margin[B] > vertical)
			margin[B] -= vertical;
		else
			margin[B] = 0;
	}
	else
	{
		box->h -= vertical;
		vertical = fz_max(margin[B], vertical);
		margin[B] = vertical;
	}

	return vertical;
}

static void draw_flow_box(fz_context *ctx, fz_html *box, float page_top, float page_bot, fz_device *dev, const fz_matrix *ctm, hb_buffer_t *hb_buf)
{
	fz_html_flow *node;
	fz_text *text;
	fz_matrix trm;
	float color[3];
	float prev_color[3];

	/* FIXME: HB_DIRECTION_TTB? */

	text = NULL;
	prev_color[0] = 0;
	prev_color[1] = 0;
	prev_color[2] = 0;

	for (node = box->flow_head; node; node = node->next)
	{
		fz_css_style *style = &node->box->style;

		if (node->type == FLOW_IMAGE)
		{
			if (node->y >= page_bot || node->y + node->h <= page_top)
				continue;
		}
		else
		{
			if (node->y > page_bot || node->y < page_top)
				continue;
		}

		if (node->type == FLOW_WORD || node->type == FLOW_SPACE || node->type == FLOW_SHYPHEN)
		{
			string_walker walker;
			const char *s;
			float x, y;

			if (node->type == FLOW_WORD && node->content.text == NULL)
				continue;
			if (node->type == FLOW_SPACE && node->breaks_line)
				continue;
			if (node->type == FLOW_SHYPHEN && !node->breaks_line)
				continue;
			if (style->visibility != V_VISIBLE)
				continue;

			color[0] = style->color.r / 255.0f;
			color[1] = style->color.g / 255.0f;
			color[2] = style->color.b / 255.0f;

			if (color[0] != prev_color[0] || color[1] != prev_color[1] || color[2] != prev_color[2])
			{
				if (text)
				{
					fz_fill_text(ctx, dev, text, ctm, fz_device_rgb(ctx), prev_color, 1);
					fz_drop_text(ctx, text);
					text = NULL;
				}
				prev_color[0] = color[0];
				prev_color[1] = color[1];
				prev_color[2] = color[2];
			}

			if (!text)
				text = fz_new_text(ctx);

			if (node->bidi_level & 1)
				x = node->x + node->w;
			else
				x = node->x;
			y = node->y;

			trm.a = node->box->em;
			trm.b = 0;
			trm.c = 0;
			trm.d = -node->box->em;
			trm.e = x;
			trm.f = y;

			s = get_node_text(ctx, node);
			init_string_walker(ctx, &walker, hb_buf, node->bidi_level & 1, style->font, node->script, s);
			while (walk_string(&walker))
			{
				float node_scale = node->box->em / walker.scale;
				unsigned int i;
				int c, k, n;

				/* Flatten advance and offset into offset array. */
				int x_advance = 0;
				int y_advance = 0;
				for (i = 0; i < walker.glyph_count; ++i)
				{
					walker.glyph_pos[i].x_offset += x_advance;
					walker.glyph_pos[i].y_offset += y_advance;
					x_advance += walker.glyph_pos[i].x_advance;
					y_advance += walker.glyph_pos[i].y_advance;
				}

				if (node->bidi_level & 1)
					x -= x_advance * node_scale;

				/* Walk characters to find glyph clusters */
				k = 0;
				while (walker.start + k < walker.end)
				{
					n = fz_chartorune(&c, walker.start + k);

					for (i = 0; i < walker.glyph_count; ++i)
					{
						if (walker.glyph_info[i].cluster == k)
						{
							trm.e = x + walker.glyph_pos[i].x_offset * node_scale;
							trm.f = y - walker.glyph_pos[i].y_offset * node_scale;
							fz_show_glyph(ctx, text, walker.font, &trm,
									walker.glyph_info[i].codepoint, c,
									0, node->bidi_level, box->markup_dir, node->markup_lang);
							c = -1; /* for subsequent glyphs in x-to-many mappings */
						}
					}

					/* no glyph found (many-to-many or many-to-one mapping) */
					if (c != -1)
					{
						fz_show_glyph(ctx, text, walker.font, &trm,
								-1, c,
								0, node->bidi_level, box->markup_dir, node->markup_lang);
					}

					k += n;
				}

				if ((node->bidi_level & 1) == 0)
					x += x_advance * node_scale;

				y += y_advance * node_scale;
			}
		}
		else if (node->type == FLOW_IMAGE)
		{
			if (text)
			{
				fz_fill_text(ctx, dev, text, ctm, fz_device_rgb(ctx), color, 1);
				fz_drop_text(ctx, text);
				text = NULL;
			}
			if (style->visibility == V_VISIBLE)
			{
				fz_matrix local_ctm = *ctm;
				fz_pre_translate(&local_ctm, node->x, node->y);
				fz_pre_scale(&local_ctm, node->w, node->h);
				fz_fill_image(ctx, dev, node->content.image, &local_ctm, 1);
			}
		}
	}

	if (text)
	{
		fz_fill_text(ctx, dev, text, ctm, fz_device_rgb(ctx), color, 1);
		fz_drop_text(ctx, text);
		text = NULL;
	}
}

static void draw_rect(fz_context *ctx, fz_device *dev, const fz_matrix *ctm, fz_css_color color, float x0, float y0, float x1, float y1)
{
	if (color.a > 0)
	{
		float rgb[3];

		fz_path *path = fz_new_path(ctx);

		fz_moveto(ctx, path, x0, y0);
		fz_lineto(ctx, path, x1, y0);
		fz_lineto(ctx, path, x1, y1);
		fz_lineto(ctx, path, x0, y1);
		fz_closepath(ctx, path);

		rgb[0] = color.r / 255.0f;
		rgb[1] = color.g / 255.0f;
		rgb[2] = color.b / 255.0f;

		fz_fill_path(ctx, dev, path, 0, ctm, fz_device_rgb(ctx), rgb, color.a / 255.0f);

		fz_drop_path(ctx, path);
	}
}

static const char *roman_uc[3][10] = {
	{ "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX" },
	{ "", "X", "XX", "XXX", "XL", "L", "LX", "LXX", "LXXX", "XC" },
	{ "", "C", "CC", "CCC", "CD", "D", "DC", "DCC", "DCCC", "CM" },
};

static const char *roman_lc[3][10] = {
	{ "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix" },
	{ "", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc" },
	{ "", "c", "cc", "ccc", "cd", "d", "dc", "dcc", "dccc", "cm" },
};

static void format_roman_number(fz_context *ctx, char *buf, int size, int n, const char *sym[3][10], const char *sym_m)
{
	int I = n % 10;
	int X = (n / 10) % 10;
	int C = (n / 100) % 10;
	int M = (n / 1000);

	fz_strlcpy(buf, "", size);
	while (M--)
		fz_strlcat(buf, sym_m, size);
	fz_strlcat(buf, sym[2][C], size);
	fz_strlcat(buf, sym[1][X], size);
	fz_strlcat(buf, sym[0][I], size);
	fz_strlcat(buf, ". ", size);
}

static void format_alpha_number(fz_context *ctx, char *buf, int size, int n, int alpha, int omega)
{
	int base = omega - alpha + 1;
	int tmp[40];
	int i, c;

	if (alpha > 256) /* to skip final-s for greek */
		--base;

	/* Bijective base-26 (base-24 for greek) numeration */
	i = 0;
	while (n > 0)
	{
		--n;
		c = n % base + alpha;
		if (alpha > 256 && c > alpha + 16) /* skip final-s for greek */
			++c;
		tmp[i++] = c;
		n /= base;
	}

	while (i > 0)
		buf += fz_runetochar(buf, tmp[--i]);
	*buf++ = '.';
	*buf++ = ' ';
	*buf = 0;
}

static void format_list_number(fz_context *ctx, int type, int x, char *buf, int size)
{
	switch (type)
	{
	case LST_NONE: fz_strlcpy(buf, "", size); break;
	case LST_DISC: fz_strlcpy(buf, "\342\227\217  ", size); break; /* U+25CF BLACK CIRCLE */
	case LST_CIRCLE: fz_strlcpy(buf, "\342\227\213  ", size); break; /* U+25CB WHITE CIRCLE */
	case LST_SQUARE: fz_strlcpy(buf, "\342\226\240  ", size); break; /* U+25A0 BLACK SQUARE */
	default:
	case LST_DECIMAL: fz_snprintf(buf, size, "%d. ", x); break;
	case LST_DECIMAL_ZERO: fz_snprintf(buf, size, "%02d. ", x); break;
	case LST_LC_ROMAN: format_roman_number(ctx, buf, size, x, roman_lc, "m"); break;
	case LST_UC_ROMAN: format_roman_number(ctx, buf, size, x, roman_uc, "M"); break;
	case LST_LC_ALPHA: format_alpha_number(ctx, buf, size, x, 'a', 'z'); break;
	case LST_UC_ALPHA: format_alpha_number(ctx, buf, size, x, 'A', 'Z'); break;
	case LST_LC_LATIN: format_alpha_number(ctx, buf, size, x, 'a', 'z'); break;
	case LST_UC_LATIN: format_alpha_number(ctx, buf, size, x, 'A', 'Z'); break;
	case LST_LC_GREEK: format_alpha_number(ctx, buf, size, x, 0x03B1, 0x03C9); break;
	case LST_UC_GREEK: format_alpha_number(ctx, buf, size, x, 0x0391, 0x03A9); break;
	}
}

static fz_html_flow *find_list_mark_anchor(fz_context *ctx, fz_html *box)
{
	/* find first flow node in <li> tag */
	while (box)
	{
		if (box->type == BOX_FLOW)
			return box->flow_head;
		box = box->down;
	}
	return NULL;
}

static void draw_list_mark(fz_context *ctx, fz_html *box, float page_top, float page_bot, fz_device *dev, const fz_matrix *ctm, int n)
{
	fz_font *font;
	fz_text *text;
	fz_matrix trm;
	fz_html_flow *line;
	float y, w;
	float color[3];
	const char *s;
	char buf[40];
	int c, g;

	fz_scale(&trm, box->em, -box->em);

	text = fz_new_text(ctx);

	line = find_list_mark_anchor(ctx, box);
	if (line)
	{
		y = line->y;
	}
	else
	{
		float h = fz_from_css_number_scale(box->style.line_height, box->em, box->em, box->em);
		float a = box->em * 0.8;
		float d = box->em * 0.2;
		if (a + d > h)
			h = a + d;
		y = box->y + a + (h - a - d) / 2;
	}

	if (y > page_bot || y < page_top)
		return;

	format_list_number(ctx, box->style.list_style_type, n, buf, sizeof buf);

	s = buf;
	w = 0;
	while (*s)
	{
		s += fz_chartorune(&c, s);
		g = fz_encode_character_with_fallback(ctx, box->style.font, c, UCDN_SCRIPT_LATIN, &font);
		w += fz_advance_glyph(ctx, font, g, 0) * box->em;
	}

	s = buf;
	trm.e = box->x - w;
	trm.f = y;
	while (*s)
	{
		s += fz_chartorune(&c, s);
		g = fz_encode_character_with_fallback(ctx, box->style.font, c, UCDN_SCRIPT_LATIN, &font);
		fz_show_glyph(ctx, text, font, &trm, g, c, 0, 0, FZ_BIDI_NEUTRAL, FZ_LANG_UNSET);
		trm.e += fz_advance_glyph(ctx, font, g, 0) * box->em;
	}

	color[0] = box->style.color.r / 255.0f;
	color[1] = box->style.color.g / 255.0f;
	color[2] = box->style.color.b / 255.0f;

	fz_fill_text(ctx, dev, text, ctm, fz_device_rgb(ctx), color, 1);

	fz_drop_text(ctx, text);
}

static void draw_block_box(fz_context *ctx, fz_html *box, float page_top, float page_bot, fz_device *dev, const fz_matrix *ctm, hb_buffer_t *hb_buf)
{
	float x0, y0, x1, y1;

	float *border = box->border;
	float *padding = box->padding;

	x0 = box->x - padding[L];
	y0 = box->y - padding[T];
	x1 = box->x + box->w + padding[R];
	y1 = box->y + box->h + padding[B];

	if (y0 > page_bot || y1 < page_top)
		return;

	if (box->style.visibility == V_VISIBLE)
	{
		draw_rect(ctx, dev, ctm, box->style.background_color, x0, y0, x1, y1);

		if (border[T] > 0)
			draw_rect(ctx, dev, ctm, box->style.border_color[T], x0 - border[L], y0 - border[T], x1 + border[R], y0);
		if (border[B] > 0)
			draw_rect(ctx, dev, ctm, box->style.border_color[B], x0 - border[L], y1, x1 + border[R], y1 + border[B]);
		if (border[L] > 0)
			draw_rect(ctx, dev, ctm, box->style.border_color[L], x0 - border[L], y0 - border[T], x0, y1 + border[B]);
		if (border[R] > 0)
			draw_rect(ctx, dev, ctm, box->style.border_color[R], x1, y0 - border[T], x1 + border[R], y1 + border[B]);

		if (box->list_item)
			draw_list_mark(ctx, box, page_top, page_bot, dev, ctm, box->list_item);
	}

	for (box = box->down; box; box = box->next)
	{
		switch (box->type)
		{
		case BOX_BLOCK: draw_block_box(ctx, box, page_top, page_bot, dev, ctm, hb_buf); break;
		case BOX_FLOW: draw_flow_box(ctx, box, page_top, page_bot, dev, ctm, hb_buf); break;
		}
	}
}

void
fz_draw_html(fz_context *ctx, fz_device *dev, const fz_matrix *ctm, fz_html *box, float page_top, float page_bot)
{
	fz_matrix local_ctm = *ctm;
	hb_buffer_t *hb_buf = NULL;
	int unlocked = 0;

	fz_var(hb_buf);
	fz_var(unlocked);

	hb_lock(ctx);

	fz_try(ctx)
	{
		hb_buf = hb_buffer_create();
		hb_unlock(ctx);
		unlocked = 1;
		fz_pre_translate(&local_ctm, 0, -page_top);
		draw_block_box(ctx, box, page_top, page_bot, dev, &local_ctm, hb_buf);
	}
	fz_always(ctx)
	{
		if (unlocked)
			hb_lock(ctx);
		hb_buffer_destroy(hb_buf);
		hb_unlock(ctx);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static char *concat_text(fz_context *ctx, fz_xml *root)
{
	fz_xml *node;
	int i = 0, n = 1;
	char *s;
	for (node = fz_xml_down(root); node; node = fz_xml_next(node))
	{
		const char *text = fz_xml_text(node);
		n += text ? strlen(text) : 0;
	}
	s = fz_malloc(ctx, n);
	for (node = fz_xml_down(root); node; node = fz_xml_next(node))
	{
		const char *text = fz_xml_text(node);
		if (text)
		{
			n = strlen(text);
			memcpy(s+i, text, n);
			i += n;
		}
	}
	s[i] = 0;
	return s;
}

static fz_css_rule *
html_load_css(fz_context *ctx, fz_archive *zip, const char *base_uri, fz_css_rule *css, fz_xml *root)
{
	fz_xml *html, *head, *node;
	fz_buffer *buf;
	char path[2048];

	fz_var(buf);

	html = fz_xml_find(root, "html");
	head = fz_xml_find_down(html, "head");
	for (node = fz_xml_down(head); node; node = fz_xml_next(node))
	{
		if (fz_xml_is_tag(node, "link"))
		{
			char *rel = fz_xml_att(node, "rel");
			if (rel && !fz_strcasecmp(rel, "stylesheet"))
			{
				char *type = fz_xml_att(node, "type");
				if ((type && !strcmp(type, "text/css")) || !type)
				{
					char *href = fz_xml_att(node, "href");
					if (href)
					{
						fz_strlcpy(path, base_uri, sizeof path);
						fz_strlcat(path, "/", sizeof path);
						fz_strlcat(path, href, sizeof path);
						fz_urldecode(path);
						fz_cleanname(path);

						buf = NULL;
						fz_try(ctx)
						{
							buf = fz_read_archive_entry(ctx, zip, path);
							fz_write_buffer_byte(ctx, buf, 0);
							css = fz_parse_css(ctx, css, (char*)buf->data, path);
						}
						fz_always(ctx)
							fz_drop_buffer(ctx, buf);
						fz_catch(ctx)
							fz_warn(ctx, "ignoring stylesheet %s", path);
					}
				}
			}
		}
		else if (fz_xml_is_tag(node, "style"))
		{
			char *s = concat_text(ctx, node);
			fz_try(ctx)
				css = fz_parse_css(ctx, css, s, "<style>");
			fz_catch(ctx)
				fz_warn(ctx, "ignoring inline stylesheet");
			fz_free(ctx, s);
		}
	}
	return css;
}

static void indent(int n)
{
	while (n-- > 0)
		putchar('\t');
}

void
fz_print_css_style(fz_context *ctx, fz_css_style *style, int boxtype, int n)
{
	indent(n); printf("font_size %g%c\n", style->font_size.value, style->font_size.unit);
	indent(n); printf("font %s\n", style->font ? style->font->name : "NULL");
	indent(n); printf("width = %g%c;\n", style->width.value, style->width.unit);
	indent(n); printf("height = %g%c;\n", style->height.value, style->height.unit);
	if (boxtype == BOX_BLOCK)
	{
		indent(n); printf("margin %g%c ", style->margin[0].value, style->margin[0].unit);
		printf("%g%c ", style->margin[1].value, style->margin[1].unit);
		printf("%g%c ", style->margin[2].value, style->margin[2].unit);
		printf("%g%c\n", style->margin[3].value, style->margin[3].unit);
		indent(n); printf("padding %g%c ", style->padding[0].value, style->padding[0].unit);
		printf("%g%c ", style->padding[1].value, style->padding[1].unit);
		printf("%g%c ", style->padding[2].value, style->padding[2].unit);
		printf("%g%c\n", style->padding[3].value, style->padding[3].unit);
		indent(n); printf("border_width %g%c ", style->border_width[0].value, style->border_width[0].unit);
		printf("%g%c ", style->border_width[1].value, style->border_width[1].unit);
		printf("%g%c ", style->border_width[2].value, style->border_width[2].unit);
		printf("%g%c\n", style->border_width[3].value, style->border_width[3].unit);
		indent(n); printf("border_style %d %d %d %d\n",
				style->border_style[0], style->border_style[1],
				style->border_style[2], style->border_style[3]);
		indent(n); printf("text_indent %g%c\n", style->text_indent.value, style->text_indent.unit);
		indent(n); printf("white_space %d\n", style->white_space);
		indent(n); printf("text_align %d\n", style->text_align);
		indent(n); printf("list_style_type %d\n", style->list_style_type);
	}
	indent(n); printf("line_height %g%c\n", style->line_height.value, style->line_height.unit);
	indent(n); printf("vertical_align %d\n", style->vertical_align);
}

void
fz_print_html_flow(fz_context *ctx, fz_html_flow *flow, fz_html_flow *end)
{
	while (flow != end)
	{
		switch (flow->type)
		{
		case FLOW_WORD: printf("%s", flow->content.text); break;
		case FLOW_SPACE: printf("[ ]"); break;
		case FLOW_SBREAK: printf("[%%]"); break;
		case FLOW_SHYPHEN: printf("[-]"); break;
		case FLOW_BREAK: printf("[!]"); break;
		case FLOW_IMAGE: printf("<img>"); break;
		}
		flow = flow->next;
	}
}

void
fz_print_html(fz_context *ctx, fz_html *box, int pstyle, int level)
{
	while (box)
	{
		indent(level);
		switch (box->type)
		{
		case BOX_BLOCK: printf("block"); break;
		case BOX_BREAK: printf("break"); break;
		case BOX_FLOW: printf("flow"); break;
		case BOX_INLINE: printf("inline"); break;
		}

		if (box->list_item)
			printf(" list=%d", box->list_item);

		if (box->down || box->flow_head)
			printf(" {\n");
		else
			printf("\n");

		if (pstyle && !box->flow_head)
			fz_print_css_style(ctx, &box->style, box->type, level+1);

		fz_print_html(ctx, box->down, pstyle, level+1);

		if (box->flow_head)
		{
			indent(level+1);
			printf("\"");
			fz_print_html_flow(ctx, box->flow_head, NULL);
			printf("\"\n");
		}

		if (box->down || box->flow_head)
		{
			indent(level);
			printf("}\n");
		}

		box = box->next;
	}
}

void
fz_layout_html(fz_context *ctx, fz_html *box, float w, float h, float em)
{
	hb_buffer_t *hb_buf = NULL;
	int unlocked = 0;

	fz_var(hb_buf);
	fz_var(unlocked);

	hb_lock(ctx);

	fz_try(ctx)
	{
		hb_buf = hb_buffer_create();
		unlocked = 1;
		hb_unlock(ctx);

		box->em = em;
		box->w = w;
		box->h = 0;

		if (box->down)
		{
			layout_block(ctx, box->down, box, h, 0, hb_buf);
			box->h = box->down->h;
		}
	}
	fz_always(ctx)
	{
		if (unlocked)
			hb_lock(ctx);
		hb_buffer_destroy(hb_buf);
		hb_unlock(ctx);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

typedef struct
{
	uint32_t *data;
	size_t cap;
	size_t len;
} uni_buf;

typedef struct
{
	fz_context *ctx;
	fz_pool *pool;
	fz_html_flow *flow;
	uni_buf *buffer;
} bidi_data;

static void fragment_cb(const uint32_t *fragment,
			size_t fragment_len,
			int bidi_level,
			int script,
			void *arg)
{
	bidi_data *data = (bidi_data *)arg;
	size_t fragment_offset = fragment - data->buffer->data;

	/* We are guaranteed that fragmentOffset will be at the beginning
	 * of flow. */
	while (fragment_len > 0)
	{
		size_t len;

		if (data->flow->type == FLOW_SPACE)
		{
			len = 1;

		}
		else if (data->flow->type == FLOW_BREAK || data->flow->type == FLOW_SBREAK || data->flow->type == FLOW_SHYPHEN)
		{
			len = 0;
		}
		else
		{
			/* Must be text */
			len = fz_utflen(data->flow->content.text);
			if (len > fragment_len)
			{
				/* We need to split this flow box */
				(void)split_flow(data->ctx, data->pool, data->flow, fragment_len);
				len = fz_utflen(data->flow->content.text);
			}
		}

		/* This flow box is entirely contained within this fragment. */
		data->flow->bidi_level = bidi_level;
		data->flow->script = script;
		data->flow = data->flow->next;
		fragment_offset += len;
		fragment_len -= len;
	}
}

static void
detect_flow_directionality(fz_context *ctx, fz_pool *pool, uni_buf *buffer, fz_bidi_direction *bidi_dir, fz_html_flow *flow)
{
	fz_html_flow *end = flow;
	const char *text;
	bidi_data data;

	while (end)
	{
		int level = end->bidi_level;

		/* Gather the text from the flow up into a single buffer (at
		 * least, as much of it as has the same direction markup). */
		buffer->len = 0;
		while (end && (level & 1) == (end->bidi_level & 1))
		{
			size_t len;
			int broken = 0;

			switch (end->type)
			{
			case FLOW_WORD:
				len = fz_utflen(end->content.text);
				text = end->content.text;
				break;
			case FLOW_SPACE:
				len = 1;
				text = " ";
				break;
			case FLOW_SHYPHEN:
			case FLOW_SBREAK:
				len = 0;
				text = "";
				break;
			case FLOW_BREAK:
			case FLOW_IMAGE:
				broken = 1;
				break;
			}

			end = end->next;

			if (broken)
				break;

			/* Make sure the buffer is large enough */
			if (buffer->len + len > buffer->cap)
			{
				size_t newcap = buffer->cap * 2;
				if (newcap == 0)
					newcap = 128; /* Sensible small default */
				buffer->data = fz_resize_array(ctx, buffer->data, newcap, sizeof(uint32_t));
				buffer->cap = newcap;
			}

			/* Expand the utf8 text into Unicode and store it in the buffer */
			while (*text)
			{
				int rune;
				text += fz_chartorune(&rune, text);
				buffer->data[buffer->len++] = rune;
			}
		}

		/* Detect directionality for the buffer */
		data.ctx = ctx;
		data.pool = pool;
		data.flow = flow;
		data.buffer = buffer;
		fz_bidi_fragment_text(ctx, buffer->data, buffer->len, bidi_dir, &fragment_cb, &data, 0 /* Flags */);
	}
}

static void
detect_box_directionality(fz_context *ctx, fz_pool *pool, uni_buf *buffer, fz_html *box)
{
	while (box)
	{
		if (box->flow_head)
			detect_flow_directionality(ctx, pool, buffer, &box->markup_dir, box->flow_head);
		detect_box_directionality(ctx, pool, buffer, box->down);
		box = box->next;
	}
}

static void
detect_directionality(fz_context *ctx, fz_pool *pool, fz_html *box)
{
	uni_buf buffer = { NULL };

	fz_try(ctx)
		detect_box_directionality(ctx, pool, &buffer, box);
	fz_always(ctx)
		fz_free(ctx, buffer.data);
	fz_catch(ctx)
		fz_rethrow(ctx);
}

fz_html *
fz_parse_html(fz_context *ctx, fz_html_font_set *set, fz_archive *zip, const char *base_uri, fz_buffer *buf, const char *user_css)
{
	fz_xml *xml;
	fz_css_match match;
	fz_html *box;
	struct genstate g;

	g.pool = fz_new_pool(ctx);
	g.set = set;
	g.zip = zip;
	g.base_uri = base_uri;
	g.at_bol = 0;
	g.emit_white = 0;

	xml = fz_parse_xml(ctx, buf->data, buf->len, 1);

	g.css = fz_parse_css(ctx, NULL, default_css, "<default>");
	g.css = html_load_css(ctx, g.zip, g.base_uri, g.css, xml);
	if (user_css)
		g.css = fz_parse_css(ctx, g.css, user_css, "<user>");

	// print_rules(g.css);

	fz_add_css_font_faces(ctx, g.set, g.zip, g.base_uri, g.css); /* load @font-face fonts into font set */

	box = new_box(ctx, g.pool, DEFAULT_DIR);
	box->pool = g.pool;

	match.up = NULL;
	match.count = 0;
	fz_match_css_at_page(ctx, &match, g.css);
	fz_apply_css_style(ctx, g.set, &box->style, &match);
	// TODO: transfer page margins out of this hacky box

	generate_boxes(ctx, xml, box, &match, 0, DEFAULT_DIR, &g);

	fz_drop_css(ctx, g.css);
	fz_drop_xml(ctx, xml);

	detect_directionality(ctx, g.pool, box);

	return box;
}
