#include "mupdf/html.h"

static const char *inherit_list[] = {
	"color",
	"direction",
	"font-family",
	"font-size",
	"font-style",
	"font-variant",
	"font-weight",
	"letter-spacing",
	"line-height",
	"list-style-image",
	"list-style-position",
	"list-style-type",
	"orphans",
	"quotes",
	"text-align",
	"text-indent",
	"text-transform",
	"visibility",
	"white-space",
	"widows",
	"word-spacing",
};

static const char *border_width_kw[] = {
	"medium",
	"thick",
	"thin",
};

static const char *border_style_kw[] = {
	"dashed",
	"dotted",
	"double",
	"groove",
	"hidden",
	"inset",
	"none",
	"outset",
	"ridge",
	"solid",
};

static const char *color_kw[] = {
	"aqua",
	"black",
	"blue",
	"fuchsia",
	"gray",
	"green",
	"lime",
	"maroon",
	"navy",
	"olive",
	"orange",
	"purple",
	"red",
	"silver",
	"teal",
	"transparent",
	"white",
	"yellow",
};

static const char *list_style_type_kw[] = {
	"armenian",
	"circle",
	"decimal",
	"decimal-leading-zero",
	"disc",
	"georgian",
	"lower-alpha",
	"lower-greek",
	"lower-latin",
	"lower-roman",
	"none",
	"square",
	"upper-alpha",
	"upper-greek",
	"upper-latin",
	"upper-roman",
};

static const char *list_style_position_kw[] = {
	"inside",
	"outside",
};

static int
keyword_in_list(const char *name, const char **list, int n)
{
	int l = 0;
	int r = n - 1;
	while (l <= r)
	{
		int m = (l + r) >> 1;
		int c = strcmp(name, list[m]);
		if (c < 0)
			r = m - 1;
		else if (c > 0)
			l = m + 1;
		else
			return 1;
	}
	return 0;
}

/*
 * Compute specificity
 */

static int
count_condition_ids(fz_css_condition *cond)
{
	int n = 0;
	while (cond)
	{
		if (cond->type == '#')
			n ++;
		cond = cond->next;
	}
	return n;
}

static int
count_selector_ids(fz_css_selector *sel)
{
	int n = count_condition_ids(sel->cond);
	if (sel->left && sel->right)
	{
		n += count_selector_ids(sel->left);
		n += count_selector_ids(sel->right);
	}
	return n;
}

static int
count_condition_atts(fz_css_condition *cond)
{
	int n = 0;
	while (cond)
	{
		if (cond->type != '#' && cond->type != ':')
			n ++;
		cond = cond->next;
	}
	return n;
}

static int
count_selector_atts(fz_css_selector *sel)
{
	int n = count_condition_atts(sel->cond);
	if (sel->left && sel->right)
	{
		n += count_selector_atts(sel->left);
		n += count_selector_atts(sel->right);
	}
	return n;
}

static int
count_condition_names(fz_css_condition *cond)
{
	int n = 0;
	while (cond)
	{
		if (cond->type == ':')
			n ++;
		cond = cond->next;
	}
	return n;
}

static int
count_selector_names(fz_css_selector *sel)
{
	int n = count_condition_names(sel->cond);
	if (sel->left && sel->right)
	{
		n += count_selector_names(sel->left);
		n += count_selector_names(sel->right);
	}
	else if (sel->name)
	{
		n ++;
	}
	return n;
}

#define INLINE_SPECIFICITY 1000

static int
selector_specificity(fz_css_selector *sel)
{
	int b = count_selector_ids(sel);
	int c = count_selector_atts(sel);
	int d = count_selector_names(sel);
	return b * 100 + c * 10 + d;
}

/*
 * Selector matching
 */

static int
match_id_condition(fz_xml *node, const char *p)
{
	const char *s = fz_xml_att(node, "id");
	if (s && !strcmp(s, p))
		return 1;
	return 0;
}

static int
match_class_condition(fz_xml *node, const char *p)
{
	const char *s = fz_xml_att(node, "class");
	char buf[1024];
	if (s) {
		strcpy(buf, s);
		s = strtok(buf, " ");
		while (s) {
			if (!strcmp(s, p))
				return 1;
			s = strtok(NULL, " ");
		}
	}
	return 0;
}

