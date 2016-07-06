/*
 * PDF creation tool: Tool for creating pdf content.
 *
 * Simple test bed to work with adding content and creating PDFs
 */

#include "mupdf/pdf.h"

static void usage(void)
{
	fprintf(stderr,
		"usage: mutool create [-o output.pdf] [-adlsz] page.txt [page2.txt ...]\n"
		"\t-o\tname of PDF file to create\n"
		"\t-a\tascii hex encode binary streams\n"
		"\t-d\tdecompress all streams\n"
		"\t-l\tlinearize PDF\n"
		"\t-s\tclean content streams\n"
		"\t-z\tdeflate uncompressed streams\n"
		"\tpage.txt file defines page size, fonts, images and contents\n"
		);
	exit(1);
}

static fz_context *ctx = NULL;
static pdf_document *doc = NULL;

static void add_font_res(pdf_obj *resources, char *name, char *path)
{
	const char *data;
	int size;
	fz_font *font;
	pdf_obj *subres, *ref;

	data = fz_lookup_base14_font(ctx, path, &size);
	if (data)
		font = fz_new_font_from_memory(ctx, path, data, size, 0, 0);
	else
		font = fz_new_font_from_file(ctx, NULL, path, 0, 0);

	subres = pdf_dict_get(ctx, resources, PDF_NAME_Font);
	if (!subres)
	{
		subres = pdf_new_dict(ctx, doc, 10);
		pdf_dict_put_drop(ctx, resources, PDF_NAME_Font, subres);
	}

	ref = pdf_add_simple_font(ctx, doc, font);
	pdf_dict_puts(ctx, subres, name, ref);
	pdf_drop_obj(ctx, ref);

	fz_drop_font(ctx, font);
}

static void add_image_res(pdf_obj *resources, char *name, char *path)
{
	fz_image *image;
	pdf_obj *subres, *ref;

	image = fz_new_image_from_file(ctx, path);

	subres = pdf_dict_get(ctx, resources, PDF_NAME_XObject);
	if (!subres)
	{
		subres = pdf_new_dict(ctx, doc, 10);
		pdf_dict_put_drop(ctx, resources, PDF_NAME_XObject, subres);
	}

	ref = pdf_add_image(ctx, doc, image, 0);
	pdf_dict_puts(ctx, subres, name, ref);
	pdf_drop_obj(ctx, ref);

	fz_drop_image(ctx, image);
}

/*
The input is a raw content stream, with commands embedded in comments:

%%MediaBox LLX LLY URX URY
%%Rotate Angle
%%Font Name Filename (or base 14 font name)
%%Image Name Filename
*/
static void create_page(char *input)
{
	fz_rect mediabox = { 0, 0, 595, 842 };
	int rotate = 0;

	char line[4096];
	char *s, *p;
	fz_stream *stm;

	fz_buffer *contents;
	pdf_obj *resources;
	pdf_obj *page;

	resources = pdf_new_dict(ctx, doc, 2);
	contents = fz_new_buffer(ctx, 1024);

	stm = fz_open_file(ctx, input);
	while (fz_read_line(ctx, stm, line, sizeof line))
	{
		if (line[0] == '%' && line[1] == '%')
		{
			p = line;
			s = fz_strsep(&p, " ");
			if (!strcmp(s, "%%MediaBox"))
			{
				mediabox.x0 = fz_atoi(fz_strsep(&p, " "));
				mediabox.y0 = fz_atoi(fz_strsep(&p, " "));
				mediabox.x1 = fz_atoi(fz_strsep(&p, " "));
				mediabox.y1 = fz_atoi(fz_strsep(&p, " "));
			}
			else if (!strcmp(s, "%%Rotate"))
			{
				rotate = fz_atoi(fz_strsep(&p, " "));
			}
			else if (!strcmp(s, "%%Font"))
			{
				s = fz_strsep(&p, " ");
				add_font_res(resources, s, p);
			}
			else if (!strcmp(s, "%%Image"))
			{
				s = fz_strsep(&p, " ");
				add_image_res(resources, s, p);
			}
		}
		else
		{
			fz_write_buffer(ctx, contents, line, strlen(line));
			fz_write_buffer_byte(ctx, contents, '\n');
		}
	}
	fz_drop_stream(ctx, stm);

	page = pdf_add_page(ctx, doc, &mediabox, rotate, contents, resources);
	pdf_insert_page(ctx, doc, -1, page);
	pdf_drop_obj(ctx, page);

	fz_drop_buffer(ctx, contents);
	pdf_drop_obj(ctx, resources);
}

int pdfcreate_main(int argc, char **argv)
{
	pdf_write_options opts = { 0 };
	char *output = "out.pdf";
	int i, c;

	while ((c = fz_getopt(argc, argv, "adlszo:")) != -1)
	{
		switch (c)
		{
		case 'o': output = fz_optarg; break;
		case 'a': opts.do_ascii ++; break;
		case 'd': opts.do_expand ^= PDF_EXPAND_ALL; break;
		case 'l': opts.do_linear ++; break;
		case 's': opts.do_clean ++; break;
		case 'z': opts.do_deflate ++; break;
		default: usage(); break;
		}
	}

	if (fz_optind == argc)
		usage();

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx)
	{
		fprintf(stderr, "cannot initialise context\n");
		exit(1);
	}

	doc = pdf_create_document(ctx);

	for (i = fz_optind; i < argc; ++i)
		create_page(argv[i]);

	pdf_save_document(ctx, doc, output, &opts);

	pdf_drop_document(ctx, doc);

	fz_flush_warnings(ctx);
	fz_drop_context(ctx);
	return 0;
}
