/*
 * mudraw -- command line tool for drawing pdf/xps/cbz documents
 */

#include "fitz.h"

#ifdef _MSC_VER
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

enum { TEXT_PLAIN = 1, TEXT_HTML = 2, TEXT_XML = 3 };

/*
	A useful bit of bash script to call this to generate mjs files:
	for f in tests_private/pdf/forms/v1.3/ *.pdf ; do g=${f%.*} ; echo $g ; ../mupdf.git/win32/debug/mudraw.exe -j $g.mjs $g.pdf ; done

	Remove the space from "/ *.pdf" before running - can't leave that
	in here, as it causes a warning about a possibly malformed comment.
*/

static char lorem[] =
"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum "
"vehicula augue id est lobortis mollis. Aenean vestibulum metus sed est "
"gravida non tempus lacus aliquet. Nulla vehicula lobortis tincidunt. "
"Donec malesuada nisl et lacus condimentum nec tincidunt urna gravida. "
"Sed dapibus magna eu velit ultrices non rhoncus risus lacinia. Fusce "
"vitae nulla volutpat elit dictum ornare at eu libero. Maecenas felis "
"enim, tempor a tincidunt id, commodo consequat lectus.\n"
"Morbi tincidunt adipiscing lacus eu dignissim. Pellentesque augue elit, "
"ultrices vitae fermentum et, faucibus et purus. Nam ante libero, lacinia "
"id tincidunt at, ultricies a lorem. Donec non neque at purus condimentum "
"eleifend quis sit amet libero. Sed semper, mi ut tempus tincidunt, lacus "
"eros pellentesque lacus, id vehicula est diam eu quam. Integer tristique "
"fringilla rhoncus. Phasellus convallis, justo ut mollis viverra, dui odio "
"euismod ante, nec fringilla nisl mi ac diam.\n"
"Maecenas mi urna, ornare commodo feugiat id, cursus in massa. Vivamus "
"augue augue, aliquam at varius eu, venenatis fermentum felis. Sed varius "
"turpis a felis ultrices quis aliquet nunc tincidunt. Suspendisse posuere "
"commodo nunc non viverra. Praesent condimentum varius quam, vel "
"consectetur odio volutpat in. Sed malesuada augue ut lectus commodo porta. "
"Vivamus eget mauris sit amet diam ultrices sollicitudin. Cras pharetra leo "
"non elit lacinia vulputate.\n"
"Donec ac enim justo, ornare scelerisque diam. Ut vel ante at lorem "
"placerat bibendum ultricies mattis metus. Phasellus in imperdiet odio. "
"Proin semper lacinia libero, sed rutrum eros blandit non. Duis tincidunt "
"ligula est, non pellentesque mauris. Aliquam in erat scelerisque lacus "
"dictum suscipit eget semper magna. Nullam luctus imperdiet risus a "
"semper.\n"
"Curabitur sit amet tempor sapien. Quisque et tortor in lacus dictum "
"pulvinar. Nunc at nisl ut velit vehicula hendrerit. Mauris elementum "
"sollicitudin leo ac ullamcorper. Proin vel leo nec justo tempus aliquet "
"nec ut mi. Pellentesque vel nisl id dui hendrerit fermentum nec quis "
"tortor. Proin eu sem luctus est consequat euismod. Vestibulum ante ipsum "
"primis in faucibus orci luctus et ultrices posuere cubilia Curae; Fusce "
"consectetur ultricies nisl ornare dictum. Cras sagittis consectetur lorem "
"sed posuere. Mauris accumsan laoreet arcu, id molestie lorem faucibus eu. "
"Vivamus commodo, neque nec imperdiet pretium, lorem metus viverra turpis, "
"malesuada vulputate justo eros sit amet neque. Nunc quis justo elit, non "
"rutrum mauris. Maecenas blandit condimentum nibh, nec vulputate orci "
"pulvinar at. Proin sed arcu vel odio tempus lobortis sed posuere ipsum. Ut "
"feugiat pellentesque tortor nec ornare.\n";

static char *output = NULL;
static float resolution = 72;
static int res_specified = 0;
static float rotation = 0;

static int showxml = 0;
static int showtext = 0;
static int showtime = 0;
static int showmd5 = 0;
static int showoutline = 0;
static int savealpha = 0;
static int uselist = 1;
static int alphabits = 8;
static float gamma_value = 1;
static int invert = 0;
static int width = 0;
static int height = 0;
static int fit = 0;
static int errored = 0;
static int ignore_errors = 0;