static int
match_condition(fz_css_condition *cond, fz_xml *node)
{
	if (!cond)
		return 1;

	switch (cond->type) {
	default: return 0;
	case ':': return 0; /* don't support pseudo-classes */
	case '#': if (!match_id_condition(node, cond->val)) return 0; break;
	case '.': if (!match_class_condition(node, cond->val)) return 0; break;
	}

	return match_condition(cond->next, node);
}

static int
match_selector(fz_css_selector *sel, fz_xml *node)
{
	if (!node)
		return 0;

	if (sel->combine)
	{
		/* descendant */
		if (sel->combine == ' ')
		{
			fz_xml *parent = fz_xml_up(node);
			while (parent)
			{
				if (match_selector(sel->left, parent))
					if (match_selector(sel->right, node))
						return 1;
				parent = fz_xml_up(parent);
			}
			return 0;
		}

		/* child */
		if (sel->combine == '>')
		{
			fz_xml *parent = fz_xml_up(node);
			if (!parent)
				return 0;
			if (!match_selector(sel->left, parent))
				return 0;
			if (!match_selector(sel->right, node))
				return 0;
		}

		/* adjacent */
		if (sel->combine == '+')
		{
			fz_xml *prev = fz_xml_prev(node);
			while (prev && !fz_xml_tag(prev))
				prev = fz_xml_prev(prev);
			if (!prev)
				return 0;
			if (!fz_xml_tag(prev))
				return 0;
			if (!match_selector(sel->left, prev))
				return 0;
			if (!match_selector(sel->right, node))
				return 0;
		}
	}

	if (sel->name)
	{
		if (strcmp(sel->name, fz_xml_tag(node)))
			return 0;
	}

	if (sel->cond)
	{
		if (!match_condition(sel->cond, node))
			return 0;
	}

	return 1;
}

/*
 * Annotating nodes with properties and expanding shorthand forms.
 */

static int
count_values(fz_css_value *value)
{
	int n = 0;
	while (value)
	{
		n++;
		value = value->next;
	}
	return n;
}

static void add_property(fz_css_match *match, const char *name, fz_css_value *value, int spec);

static void
add_shorthand_trbl(fz_css_match *match, fz_css_value *value, int spec,
	const char *name_t, const char *name_r, const char *name_b, const char *name_l)
{
	int n = count_values(value);

	if (n == 1)
	{
		add_property(match, name_t, value, spec);
		add_property(match, name_r, value, spec);
		add_property(match, name_b, value, spec);
		add_property(match, name_l, value, spec);
	}

	if (n == 2)
	{
		fz_css_value *a = value;
		fz_css_value *b = value->next;

		add_property(match, name_t, a, spec);
		add_property(match, name_r, b, spec);
		add_property(match, name_b, a, spec);
		add_property(match, name_l, b, spec);
	}

	if (n == 3)
	{
		fz_css_value *a = value;
		fz_css_value *b = value->next;
		fz_css_value *c = value->next->next;

		add_property(match, name_t, a, spec);
		add_property(match, name_r, b, spec);
		add_property(match, name_b, c, spec);
		add_property(match, name_l, b, spec);
	}

	if (n == 4)
	{
		fz_css_value *a = value;
		fz_css_value *b = value->next;
		fz_css_value *c = value->next->next;
		fz_css_value *d = value->next->next->next;

		add_property(match, name_t, a, spec);
		add_property(match, name_r, b, spec);
		add_property(match, name_b, c, spec);
		add_property(match, name_l, d, spec);
	}
}

static void
add_shorthand_margin(fz_css_match *match, fz_css_value *value, int spec)
{
	add_shorthand_trbl(match, value, spec,
		"margin-top", "margin-right", "margin-bottom", "margin-left");
}

static void
add_shorthand_padding(fz_css_match *match, fz_css_value *value, int spec)
{
	add_shorthand_trbl(match, value, spec,
		"padding-top", "padding-right", "padding-bottom", "padding-left");
}

static void
add_shorthand_border_width(fz_css_match *match, fz_css_value *value, int spec)
{
	add_shorthand_trbl(match, value, spec,
		"border-top-width", "border-right-width", "border-bottom-width", "border-left-width");
}

