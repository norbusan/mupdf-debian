#include "mupdf/xps.h"

/* Quick parsing of document to find links. */

static void
xps_load_links_in_element(fz_context *ctx, xps_document *doc, const fz_matrix *ctm,
		char *base_uri, xps_resource *dict, fz_xml *node, fz_link **link);

static void
xps_add_link(fz_context *ctx, xps_document *doc, const fz_rect *area, char *base_uri, char *target_uri, fz_link **head)
{
	fz_link_dest dest;
	fz_link *link;

	memset(&dest, 0, sizeof dest);

	if (xps_url_is_remote(ctx, doc, target_uri))
	{
		dest.kind = FZ_LINK_URI;
		dest.ld.uri.uri = fz_strdup(ctx, target_uri);
	}
	else
	{
		dest.kind = FZ_LINK_GOTO;
		dest.ld.gotor.page = xps_lookup_link_target(ctx, doc, target_uri);
	}

	link = fz_new_link(ctx, area, dest);
	link->next = *head;
	*head = link;
}

static void
xps_load_links_in_path(fz_context *ctx, xps_document *doc, const fz_matrix *ctm,
		char *base_uri, xps_resource *dict, fz_xml *root, fz_link **link)
{
	char *navigate_uri_att = fz_xml_att(root, "FixedPage.NavigateUri");
	if (navigate_uri_att)
	{
		char *transform_att = fz_xml_att(root, "RenderTransform");
		fz_xml *transform_tag = fz_xml_down(fz_xml_find_down(root, "Path.RenderTransform"));

		char *data_att = fz_xml_att(root, "Data");
		fz_xml *data_tag = fz_xml_down(fz_xml_find_down(root, "Path.Data"));

		fz_path *path = NULL;
		int fill_rule;
		fz_matrix local_ctm;
		fz_rect area;

		xps_resolve_resource_reference(ctx, doc, dict, &data_att, &data_tag, NULL);
		xps_resolve_resource_reference(ctx, doc, dict, &transform_att, &transform_tag, NULL);

		xps_parse_transform(ctx, doc, transform_att, transform_tag, &local_ctm, ctm);

		if (data_att)
			path = xps_parse_abbreviated_geometry(ctx, doc, data_att, &fill_rule);
		else if (data_tag)
			path = xps_parse_path_geometry(ctx, doc, dict, data_tag, 0, &fill_rule);
		if (path)
		{
			fz_bound_path(ctx, path, NULL, &local_ctm, &area);
			fz_drop_path(ctx, path);
			xps_add_link(ctx, doc, &area, base_uri, navigate_uri_att, link);
		}
	}
}

static void
xps_load_links_in_glyphs(fz_context *ctx, xps_document *doc, const fz_matrix *ctm,
		char *base_uri, xps_resource *dict, fz_xml *root, fz_link **link)
{
	char *navigate_uri_att = fz_xml_att(root, "FixedPage.NavigateUri");
	if (navigate_uri_att)
	{
		char *transform_att = fz_xml_att(root, "RenderTransform");
		fz_xml *transform_tag = fz_xml_down(fz_xml_find_down(root, "Path.RenderTransform"));

		char *bidi_level_att = fz_xml_att(root, "BidiLevel");
		char *font_size_att = fz_xml_att(root, "FontRenderingEmSize");
		char *font_uri_att = fz_xml_att(root, "FontUri");
		char *origin_x_att = fz_xml_att(root, "OriginX");
		char *origin_y_att = fz_xml_att(root, "OriginY");
		char *is_sideways_att = fz_xml_att(root, "IsSideways");
		char *indices_att = fz_xml_att(root, "Indices");
		char *unicode_att = fz_xml_att(root, "UnicodeString");
		char *style_att = fz_xml_att(root, "StyleSimulations");

		int is_sideways = 0;
		int bidi_level = 0;
		fz_matrix local_ctm;
		fz_font *font;
		fz_text *text;
		fz_rect area;

		xps_resolve_resource_reference(ctx, doc, dict, &transform_att, &transform_tag, NULL);

		xps_parse_transform(ctx, doc, transform_att, transform_tag, &local_ctm, ctm);

		if (is_sideways_att)
			is_sideways = !strcmp(is_sideways_att, "true");
		if (bidi_level_att)
			bidi_level = atoi(bidi_level_att);

		font = xps_lookup_font(ctx, doc, base_uri, font_uri_att, style_att);
		text = xps_parse_glyphs_imp(ctx, doc, &local_ctm, font, fz_atof(font_size_att),
				fz_atof(origin_x_att), fz_atof(origin_y_att),
				is_sideways, bidi_level, indices_att, unicode_att);
		fz_bound_text(ctx, text, NULL, &local_ctm, &area);
		fz_drop_text(ctx, text);
		fz_drop_font(ctx, font);

		xps_add_link(ctx, doc, &area, base_uri, navigate_uri_att, link);
	}
}

