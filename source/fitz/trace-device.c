#include "mupdf/fitz.h"

static void
fz_trace_matrix(const fz_matrix *ctm)
{
	printf(" matrix=\"%g %g %g %g %g %g\"",
		ctm->a, ctm->b, ctm->c, ctm->d, ctm->e, ctm->f);
}

static void
fz_trace_trm(const fz_matrix *trm)
{
	printf(" trm=\"%g %g %g %g\"",
		trm->a, trm->b, trm->c, trm->d);
}

static void
fz_trace_color(fz_colorspace *colorspace, float *color, float alpha)
{
	int i;
	printf(" colorspace=\"%s\" color=\"", colorspace->name);
	for (i = 0; i < colorspace->n; i++)
		printf("%s%g", i == 0 ? "" : " ", color[i]);
	printf("\"");
	if (alpha < 1)
		printf(" alpha=\"%g\"", alpha);
}

static void
fz_trace_path(fz_path *path, int indent)
{
	float x, y;
	int i = 0, k = 0, n;
	while (i < path->cmd_len)
	{
		for (n = 0; n < indent; n++)
			putchar(' ');
		switch (path->cmds[i++])
		{
		case FZ_MOVETO:
			x = path->coords[k++];
			y = path->coords[k++];
			printf("<moveto x=\"%g\" y=\"%g\"/>\n", x, y);
			break;
		case FZ_LINETO:
			x = path->coords[k++];
			y = path->coords[k++];
			printf("<lineto x=\"%g\" y=\"%g\"/>\n", x, y);
			break;
		case FZ_CURVETO:
			x = path->coords[k++];
			y = path->coords[k++];
			printf("<curveto x1=\"%g\" y1=\"%g\"", x, y);
			x = path->coords[k++];
			y = path->coords[k++];
			printf(" x2=\"%g\" y2=\"%g\"", x, y);
			x = path->coords[k++];
			y = path->coords[k++];
			printf(" x3=\"%g\" y3=\"%g\"/>\n", x, y);
			break;
		case FZ_CLOSE_PATH:
			printf("<closepath/>\n");
			break;
		}
	}
}

static void
fz_trace_begin_page(fz_device *dev, const fz_rect *rect, const fz_matrix *ctm)
{
	printf("<page mediabox=\"%g %g %g %g\"", rect->x0, rect->y0, rect->x1, rect->y1);
	fz_trace_matrix(ctm);
	printf(">\n");
}

static void
fz_trace_end_page(fz_device *dev)
{
	printf("</page>\n");
}

static void
fz_trace_fill_path(fz_device *dev, fz_path *path, int even_odd, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<fill_path");
	if (even_odd)
		printf(" winding=\"eofill\"");
	else
		printf(" winding=\"nonzero\"");
	fz_trace_color(colorspace, color, alpha);
	fz_trace_matrix(ctm);
	printf(">\n");
	fz_trace_path(path, 0);
	printf("</fill_path>\n");
}

static void
fz_trace_stroke_path(fz_device *dev, fz_path *path, fz_stroke_state *stroke, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	int i;

	printf("<stroke_path");
	printf(" linewidth=\"%g\"", stroke->linewidth);
	printf(" miterlimit=\"%g\"", stroke->miterlimit);
	printf(" linecap=\"%d,%d,%d\"", stroke->start_cap, stroke->dash_cap, stroke->end_cap);
	printf(" linejoin=\"%d\"", stroke->linejoin);

	if (stroke->dash_len)
	{
		printf(" dash_phase=\"%g\" dash=\"", stroke->dash_phase);
		for (i = 0; i < stroke->dash_len; i++)
			printf("%s%g", i > 0 ? " " : "", stroke->dash_list[i]);
		printf("\"");
	}

	fz_trace_color(colorspace, color, alpha);
	fz_trace_matrix(ctm);
	printf(">\n");

	fz_trace_path(path, 0);

	printf("</stroke_path>\n");
}