static void
add_shorthand_border_color(fz_css_match *match, fz_css_value *value, int spec)
{
	add_shorthand_trbl(match, value, spec,
		"border-top-color", "border-right-color", "border-bottom-color", "border-left-color");
}

static void
add_shorthand_border_style(fz_css_match *match, fz_css_value *value, int spec)
{
	add_shorthand_trbl(match, value, spec,
		"border-top-style", "border-right-style", "border-bottom-style", "border-left-style");
}

static void
add_shorthand_border(fz_css_match *match, fz_css_value *value, int spec, int T, int R, int B, int L)
{
	while (value)
	{
		if (value->type == CSS_HASH)
		{
			if (T) add_property(match, "border-top-color", value, spec);
			if (R) add_property(match, "border-right-color", value, spec);
			if (B) add_property(match, "border-bottom-color", value, spec);
			if (L) add_property(match, "border-left-color", value, spec);
		}
		else if (value->type == CSS_KEYWORD)
		{
			if (keyword_in_list(value->data, border_width_kw, nelem(border_width_kw)))
			{
				if (T) add_property(match, "border-top-width", value, spec);
				if (R) add_property(match, "border-right-width", value, spec);
				if (B) add_property(match, "border-bottom-width", value, spec);
				if (L) add_property(match, "border-left-width", value, spec);
			}
			else if (keyword_in_list(value->data, border_style_kw, nelem(border_style_kw)))
			{
				if (T) add_property(match, "border-top-style", value, spec);
				if (R) add_property(match, "border-right-style", value, spec);
				if (B) add_property(match, "border-bottom-style", value, spec);
				if (L) add_property(match, "border-left-style", value, spec);
			}
			else if (keyword_in_list(value->data, color_kw, nelem(color_kw)))
			{
				if (T) add_property(match, "border-top-color", value, spec);
				if (R) add_property(match, "border-right-color", value, spec);
				if (B) add_property(match, "border-bottom-color", value, spec);
				if (L) add_property(match, "border-left-color", value, spec);
			}
		}
		else
		{
			if (T) add_property(match, "border-top-width", value, spec);
			if (R) add_property(match, "border-right-width", value, spec);
			if (B) add_property(match, "border-bottom-width", value, spec);
			if (L) add_property(match, "border-left-width", value, spec);
		}
		value = value->next;
	}
}

static void
add_shorthand_list_style(fz_css_match *match, fz_css_value *value, int spec)
{
	while (value)
	{
		if (value->type == CSS_KEYWORD)
		{
			if (keyword_in_list(value->data, list_style_type_kw, nelem(list_style_type_kw)))
			{
				add_property(match, "list-style-type", value, spec);
			}
			else if (keyword_in_list(value->data, list_style_position_kw, nelem(list_style_position_kw)))
			{
				add_property(match, "list-style-position", value, spec);
			}
		}
		value = value->next;
	}
}

static void
add_property(fz_css_match *match, const char *name, fz_css_value *value, int spec)
{
	int i;

	if (!strcmp(name, "margin"))
	{
		add_shorthand_margin(match, value, spec);
		return;
	}
	if (!strcmp(name, "padding"))
	{
		add_shorthand_padding(match, value, spec);
		return;
	}
	if (!strcmp(name, "border-width"))
	{
		add_shorthand_border_width(match, value, spec);
		return;
	}
	if (!strcmp(name, "border-color"))
	{
		add_shorthand_border_color(match, value, spec);
		return;
	}
	if (!strcmp(name, "border-style"))
	{
		add_shorthand_border_style(match, value, spec);
		return;
	}
	if (!strcmp(name, "border"))
	{
		add_shorthand_border(match, value, spec, 1, 1, 1, 1);
		return;
	}
	if (!strcmp(name, "border-top"))
	{
		add_shorthand_border(match, value, spec, 1, 0, 0, 0);
		return;
	}
	if (!strcmp(name, "border-right"))
	{
		add_shorthand_border(match, value, spec, 0, 1, 0, 0);
		return;
	}
	if (!strcmp(name, "border-bottom"))
	{
		add_shorthand_border(match, value, spec, 0, 0, 1, 0);
		return;
	}
	if (!strcmp(name, "border-left"))
	{
		add_shorthand_border(match, value, spec, 0, 0, 0, 1);
		return;
	}
	if (!strcmp(name, "list-style"))
	{
		add_shorthand_list_style(match, value, spec);
		return;
	}

	/* shorthand expansions: */
	/* TODO: border-color */
	/* TODO: border-style */
	/* TODO: font */
	/* TODO: list-style */
	/* TODO: background */

	for (i = 0; i < match->count; ++i)
	{
		if (!strcmp(match->prop[i].name, name))
		{
			if (match->prop[i].spec <= spec)
			{
				match->prop[i].value = value;
				match->prop[i].spec = spec;
			}
			return;
		}
	}

	if (match->count + 1 >= nelem(match->prop))
	{
		// fz_warn(ctx, "too many css properties");
		return;
	}

	match->prop[match->count].name = name;
	match->prop[match->count].value = value;
	match->prop[match->count].spec = spec;
	++match->count;
}