static void
xps_load_links_in_canvas(fz_context *ctx, xps_document *doc, const fz_matrix *ctm,
		char *base_uri, xps_resource *dict, fz_xml *root, fz_link **link)
{
	xps_resource *new_dict = NULL;
	fz_matrix local_ctm;
	fz_xml *node;

	char *navigate_uri_att = fz_xml_att(root, "FixedPage.NavigateUri");
	char *transform_att = fz_xml_att(root, "RenderTransform");
	fz_xml *transform_tag = fz_xml_down(fz_xml_find_down(root, "Canvas.RenderTransform"));
	fz_xml *resource_tag = fz_xml_down(fz_xml_find_down(root, "Canvas.Resources"));

	if (resource_tag)
	{
		new_dict = xps_parse_resource_dictionary(ctx, doc, base_uri, resource_tag);
		if (new_dict)
		{
			new_dict->parent = dict;
			dict = new_dict;
		}
	}

	xps_resolve_resource_reference(ctx, doc, dict, &transform_att, &transform_tag, NULL);

	xps_parse_transform(ctx, doc, transform_att, transform_tag, &local_ctm, ctm);

	if (navigate_uri_att)
		fz_warn(ctx, "FixedPage.NavigateUri attribute on Canvas element");

	for (node = fz_xml_down(root); node; node = fz_xml_next(node))
		xps_load_links_in_element(ctx, doc, &local_ctm, base_uri, dict, node, link);

	if (new_dict)
		xps_drop_resource_dictionary(ctx, doc, new_dict);
}

static void
xps_load_links_in_element(fz_context *ctx, xps_document *doc, const fz_matrix *ctm, char *base_uri, xps_resource *dict, fz_xml *node, fz_link **link)
{
	if (fz_xml_is_tag(node, "Path"))
		xps_load_links_in_path(ctx, doc, ctm, base_uri, dict, node, link);
	else if (fz_xml_is_tag(node, "Glyphs"))
		xps_load_links_in_glyphs(ctx, doc, ctm, base_uri, dict, node, link);
	else if (fz_xml_is_tag(node, "Canvas"))
		xps_load_links_in_canvas(ctx, doc, ctm, base_uri, dict, node, link);
	else if (fz_xml_is_tag(node, "AlternateContent"))
	{
		node = xps_lookup_alternate_content(ctx, doc, node);
		if (node)
			xps_load_links_in_element(ctx, doc, ctm, base_uri, dict, node, link);
	}
}

static void
xps_load_links_in_fixed_page(fz_context *ctx, xps_document *doc, const fz_matrix *ctm, xps_page *page, fz_link **link)
{
	fz_xml *node, *resource_tag;
	xps_resource *dict = NULL;
	char base_uri[1024];
	char *s;

	if (!page->root)
		return;

	fz_strlcpy(base_uri, page->fix->name, sizeof base_uri);
	s = strrchr(base_uri, '/');
	if (s)
		s[1] = 0;

	resource_tag = fz_xml_down(fz_xml_find_down(page->root, "FixedPage.Resources"));
	if (resource_tag)
		dict = xps_parse_resource_dictionary(ctx, doc, base_uri, resource_tag);

	for (node = fz_xml_down(page->root); node; node = fz_xml_next(node))
		xps_load_links_in_element(ctx, doc, ctm, base_uri, dict, node, link);

	if (dict)
		xps_drop_resource_dictionary(ctx, doc, dict);
}

fz_link *
xps_load_links(fz_context *ctx, xps_page *page)
{
	fz_matrix ctm;
	fz_link *link = NULL;
	fz_scale(&ctm, 72.0f / 96.0f, 72.0f / 96.0f);
	xps_load_links_in_fixed_page(ctx, page->doc, &ctm, page, &link);
	return link;
}