static void
fz_trace_clip_path(fz_device *dev, fz_path *path, const fz_rect *rect, int even_odd, const fz_matrix *ctm)
{
	printf("<clip_path");
	if (even_odd)
		printf(" winding=\"eofill\"");
	else
		printf(" winding=\"nonzero\"");
	fz_trace_matrix(ctm);
	if (rect)
		printf(" contentbbox=\"%g %g %g %g\">\n", rect->x0, rect->y0, rect->x1, rect->y1);
	else
		printf(">\n");
	fz_trace_path(path, 0);
	printf("</clip_path>\n");
}

static void
fz_trace_clip_stroke_path(fz_device *dev, fz_path *path, const fz_rect *rect, fz_stroke_state *stroke, const fz_matrix *ctm)
{
	printf("<clip_stroke_path");
	fz_trace_matrix(ctm);
	printf(">\n");
	fz_trace_path(path, 0);
	printf("</clip_stroke_path>\n");
}

static void
fz_trace_fill_text(fz_device *dev, fz_text *text, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<fill_text font=\"%s\" wmode=\"%d\"", text->font->name, text->wmode);
	fz_trace_color(colorspace, color, alpha);
	fz_trace_matrix(ctm);
	fz_trace_trm(&text->trm);
	printf(">\n");
	fz_print_text(dev->ctx, stdout, text);
	printf("</fill_text>\n");
}

static void
fz_trace_stroke_text(fz_device *dev, fz_text *text, fz_stroke_state *stroke, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<stroke_text font=\"%s\" wmode=\"%d\"", text->font->name, text->wmode);
	fz_trace_color(colorspace, color, alpha);
	fz_trace_matrix(ctm);
	fz_trace_trm(&text->trm);
	printf(">\n");
	fz_print_text(dev->ctx, stdout, text);
	printf("</stroke_text>\n");
}

static void
fz_trace_clip_text(fz_device *dev, fz_text *text, const fz_matrix *ctm, int accumulate)
{
	printf("<clip_text font=\"%s\" wmode=\"%d\"", text->font->name, text->wmode);
	printf(" accumulate=\"%d\"", accumulate);
	fz_trace_matrix(ctm);
	fz_trace_trm(&text->trm);
	printf(">\n");
	fz_print_text(dev->ctx, stdout, text);
	printf("</clip_text>\n");
}

static void
fz_trace_clip_stroke_text(fz_device *dev, fz_text *text, fz_stroke_state *stroke, const fz_matrix *ctm)
{
	printf("<clip_stroke_text font=\"%s\" wmode=\"%d\"", text->font->name, text->wmode);
	fz_trace_matrix(ctm);
	fz_trace_trm(&text->trm);
	printf(">\n");
	fz_print_text(dev->ctx, stdout, text);
	printf("</clip_stroke_text>\n");
}

static void
fz_trace_ignore_text(fz_device *dev, fz_text *text, const fz_matrix *ctm)
{
	printf("<ignore_text font=\"%s\" wmode=\"%d\"", text->font->name, text->wmode);
	fz_trace_matrix(ctm);
	fz_trace_trm(&text->trm);
	printf(">\n");
	fz_print_text(dev->ctx, stdout, text);
	printf("</ignore_text>\n");
}

static void
fz_trace_fill_image(fz_device *dev, fz_image *image, const fz_matrix *ctm, float alpha)
{
	printf("<fill_image alpha=\"%g\"", alpha);
	fz_trace_matrix(ctm);
	printf(" width=\"%d\" height=\"%d\"", image->w, image->h);
	printf("/>\n");
}

static void
fz_trace_fill_shade(fz_device *dev, fz_shade *shade, const fz_matrix *ctm, float alpha)
{
	printf("<fill_shade alpha=\"%g\"", alpha);
	fz_trace_matrix(ctm);
	printf("/>\n");
}