void
fz_match_css(fz_context *ctx, fz_css_match *match, fz_css_rule *css, fz_xml *node)
{
	fz_css_rule *rule;
	fz_css_selector *sel;
	fz_css_property *prop, *head, *tail;
	const char *s;

	for (rule = css; rule; rule = rule->next)
	{
		sel = rule->selector;
		while (sel)
		{
			if (match_selector(sel, node))
			{
				for (prop = rule->declaration; prop; prop = prop->next)
					add_property(match, prop->name, prop->value, selector_specificity(sel));
				break;
			}
			sel = sel->next;
		}
	}

	s = fz_xml_att(node, "style");
	if (s)
	{
		fz_try(ctx)
		{
			head = tail = prop = fz_parse_css_properties(ctx, s);
			while (prop)
			{
				add_property(match, prop->name, prop->value, INLINE_SPECIFICITY);
				tail = prop;
				prop = prop->next;
			}
			if (tail)
				tail->next = css->garbage;
			css->garbage = head;
		}
		fz_catch(ctx)
		{
			fz_warn(ctx, "ignoring style attribute");
		}
	}
}

static fz_css_value *
value_from_raw_property(fz_css_match *match, const char *name)
{
	int i;
	for (i = 0; i < match->count; ++i)
		if (!strcmp(match->prop[i].name, name))
			return match->prop[i].value;
	return NULL;
}

static fz_css_value *
value_from_property(fz_css_match *match, const char *name)
{
	fz_css_value *value;

	value = value_from_raw_property(match, name);
	if (match->up)
	{
		if (value && !strcmp(value->data, "inherit"))
			return value_from_property(match->up, name);
		if (!value && keyword_in_list(name, inherit_list, nelem(inherit_list)))
			return value_from_property(match->up, name);
	}
	return value;
}

static const char *
string_from_property(fz_css_match *match, const char *name, const char *initial)
{
	fz_css_value *value;
	value = value_from_property(match, name);
	if (!value)
		return initial;
	return value->data;
}

static fz_css_number
make_number(float v, int u)
{
	fz_css_number n;
	n.value = v;
	n.unit = u;
	return n;
}

static fz_css_number
number_from_value(fz_css_value *value, float initial, int initial_unit)
{
	char *p;

	if (!value)
		return make_number(initial, initial_unit);

	if (value->type == CSS_PERCENT)
		return make_number((float)fz_strtod(value->data, NULL), N_PERCENT);

	if (value->type == CSS_NUMBER)
		return make_number((float)fz_strtod(value->data, NULL), N_NUMBER);

	if (value->type == CSS_LENGTH)
	{
		float x = (float)fz_strtod(value->data, &p);

		if (p[0] == 'e' && p[1] == 'm')
			return make_number(x, N_SCALE);
		if (p[0] == 'e' && p[1] == 'x')
			return make_number(x / 2, N_SCALE);

		if (p[0] == 'i' && p[1] == 'n')
			return make_number(x * 72, N_NUMBER);
		if (p[0] == 'c' && p[1] == 'm')
			return make_number(x * 7200 / 254, N_NUMBER);
		if (p[0] == 'm' && p[1] == 'm')
			return make_number(x * 720 / 254, N_NUMBER);
		if (p[0] == 'p' && p[1] == 'c')
			return make_number(x * 12, N_NUMBER);

		if (p[0] == 'p' && p[1] == 't')
			return make_number(x, N_NUMBER);
		if (p[0] == 'p' && p[1] == 'x')
			return make_number(x, N_NUMBER);

		return make_number(x, N_NUMBER);
	}

	if (value->type == CSS_KEYWORD)
	{
		if (!strcmp(value->data, "auto"))
			return make_number(0, N_AUTO);
	}

	return make_number(initial, initial_unit);
}