static fz_text_sheet *sheet = NULL;
static fz_colorspace *colorspace;
static char *filename;
static int files = 0;
fz_output *out = NULL;

static char *mujstest_filename = NULL;
static FILE *mujstest_file = NULL;
static int mujstest_count = 0;

static struct {
	int count, total;
	int min, max;
	int minpage, maxpage;
	char *minfilename;
	char *maxfilename;
} timing;

static void usage(void)
{
	fprintf(stderr,
		"usage: mudraw [options] input [pages]\n"
		"\t-o -\toutput filename (%%d for page number)\n"
		"\t\tsupported formats: pgm, ppm, pam, png, pbm\n"
		"\t-p -\tpassword\n"
		"\t-r -\tresolution in dpi (default: 72)\n"
		"\t-w -\twidth (in pixels) (maximum width if -r is specified)\n"
		"\t-h -\theight (in pixels) (maximum height if -r is specified)\n"
		"\t-f -\tfit width and/or height exactly (ignore aspect)\n"
		"\t-a\tsave alpha channel (only pam and png)\n"
		"\t-b -\tnumber of bits of antialiasing (0 to 8)\n"
		"\t-g\trender in grayscale\n"
		"\t-m\tshow timing information\n"
		"\t-t\tshow text (-tt for xml, -ttt for more verbose xml)\n"
		"\t-x\tshow display list\n"
		"\t-d\tdisable use of display list\n"
		"\t-5\tshow md5 checksums\n"
		"\t-R -\trotate clockwise by given number of degrees\n"
		"\t-G gamma\tgamma correct output\n"
		"\t-I\tinvert output\n"
		"\t-l\tprint outline\n"
		"\t-j -\tOutput mujstest file\n"
		"\t-i\tignore errors and continue with the next file\n"
		"\tpages\tcomma separated list of ranges\n");
	exit(1);
}

static int gettime(void)
{
	static struct timeval first;
	static int once = 1;
	struct timeval now;
	if (once)
	{
		gettimeofday(&first, NULL);
		once = 0;
	}
	gettimeofday(&now, NULL);
	return (now.tv_sec - first.tv_sec) * 1000 + (now.tv_usec - first.tv_usec) / 1000;
}

static int isrange(char *s)
{
	while (*s)
	{
		if ((*s < '0' || *s > '9') && *s != '-' && *s != ',')
			return 0;
		s++;
	}
	return 1;
}

static void escape_string(FILE *out, int len, const char *string)
{
	while (len-- && *string)
	{
		char c = *string++;
		switch (c)
		{
		case '\n':
			fputc('\\', out);
			fputc('n', out);
			break;
		case '\r':
			fputc('\\', out);
			fputc('r', out);
			break;
		case '\t':
			fputc('\\', out);
			fputc('t', out);
			break;
		default:
			fputc(c, out);
		}
	}
}