static void
fz_trace_fill_image_mask(fz_device *dev, fz_image *image, const fz_matrix *ctm,
fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<fill_image_mask");
	fz_trace_matrix(ctm);
	fz_trace_color(colorspace, color, alpha);
	printf(" width=\"%d\" height=\"%d\"", image->w, image->h);
	printf("/>\n");
}

static void
fz_trace_clip_image_mask(fz_device *dev, fz_image *image, const fz_rect *rect, const fz_matrix *ctm)
{
	printf("<clip_image_mask");
	fz_trace_matrix(ctm);
	printf(" width=\"%d\" height=\"%d\"", image->w, image->h);
	printf("/>\n");
}

static void
fz_trace_pop_clip(fz_device *dev)
{
	printf("<pop_clip/>\n");
}

static void
fz_trace_begin_mask(fz_device *dev, const fz_rect *bbox, int luminosity, fz_colorspace *colorspace, float *color)
{
	printf("<mask bbox=\"%g %g %g %g\" s=\"%s\"",
		bbox->x0, bbox->y0, bbox->x1, bbox->y1,
		luminosity ? "luminosity" : "alpha");
	printf(">\n");
}

static void
fz_trace_end_mask(fz_device *dev)
{
	printf("</mask>\n");
}

static void
fz_trace_begin_group(fz_device *dev, const fz_rect *bbox, int isolated, int knockout, int blendmode, float alpha)
{
	printf("<group bbox=\"%g %g %g %g\" isolated=\"%d\" knockout=\"%d\" blendmode=\"%s\" alpha=\"%g\">\n",
		bbox->x0, bbox->y0, bbox->x1, bbox->y1,
		isolated, knockout, fz_blendmode_name(blendmode), alpha);
}

static void
fz_trace_end_group(fz_device *dev)
{
	printf("</group>\n");
}

static int
fz_trace_begin_tile(fz_device *dev, const fz_rect *area, const fz_rect *view, float xstep, float ystep, const fz_matrix *ctm, int id)
{
	printf("<tile");
	printf(" area=\"%g %g %g %g\"", area->x0, area->y0, area->x1, area->y1);
	printf(" view=\"%g %g %g %g\"", view->x0, view->y0, view->x1, view->y1);
	printf(" xstep=\"%g\" ystep=\"%g\"", xstep, ystep);
	fz_trace_matrix(ctm);
	printf(">\n");
	return 0;
}

static void
fz_trace_end_tile(fz_device *dev)
{
	printf("</tile>\n");
}

fz_device *fz_new_trace_device(fz_context *ctx)
{
	fz_device *dev = fz_new_device(ctx, NULL);

	dev->begin_page = fz_trace_begin_page;
	dev->end_page = fz_trace_end_page;

	dev->fill_path = fz_trace_fill_path;
	dev->stroke_path = fz_trace_stroke_path;
	dev->clip_path = fz_trace_clip_path;
	dev->clip_stroke_path = fz_trace_clip_stroke_path;

	dev->fill_text = fz_trace_fill_text;
	dev->stroke_text = fz_trace_stroke_text;
	dev->clip_text = fz_trace_clip_text;
	dev->clip_stroke_text = fz_trace_clip_stroke_text;
	dev->ignore_text = fz_trace_ignore_text;

	dev->fill_shade = fz_trace_fill_shade;
	dev->fill_image = fz_trace_fill_image;
	dev->fill_image_mask = fz_trace_fill_image_mask;
	dev->clip_image_mask = fz_trace_clip_image_mask;

	dev->pop_clip = fz_trace_pop_clip;

	dev->begin_mask = fz_trace_begin_mask;
	dev->end_mask = fz_trace_end_mask;
	dev->begin_group = fz_trace_begin_group;
	dev->end_group = fz_trace_end_group;

	dev->begin_tile = fz_trace_begin_tile;
	dev->end_tile = fz_trace_end_tile;

	return dev;
}