static fz_css_number
number_from_property(fz_css_match *match, const char *property, float initial, int initial_unit)
{
	return number_from_value(value_from_property(match, property), initial, initial_unit);
}

static fz_css_number
border_width_from_property(fz_css_match *match, const char *property)
{
	fz_css_value *value = value_from_property(match, property);
	if (value)
	{
		if (!strcmp(value->data, "thin"))
			return make_number(1, N_NUMBER);
		if (!strcmp(value->data, "medium"))
			return make_number(2, N_NUMBER);
		if (!strcmp(value->data, "thick"))
			return make_number(4, N_NUMBER);
		return number_from_value(value, 0, N_NUMBER);
	}
	return make_number(2, N_NUMBER); /* initial: 'medium' */
}

static int
border_style_from_property(fz_css_match *match, const char *property)
{
	fz_css_value *value = value_from_property(match, property);
	if (value)
	{
		if (!strcmp(value->data, "none")) return BS_NONE;
		else if (!strcmp(value->data, "hidden")) return BS_NONE;
		else if (!strcmp(value->data, "solid")) return BS_SOLID;
	}
	return BS_NONE;
}

float
fz_from_css_number(fz_css_number number, float em, float width)
{
	switch (number.unit) {
	default:
	case N_NUMBER: return number.value;
	case N_SCALE: return number.value * em;
	case N_PERCENT: return number.value * 0.01 * width;
	case N_AUTO: return width;
	}
}

float
fz_from_css_number_scale(fz_css_number number, float scale, float em, float width)
{
	switch (number.unit) {
	default:
	case N_NUMBER: return number.value * scale;
	case N_SCALE: return number.value * em;
	case N_PERCENT: return number.value * 0.01 * width;
	case N_AUTO: return width;
	}
}

static fz_css_color
make_color(int r, int g, int b, int a)
{
	fz_css_color c;
	c.r = r < 0 ? 0 : r > 255 ? 255 : r;
	c.g = g < 0 ? 0 : g > 255 ? 255 : g;
	c.b = b < 0 ? 0 : b > 255 ? 255 : b;
	c.a = a < 0 ? 0 : a > 255 ? 255 : a;
	return c;
}

static int tohex(int c)
{
	if (c - '0' < 10)
		return c - '0';
	return (c | 32) - 'a' + 10;
}

