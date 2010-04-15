#include "fitz.h"

static void
fz_tracematrix(fz_matrix ctm)
{
	printf("matrix=\"%g %g %g %g %g %g\" ",
		ctm.a, ctm.b, ctm.c, ctm.d, ctm.e, ctm.f);
}

static void
fz_tracecolor(fz_colorspace *colorspace, float *color, float alpha)
{
	int i;
	printf("colorspace=\"%s\" color=\"", colorspace->name);
	for (i = 0; i < colorspace->n; i++)
		printf("%s%g", i == 0 ? "" : " ", color[i]);
	printf("\" ");
	if (alpha != 1.0)
		printf("alpha=\"%g\" ", alpha);
}

static void
fz_tracepath(fz_path *path, int indent)
{
	float x, y;
	int i = 0;
	int n;
	while (i < path->len)
	{
		for (n = 0; n < indent; n++)
			putchar(' ');
		switch (path->els[i++].k)
		{
		case FZ_MOVETO:
			x = path->els[i++].v;
			y = path->els[i++].v;
			printf("<moveto x=\"%g\" y=\"%g\" />\n", x, y);
			break;
		case FZ_LINETO:
			x = path->els[i++].v;
			y = path->els[i++].v;
			printf("<lineto x=\"%g\" y=\"%g\" />\n", x, y);
			break;
		case FZ_CURVETO:
			x = path->els[i++].v;
			y = path->els[i++].v;
			printf("<curveto x1=\"%g\" y1=\"%g\" ", x, y);
			x = path->els[i++].v;
			y = path->els[i++].v;
			printf("x2=\"%g\" y2=\"%g\" ", x, y);
			x = path->els[i++].v;
			y = path->els[i++].v;
			printf("x3=\"%g\" y3=\"%g\" />\n", x, y);
			break;
		case FZ_CLOSEPATH:
			printf("<closepath />\n");
		}
	}
}

static void
fz_tracefillpath(void *user, fz_path *path, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<fillpath ");
	if (path->evenodd)
		printf("winding=\"eofill\" ");
	else
		printf("winding=\"nonzero\" ");
	fz_tracecolor(colorspace, color, alpha);
	fz_tracematrix(ctm);
	printf(">\n");
	fz_tracepath(path, 0);
	printf("</fillpath>\n");
}

static void
fz_tracestrokepath(void *user, fz_path *path, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	int i;

	printf("<strokepath ");
	printf("linewidth=\"%g\" ", path->linewidth);
	printf("miterlimit=\"%g\" ", path->miterlimit);
	printf("linecap=\"%d\" ", path->linecap);
	printf("linejoin=\"%d\" ", path->linejoin);

	if (path->dashlen)
	{
		printf("dashphase=\"%g\" dash=\"", path->dashphase);
		for (i = 0; i < path->dashlen; i++)
			printf("%g ", path->dashlist[i]);
		printf("\"");
	}

	fz_tracecolor(colorspace, color, alpha);
	fz_tracematrix(ctm);
	printf(">\n");

	fz_tracepath(path, 0);

	printf("</strokepath>\n");
}

static void
fz_traceclippath(void *user, fz_path *path, fz_matrix ctm)
{
	printf("<gsave>\n");
	printf("<clippath ");
	if (path->evenodd)
		printf("winding=\"eofill\" ");
	else
		printf("winding=\"nonzero\" ");
	fz_tracematrix(ctm);
	printf(">\n");
	fz_tracepath(path, 0);
	printf("</clippath>\n");
}

static void
fz_tracefilltext(void *user, fz_text *text, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<filltext font=\"%s\" ", text->font->name);
	fz_tracecolor(colorspace, color, alpha);
	fz_tracematrix(fz_concat(ctm, text->trm));
	printf(">\n");
	fz_debugtext(text, 0);
	printf("</filltext>\n");
}

static void
fz_tracestroketext(void *user, fz_text *text, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<stroketext font=\"%s\" ", text->font->name);
	fz_tracecolor(colorspace, color, alpha);
	fz_tracematrix(fz_concat(ctm, text->trm));
	printf(">\n");
	fz_debugtext(text, 0);
	printf("</stroketext>\n");
}

static void
fz_tracecliptext(void *user, fz_text *text, fz_matrix ctm)
{
	printf("<gsave>\n");
	printf("<cliptext font=\"%s\" ", text->font->name);
	fz_tracematrix(fz_concat(ctm, text->trm));
	printf(">\n");
	fz_debugtext(text, 0);
	printf("</cliptext>\n");
}

static void
fz_traceignoretext(void *user, fz_text *text, fz_matrix ctm)
{
	printf("<ignoretext font=\"%s\" ", text->font->name);
	fz_tracematrix(fz_concat(ctm, text->trm));
	printf(">\n");
	fz_debugtext(text, 0);
	printf("</ignoretext>\n");
}

static void
fz_tracefillimage(void *user, fz_pixmap *image, fz_matrix ctm)
{
	printf("<fillimage ");
	fz_tracematrix(ctm);
	printf("/>\n");
}

static void
fz_tracefillshade(void *user, fz_shade *shade, fz_matrix ctm)
{
	printf("<fillshade ");
	fz_tracematrix(ctm);
	printf("/>\n");
}

static void
fz_tracefillimagemask(void *user, fz_pixmap *image, fz_matrix ctm,
fz_colorspace *colorspace, float *color, float alpha)
{
	printf("<fillimagemask ");
	fz_tracematrix(ctm);
	fz_tracecolor(colorspace, color, alpha);
	printf("/>\n");
}

static void
fz_traceclipimagemask(void *user, fz_pixmap *image, fz_matrix ctm)
{
	printf("<gsave>\n");
	printf("<clipimagemask ");
	fz_tracematrix(ctm);
	printf("/>\n");
}

static void
fz_tracepopclip(void *user)
{
	printf("</gsave>\n");
}

fz_device *fz_newtracedevice(void)
{
	fz_device *dev = fz_newdevice(nil);

	dev->fillpath = fz_tracefillpath;
	dev->strokepath = fz_tracestrokepath;
	dev->clippath = fz_traceclippath;

	dev->filltext = fz_tracefilltext;
	dev->stroketext = fz_tracestroketext;
	dev->cliptext = fz_tracecliptext;
	dev->ignoretext = fz_traceignoretext;

	dev->fillshade = fz_tracefillshade;
	dev->fillimage = fz_tracefillimage;
	dev->fillimagemask = fz_tracefillimagemask;
	dev->clipimagemask = fz_traceclipimagemask;

	dev->popclip = fz_tracepopclip;

	return dev;
}