static void drawpage(fz_context *ctx, fz_document *doc, int pagenum)
{
	fz_page *page;
	fz_display_list *list = NULL;
	fz_device *dev = NULL;
	int start;
	fz_cookie cookie = { 0 };
	int needshot = 0;

	fz_var(list);
	fz_var(dev);

	if (showtime)
	{
		start = gettime();
	}

	fz_try(ctx)
	{
		page = fz_load_page(doc, pagenum - 1);
	}
	fz_catch(ctx)
	{
		fz_throw(ctx, "cannot load page %d in file '%s'", pagenum, filename);
	}

	if (mujstest_file)
	{
		fz_interactive *inter = fz_interact(doc);
		fz_widget *widget = NULL;

		if (inter)
			widget = fz_first_widget(inter, page);

		if (widget)
		{
			fprintf(mujstest_file, "GOTO %d\n", pagenum);
			needshot = 1;
		}
		for (;widget; widget = fz_next_widget(inter, widget))
		{
			fz_rect rect;
			int w, h, len;
			int type = fz_widget_get_type(widget);

			fz_bound_widget(widget, &rect);
			w = (rect.x1 - rect.x0);
			h = (rect.y1 - rect.y0);
			++mujstest_count;
			switch (type)
			{
			default:
				fprintf(mujstest_file, "%% UNKNOWN %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				break;
			case FZ_WIDGET_TYPE_PUSHBUTTON:
				fprintf(mujstest_file, "%% PUSHBUTTON %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				break;
			case FZ_WIDGET_TYPE_CHECKBOX:
				fprintf(mujstest_file, "%% CHECKBOX %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				break;
			case FZ_WIDGET_TYPE_RADIOBUTTON:
				fprintf(mujstest_file, "%% RADIOBUTTON %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				break;
			case FZ_WIDGET_TYPE_TEXT:
			{
				int maxlen = fz_text_widget_max_len(inter, widget);
				int texttype = fz_text_widget_content_type(inter, widget);

				/* If height is low, assume a single row, and base
				 * the width off that. */
				if (h < 10)
				{
					w = (w+h-1) / (h ? h : 1);
					h = 1;
				}
				/* Otherwise, if width is low, work off height */
				else if (w < 10)
				{
					h = (w+h-1) / (w ? w : 1);
					w = 1;
				}
				else
				{
					w = (w+9)/10;
					h = (h+9)/10;
				}
				len = w*h;
				if (len < 2)
					len = 2;
				if (len > maxlen)
					len = maxlen;
				fprintf(mujstest_file, "%% TEXT %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				switch (texttype)
				{
				default:
				case FZ_WIDGET_CONTENT_UNRESTRAINED:
					fprintf(mujstest_file, "TEXT %d ", mujstest_count);
					escape_string(mujstest_file, len-3, lorem);
					fprintf(mujstest_file, "\n");
					break;
				case FZ_WIDGET_CONTENT_NUMBER:
					fprintf(mujstest_file, "TEXT %d\n", mujstest_count);
					break;
				case FZ_WIDGET_CONTENT_SPECIAL:
					fprintf(mujstest_file, "TEXT %lld\n", 46702919800LL + mujstest_count);
					break;
				case FZ_WIDGET_CONTENT_DATE:
					fprintf(mujstest_file, "TEXT Jun %d 1979\n", 1 + ((13 + mujstest_count) % 30));
					break;
				case FZ_WIDGET_CONTENT_TIME:
					++mujstest_count;
					fprintf(mujstest_file, "TEXT %02d:%02d\n", ((mujstest_count/60) % 24), mujstest_count % 60);
					break;
				}
				break;
			}
			case FZ_WIDGET_TYPE_LISTBOX:
				fprintf(mujstest_file, "%% LISTBOX %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				break;
			case FZ_WIDGET_TYPE_COMBOBOX:
				fprintf(mujstest_file, "%% COMBOBOX %0.2f %0.2f %0.2f %0.2f\n", rect.x0, rect.y0, rect.x1, rect.y1);
				break;
			}
			fprintf(mujstest_file, "CLICK %0.2f %0.2f\n", (rect.x0+rect.x1)/2, (rect.y0+rect.y1)/2);
		}
	}

	if (uselist)
	{
		fz_try(ctx)
		{
			list = fz_new_display_list(ctx);
			dev = fz_new_list_device(ctx, list);
			fz_run_page(doc, page, dev, &fz_identity, &cookie);
		}
		fz_always(ctx)
		{
			fz_free_device(dev);
			dev = NULL;
		}
		fz_catch(ctx)
		{
			fz_free_display_list(ctx, list);
			fz_free_page(doc, page);
			fz_throw(ctx, "cannot draw page %d in file '%s'", pagenum, filename);
		}
	}

	if (showxml)
	{
		fz_try(ctx)
		{
			dev = fz_new_trace_device(ctx);
			fz_printf(out, "<page number=\"%d\">\n", pagenum);
			if (list)
				fz_run_display_list(list, dev, &fz_identity, &fz_infinite_rect, &cookie);
			else
				fz_run_page(doc, page, dev, &fz_identity, &cookie);
			fz_printf(out, "</page>\n");
		}
		fz_always(ctx)
		{
			fz_free_device(dev);
			dev = NULL;
		}
		fz_catch(ctx)
		{
			fz_free_display_list(ctx, list);
			fz_free_page(doc, page);
			fz_rethrow(ctx);
		}
	}

	if (showtext)
	{
		fz_text_page *text = NULL;

		fz_var(text);

		fz_try(ctx)
		{
			fz_rect bounds;
			text = fz_new_text_page(ctx, fz_bound_page(doc, page, &bounds));
			dev = fz_new_text_device(ctx, sheet, text);
			if (list)
				fz_run_display_list(list, dev, &fz_identity, &fz_infinite_rect, &cookie);
			else
				fz_run_page(doc, page, dev, &fz_identity, &cookie);
			fz_free_device(dev);
			dev = NULL;
			if (showtext == TEXT_XML)
			{
				fz_print_text_page_xml(ctx, out, text);
			}
			else if (showtext == TEXT_HTML)
			{
				fz_print_text_page_html(ctx, out, text);
			}
			else if (showtext == TEXT_PLAIN)
			{
				fz_print_text_page(ctx, out, text);
				fz_printf(out, "\f\n");
			}
		}
		fz_always(ctx)
		{
			fz_free_device(dev);
			dev = NULL;
			fz_free_text_page(ctx, text);
		}
		fz_catch(ctx)
		{
			fz_free_display_list(ctx, list);
			fz_free_page(doc, page);
			fz_rethrow(ctx);
		}
	}

	if (showmd5 || showtime)
		printf("page %s %d", filename, pagenum);

	if (output || showmd5 || showtime)
	{
		float zoom;
		fz_matrix ctm;
		fz_rect bounds, tbounds;
		fz_irect ibounds;
		fz_pixmap *pix = NULL;
		int w, h;

		fz_var(pix);

		fz_bound_page(doc, page, &bounds);
		zoom = resolution / 72;
		fz_pre_scale(fz_rotate(&ctm, rotation), zoom, zoom);
		tbounds = bounds;
		fz_round_rect(&ibounds, fz_transform_rect(&tbounds, &ctm));

		/* Make local copies of our width/height */
		w = width;
		h = height;

		/* If a resolution is specified, check to see whether w/h are
		 * exceeded; if not, unset them. */
		if (res_specified)
		{
			int t;
			t = ibounds.x1 - ibounds.x0;
			if (w && t <= w)
				w = 0;
			t = ibounds.y1 - ibounds.y0;
			if (h && t <= h)
				h = 0;
		}

		/* Now w or h will be 0 unless they need to be enforced. */
		if (w || h)
		{
			float scalex = w / (tbounds.x1 - tbounds.x0);
			float scaley = h / (tbounds.y1 - tbounds.y0);
			fz_matrix scale_mat;

			if (fit)
			{
				if (w == 0)
					scalex = 1.0f;
				if (h == 0)
					scaley = 1.0f;
			}
			else
			{
				if (w == 0)
					scalex = scaley;
				if (h == 0)
					scaley = scalex;
			}
			if (!fit)
			{
				if (scalex > scaley)
					scalex = scaley;
				else
					scaley = scalex;
			}
			fz_scale(&scale_mat, scalex, scaley);
			fz_concat(&ctm, &ctm, &scale_mat);
			tbounds = bounds;
			fz_transform_rect(&tbounds, &ctm);
		}
		fz_round_rect(&ibounds, &tbounds);
		fz_rect_from_irect(&tbounds, &ibounds);

		/* TODO: banded rendering and multi-page ppm */

		fz_try(ctx)
		{
			pix = fz_new_pixmap_with_bbox(ctx, colorspace, &ibounds);

			if (savealpha)
				fz_clear_pixmap(ctx, pix);
			else
				fz_clear_pixmap_with_value(ctx, pix, 255);

			dev = fz_new_draw_device(ctx, pix);
			if (list)
				fz_run_display_list(list, dev, &ctm, &tbounds, &cookie);
			else
				fz_run_page(doc, page, dev, &ctm, &cookie);
			fz_free_device(dev);
			dev = NULL;

			if (invert)
				fz_invert_pixmap(ctx, pix);
			if (gamma_value != 1)
				fz_gamma_pixmap(ctx, pix, gamma_value);

			if (savealpha)
				fz_unmultiply_pixmap(ctx, pix);

			if (output)
			{
				char buf[512];
				sprintf(buf, output, pagenum);
				if (strstr(output, ".pgm") || strstr(output, ".ppm") || strstr(output, ".pnm"))
					fz_write_pnm(ctx, pix, buf);
				else if (strstr(output, ".pam"))
					fz_write_pam(ctx, pix, buf, savealpha);
				else if (strstr(output, ".png"))
					fz_write_png(ctx, pix, buf, savealpha);
				else if (strstr(output, ".pbm")) {
					fz_bitmap *bit = fz_halftone_pixmap(ctx, pix, NULL);
					fz_write_pbm(ctx, bit, buf);
					fz_drop_bitmap(ctx, bit);
				}
			}

			if (showmd5)
			{
				unsigned char digest[16];
				int i;

				fz_md5_pixmap(pix, digest);
				printf(" ");
				for (i = 0; i < 16; i++)
					printf("%02x", digest[i]);
			}
		}
		fz_always(ctx)
		{
			fz_free_device(dev);
			dev = NULL;
			fz_drop_pixmap(ctx, pix);
		}
		fz_catch(ctx)
		{
			fz_free_display_list(ctx, list);
			fz_free_page(doc, page);
			fz_rethrow(ctx);
		}
	}

	if (list)
		fz_free_display_list(ctx, list);

	fz_free_page(doc, page);

	if (showtime)
	{
		int end = gettime();
		int diff = end - start;

		if (diff < timing.min)
		{
			timing.min = diff;
			timing.minpage = pagenum;
			timing.minfilename = filename;
		}
		if (diff > timing.max)
		{
			timing.max = diff;
			timing.maxpage = pagenum;
			timing.maxfilename = filename;
		}
		timing.total += diff;
		timing.count ++;

		printf(" %dms", diff);
	}

	if (showmd5 || showtime)
		printf("\n");

	fz_flush_warnings(ctx);

	if (mujstest_file && needshot)
	{
		fprintf(mujstest_file, "SCREENSHOT\n");
	}

	if (cookie.errors)
		errored = 1;
}

static void drawrange(fz_context *ctx, fz_document *doc, char *range)
{
	int page, spage, epage, pagecount;
	char *spec, *dash;

	pagecount = fz_count_pages(doc);
	spec = fz_strsep(&range, ",");
	while (spec)
	{
		dash = strchr(spec, '-');

		if (dash == spec)
			spage = epage = pagecount;
		else
			spage = epage = atoi(spec);

		if (dash)
		{
			if (strlen(dash) > 1)
				epage = atoi(dash + 1);
			else
				epage = pagecount;
		}

		spage = fz_clampi(spage, 1, pagecount);
		epage = fz_clampi(epage, 1, pagecount);

		if (spage < epage)
			for (page = spage; page <= epage; page++)
				drawpage(ctx, doc, page);
		else
			for (page = spage; page >= epage; page--)
				drawpage(ctx, doc, page);

		spec = fz_strsep(&range, ",");
	}
}

static void drawoutline(fz_context *ctx, fz_document *doc)
{
	fz_outline *outline = fz_load_outline(doc);
	fz_output *out = NULL;

	fz_var(out);
	fz_try(ctx)
	{
		out = fz_new_output_file(ctx, stdout);
		if (showoutline > 1)
			fz_print_outline_xml(ctx, out, outline);
		else
			fz_print_outline(ctx, out, outline);
	}
	fz_always(ctx)
	{
		fz_close_output(out);
		fz_free_outline(ctx, outline);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

#ifdef MUPDF_COMBINED_EXE
int draw_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
	char *password = "";
	int grayscale = 0;
	fz_document *doc = NULL;
	int c;
	fz_context *ctx;

	fz_var(doc);

	while ((c = fz_getopt(argc, argv, "lo:p:r:R:ab:dgmtx5G:Iw:h:fij:")) != -1)
	{
		switch (c)
		{
		case 'o': output = fz_optarg; break;
		case 'p': password = fz_optarg; break;
		case 'r': resolution = atof(fz_optarg); res_specified = 1; break;
		case 'R': rotation = atof(fz_optarg); break;
		case 'a': savealpha = 1; break;
		case 'b': alphabits = atoi(fz_optarg); break;
		case 'l': showoutline++; break;
		case 'm': showtime++; break;
		case 't': showtext++; break;
		case 'x': showxml++; break;
		case '5': showmd5++; break;
		case 'g': grayscale++; break;
		case 'd': uselist = 0; break;
		case 'G': gamma_value = atof(fz_optarg); break;
		case 'w': width = atof(fz_optarg); break;
		case 'h': height = atof(fz_optarg); break;
		case 'f': fit = 1; break;
		case 'I': invert++; break;
		case 'j': mujstest_filename = fz_optarg; break;
		case 'i': ignore_errors = 1; break;
		default: usage(); break;
		}
	}

	if (fz_optind == argc)
		usage();

	if (!showtext && !showxml && !showtime && !showmd5 && !showoutline && !output && !mujstest_filename)
	{
		printf("nothing to do\n");
		exit(0);
	}

	if (mujstest_filename)
	{
		if (strcmp(mujstest_filename, "-") == 0)
			mujstest_file = stdout;
		else
			mujstest_file = fopen(mujstest_filename, "wb");
	}

	ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!ctx)
	{
		fprintf(stderr, "cannot initialise context\n");
		exit(1);
	}

	fz_set_aa_level(ctx, alphabits);

	colorspace = fz_device_rgb;
	if (output && strstr(output, ".pgm"))
		colorspace = fz_device_gray;
	if (output && strstr(output, ".ppm"))
		colorspace = fz_device_rgb;
	if (output && strstr(output, ".pbm"))
		colorspace = fz_device_gray;
	if (grayscale)
		colorspace = fz_device_gray;

	timing.count = 0;
	timing.total = 0;
	timing.min = 1 << 30;
	timing.max = 0;
	timing.minpage = 0;
	timing.maxpage = 0;
	timing.minfilename = "";
	timing.maxfilename = "";

	if (showxml || showtext)
		out = fz_new_output_file(ctx, stdout);

	if (showxml || showtext == TEXT_XML)
		fz_printf(out, "<?xml version=\"1.0\"?>\n");

	if (showtext)
		sheet = fz_new_text_sheet(ctx);

	if (showtext == TEXT_HTML)
	{
		fz_printf(out, "<style>\n");
		fz_printf(out, "body{background-color:gray;margin:12tp;}\n");
		fz_printf(out, "div.page{background-color:white;margin:6pt;padding:6pt;}\n");
		fz_printf(out, "div.block{border:1px solid gray;margin:6pt;padding:6pt;}\n");
		fz_printf(out, "p{margin:0;padding:0;}\n");
		fz_printf(out, "</style>\n");
		fz_printf(out, "<body>\n");
	}

	fz_try(ctx)
	{
		while (fz_optind < argc)
		{
			fz_try(ctx)
			{
				filename = argv[fz_optind++];
				files++;

				fz_try(ctx)
				{
					doc = fz_open_document(ctx, filename);
				}
				fz_catch(ctx)
				{
					fz_throw(ctx, "cannot open document: %s", filename);
				}

				if (fz_needs_password(doc))
				{
					if (!fz_authenticate_password(doc, password))
						fz_throw(ctx, "cannot authenticate password: %s", filename);
					if (mujstest_file)
						fprintf(mujstest_file, "PASSWORD %s\n", password);
				}

				if (mujstest_file)
				{
					fprintf(mujstest_file, "OPEN %s\n", filename);
				}

				if (showxml || showtext == TEXT_XML)
					fz_printf(out, "<document name=\"%s\">\n", filename);

				if (showoutline)
					drawoutline(ctx, doc);

				if (showtext || showxml || showtime || showmd5 || output || mujstest_file)
				{
					if (fz_optind == argc || !isrange(argv[fz_optind]))
						drawrange(ctx, doc, "1-");
					if (fz_optind < argc && isrange(argv[fz_optind]))
						drawrange(ctx, doc, argv[fz_optind++]);
				}

				if (showxml || showtext == TEXT_XML)
					fz_printf(out, "</document>\n");

				fz_close_document(doc);
				doc = NULL;
			}
			fz_catch(ctx)
			{
				if (!ignore_errors)
					fz_rethrow(ctx);

				fz_close_document(doc);
				doc = NULL;
				fz_warn(ctx, "ignoring error in '%s'", filename);
			}
		}
	}
	fz_catch(ctx)
	{
		fz_close_document(doc);
		fprintf(stderr, "error: cannot draw '%s'\n", filename);
		errored = 1;
	}

	if (showtext == TEXT_HTML)
	{
		fz_printf(out, "</body>\n");
		fz_printf(out, "<style>\n");
		fz_print_text_sheet(ctx, out, sheet);
		fz_printf(out, "</style>\n");
	}

	if (showtext)
		fz_free_text_sheet(ctx, sheet);

	if (showxml || showtext)
	{
		fz_close_output(out);
		out = NULL;
	}

	if (showtime && timing.count > 0)
	{
		if (files == 1)
		{
			printf("total %dms / %d pages for an average of %dms\n",
				timing.total, timing.count, timing.total / timing.count);
			printf("fastest page %d: %dms\n", timing.minpage, timing.min);
			printf("slowest page %d: %dms\n", timing.maxpage, timing.max);
		}
		else
		{
			printf("total %dms / %d pages for an average of %dms in %d files\n",
				timing.total, timing.count, timing.total / timing.count, files);
			printf("fastest page %d: %dms (%s)\n", timing.minpage, timing.min, timing.minfilename);
			printf("slowest page %d: %dms (%s)\n", timing.maxpage, timing.max, timing.maxfilename);
		}
	}

	if (mujstest_file && mujstest_file != stdout)
		fclose(mujstest_file);

	fz_free_context(ctx);
	return (errored != 0);
}