static fz_css_color
color_from_value(fz_css_value *value, fz_css_color initial)
{
	if (!value)
		return initial;

	if (value->type == CSS_HASH)
	{
		int r, g, b, n;
		n = strlen(value->data);
		if (n == 3)
		{
			r = tohex(value->data[0]) * 16 + tohex(value->data[0]);
			g = tohex(value->data[1]) * 16 + tohex(value->data[1]);
			b = tohex(value->data[2]) * 16 + tohex(value->data[2]);
		}
		else if (n == 6)
		{
			r = tohex(value->data[0]) * 16 + tohex(value->data[1]);
			g = tohex(value->data[2]) * 16 + tohex(value->data[3]);
			b = tohex(value->data[4]) * 16 + tohex(value->data[5]);
		}
		else
		{
			r = g = b = 0;
		}
		return make_color(r, g, b, 255);
	}

	if (value->type == '(' && !strcmp(value->data, "rgb"))
	{
		fz_css_value *vr, *vg, *vb;
		int r, g, b;
		vr = value->args;
		vg = vr && vr->next ? vr->next->next : NULL; /* skip the ',' nodes */
		vb = vg && vg->next ? vg->next->next : NULL; /* skip the ',' nodes */
		r = fz_from_css_number(number_from_value(vr, 0, N_NUMBER), 255, 255);
		g = fz_from_css_number(number_from_value(vg, 0, N_NUMBER), 255, 255);
		b = fz_from_css_number(number_from_value(vb, 0, N_NUMBER), 255, 255);
		return make_color(r, g, b, 255);
	}

	if (value->type == CSS_KEYWORD)
	{
		if (!strcmp(value->data, "transparent"))
			return make_color(0, 0, 0, 0);
		if (!strcmp(value->data, "maroon"))
			return make_color(0x80, 0x00, 0x00, 255);
		if (!strcmp(value->data, "red"))
			return make_color(0xFF, 0x00, 0x00, 255);
		if (!strcmp(value->data, "orange"))
			return make_color(0xFF, 0xA5, 0x00, 255);
		if (!strcmp(value->data, "yellow"))
			return make_color(0xFF, 0xFF, 0x00, 255);
		if (!strcmp(value->data, "olive"))
			return make_color(0x80, 0x80, 0x00, 255);
		if (!strcmp(value->data, "purple"))
			return make_color(0x80, 0x00, 0x80, 255);
		if (!strcmp(value->data, "fuchsia"))
			return make_color(0xFF, 0x00, 0xFF, 255);
		if (!strcmp(value->data, "white"))
			return make_color(0xFF, 0xFF, 0xFF, 255);
		if (!strcmp(value->data, "lime"))
			return make_color(0x00, 0xFF, 0x00, 255);
		if (!strcmp(value->data, "green"))
			return make_color(0x00, 0x80, 0x00, 255);
		if (!strcmp(value->data, "navy"))
			return make_color(0x00, 0x00, 0x80, 255);
		if (!strcmp(value->data, "blue"))
			return make_color(0x00, 0x00, 0xFF, 255);
		if (!strcmp(value->data, "aqua"))
			return make_color(0x00, 0xFF, 0xFF, 255);
		if (!strcmp(value->data, "teal"))
			return make_color(0x00, 0x80, 0x80, 255);
		if (!strcmp(value->data, "black"))
			return make_color(0x00, 0x00, 0x00, 255);
		if (!strcmp(value->data, "silver"))
			return make_color(0xC0, 0xC0, 0xC0, 255);
		if (!strcmp(value->data, "gray"))
			return make_color(0x80, 0x80, 0x80, 255);
		return make_color(0, 0, 0, 255);
	}
	return initial;
}

static fz_css_color
color_from_property(fz_css_match *match, const char *property, fz_css_color initial)
{
	return color_from_value(value_from_property(match, property), initial);
}

int
fz_get_css_match_display(fz_css_match *match)
{
	fz_css_value *value = value_from_property(match, "display");
	if (value)
	{
		if (!strcmp(value->data, "none"))
			return DIS_NONE;
		if (!strcmp(value->data, "inline"))
			return DIS_INLINE;
		if (!strcmp(value->data, "block"))
			return DIS_BLOCK;
		if (!strcmp(value->data, "list-item"))
			return DIS_LIST_ITEM;
		if (!strcmp(value->data, "inline-block"))
			return DIS_INLINE_BLOCK;
	}
	return DIS_INLINE;
}

static int
white_space_from_property(fz_css_match *match)
{
	fz_css_value *value = value_from_property(match, "white-space");
	if (value)
	{
		if (!strcmp(value->data, "normal")) return WS_NORMAL;
		else if (!strcmp(value->data, "pre")) return WS_PRE;
		else if (!strcmp(value->data, "nowrap")) return WS_NOWRAP;
		else if (!strcmp(value->data, "pre-wrap")) return WS_PRE_WRAP;
		else if (!strcmp(value->data, "pre-line")) return WS_PRE_LINE;
	}
	return WS_NORMAL;
}

void
fz_default_css_style(fz_context *ctx, fz_css_style *style)
{
	memset(style, 0, sizeof *style);
	style->text_align = TA_LEFT;
	style->vertical_align = VA_BASELINE;
	style->white_space = WS_NORMAL;
	style->list_style_type = LST_DISC;
	style->font_size = make_number(1, N_SCALE);
	style->width = make_number(0, N_AUTO);
	style->height = make_number(0, N_AUTO);
}

void
fz_apply_css_style(fz_context *ctx, fz_html_font_set *set, fz_css_style *style, fz_css_match *match)
{
	fz_css_value *value;

	fz_css_color black = { 0, 0, 0, 255 };
	fz_css_color transparent = { 0, 0, 0, 0 };

	fz_default_css_style(ctx, style);

	style->white_space = white_space_from_property(match);

	value = value_from_property(match, "text-align");
	if (value)
	{
		if (!strcmp(value->data, "left")) style->text_align = TA_LEFT;
		else if (!strcmp(value->data, "right")) style->text_align = TA_RIGHT;
		else if (!strcmp(value->data, "center")) style->text_align = TA_CENTER;
		else if (!strcmp(value->data, "justify")) style->text_align = TA_JUSTIFY;
	}

	value = value_from_property(match, "vertical-align");
	if (value)
	{
		if (!strcmp(value->data, "baseline")) style->vertical_align = VA_BASELINE;
		else if (!strcmp(value->data, "sub")) style->vertical_align = VA_SUB;
		else if (!strcmp(value->data, "super")) style->vertical_align = VA_SUPER;
		else if (!strcmp(value->data, "top")) style->vertical_align = VA_TOP;
		else if (!strcmp(value->data, "bottom")) style->vertical_align = VA_BOTTOM;
	}

	value = value_from_property(match, "font-size");
	if (value)
	{
		if (!strcmp(value->data, "xx-large")) style->font_size = make_number(1.73f, N_SCALE);
		else if (!strcmp(value->data, "x-large")) style->font_size = make_number(1.44f, N_SCALE);
		else if (!strcmp(value->data, "large")) style->font_size = make_number(1.2f, N_SCALE);
		else if (!strcmp(value->data, "medium")) style->font_size = make_number(1.0f, N_SCALE);
		else if (!strcmp(value->data, "small")) style->font_size = make_number(0.83f, N_SCALE);
		else if (!strcmp(value->data, "x-small")) style->font_size = make_number(0.69f, N_SCALE);
		else if (!strcmp(value->data, "xx-small")) style->font_size = make_number(0.69f, N_SCALE);
		else if (!strcmp(value->data, "larger")) style->font_size = make_number(1.2f, N_SCALE);
		else if (!strcmp(value->data, "smaller")) style->font_size = make_number(1/1.2f, N_SCALE);
		else style->font_size = number_from_value(value, 12, N_NUMBER);
	}
	else
	{
		style->font_size = make_number(1, N_SCALE);
	}

	value = value_from_property(match, "list-style-type");
	if (value)
	{
		if (!strcmp(value->data, "none")) style->list_style_type = LST_NONE;
		else if (!strcmp(value->data, "disc")) style->list_style_type = LST_DISC;
		else if (!strcmp(value->data, "circle")) style->list_style_type = LST_CIRCLE;
		else if (!strcmp(value->data, "square")) style->list_style_type = LST_SQUARE;
		else if (!strcmp(value->data, "decimal")) style->list_style_type = LST_DECIMAL;
		else if (!strcmp(value->data, "decimal-leading-zero")) style->list_style_type = LST_DECIMAL_ZERO;
		else if (!strcmp(value->data, "lower-roman")) style->list_style_type = LST_LC_ROMAN;
		else if (!strcmp(value->data, "upper-roman")) style->list_style_type = LST_UC_ROMAN;
		else if (!strcmp(value->data, "lower-greek")) style->list_style_type = LST_LC_GREEK;
		else if (!strcmp(value->data, "upper-greek")) style->list_style_type = LST_UC_GREEK;
		else if (!strcmp(value->data, "lower-latin")) style->list_style_type = LST_LC_LATIN;
		else if (!strcmp(value->data, "upper-latin")) style->list_style_type = LST_UC_LATIN;
		else if (!strcmp(value->data, "lower-alpha")) style->list_style_type = LST_LC_ALPHA;
		else if (!strcmp(value->data, "upper-alpha")) style->list_style_type = LST_UC_ALPHA;
		else if (!strcmp(value->data, "armenian")) style->list_style_type = LST_ARMENIAN;
		else if (!strcmp(value->data, "georgian")) style->list_style_type = LST_GEORGIAN;
	}

	style->line_height = number_from_property(match, "line-height", 1.2f, N_SCALE);

	style->text_indent = number_from_property(match, "text-indent", 0, N_NUMBER);

	style->width = number_from_property(match, "width", 0, N_AUTO);
	style->height = number_from_property(match, "height", 0, N_AUTO);

	style->margin[0] = number_from_property(match, "margin-top", 0, N_NUMBER);
	style->margin[1] = number_from_property(match, "margin-right", 0, N_NUMBER);
	style->margin[2] = number_from_property(match, "margin-bottom", 0, N_NUMBER);
	style->margin[3] = number_from_property(match, "margin-left", 0, N_NUMBER);

	style->padding[0] = number_from_property(match, "padding-top", 0, N_NUMBER);
	style->padding[1] = number_from_property(match, "padding-right", 0, N_NUMBER);
	style->padding[2] = number_from_property(match, "padding-bottom", 0, N_NUMBER);
	style->padding[3] = number_from_property(match, "padding-left", 0, N_NUMBER);

	style->color = color_from_property(match, "color", black);
	style->background_color = color_from_property(match, "background-color", transparent);

	style->border_style[0] = border_style_from_property(match, "border-top-style");
	style->border_style[1] = border_style_from_property(match, "border-right-style");
	style->border_style[2] = border_style_from_property(match, "border-bottom-style");
	style->border_style[3] = border_style_from_property(match, "border-left-style");

	style->border_color[0] = color_from_property(match, "border-top-color", style->color);
	style->border_color[1] = color_from_property(match, "border-right-color", style->color);
	style->border_color[2] = color_from_property(match, "border-bottom-color", style->color);
	style->border_color[3] = color_from_property(match, "border-left-color", style->color);

	style->border_width[0] = border_width_from_property(match, "border-top-width");
	style->border_width[1] = border_width_from_property(match, "border-right-width");
	style->border_width[2] = border_width_from_property(match, "border-bottom-width");
	style->border_width[3] = border_width_from_property(match, "border-left-width");

	{
		const char *font_family = string_from_property(match, "font-family", "serif");
		const char *font_variant = string_from_property(match, "font-variant", "normal");
		const char *font_style = string_from_property(match, "font-style", "normal");
		const char *font_weight = string_from_property(match, "font-weight", "normal");
		style->font = fz_load_html_font(ctx, set, font_family, font_variant, font_style, font_weight);
	}
}

/*
 * Pretty printing
 */

void
print_value(fz_css_value *val)
{
	printf("%s", val->data);
	if (val->args)
	{
		printf("(");
		print_value(val->args);
		printf(")");
	}
	if (val->next)
	{
		printf(" ");
		print_value(val->next);
	}
}

void
print_property(fz_css_property *prop)
{
	printf("\t%s: ", prop->name);
	print_value(prop->value);
	printf(" !%d;\n", prop->spec);
}

void
print_condition(fz_css_condition *cond)
{
	if (cond->type == '=')
		printf("[%s=%s]", cond->key, cond->val);
	else if (cond->type == '[')
		printf("[%s]", cond->key);
	else
		printf("%c%s", cond->type, cond->val);
	if (cond->next)
		print_condition(cond->next);
}

void
print_selector(fz_css_selector *sel)
{
	if (sel->combine)
	{
putchar('(');
		print_selector(sel->left);
		if (sel->combine == ' ')
			printf(" ");
		else
			printf(" %c ", sel->combine);
		print_selector(sel->right);
putchar(')');
	}
	else if (sel->name)
		printf("%s", sel->name);
	else
		printf("*");
	if (sel->cond)
	{
		print_condition(sel->cond);
	}
}

void
print_rule(fz_css_rule *rule)
{
	fz_css_selector *sel;
	fz_css_property *prop;

	for (sel = rule->selector; sel; sel = sel->next)
	{
		print_selector(sel);
		printf(" !%d", selector_specificity(sel));
		if (sel->next)
			printf(", ");
	}

	printf("\n{\n");
	for (prop = rule->declaration; prop; prop = prop->next)
	{
		print_property(prop);
	}
	printf("}\n");
}

void
print_rules(fz_css_rule *rule)
{
	while (rule)
	{
		print_rule(rule);
		rule = rule->next;
	}
}
