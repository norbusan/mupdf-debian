/*
 * pdfdraw:
 *   Draw pages to PPM bitmaps.
 *   Dump parsed display list as XML.
 *   Dump text content as UTF-8.
 *   Benchmark rendering speed.
 */

#include "pdftool.h"

#ifdef _MSC_VER
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

static void showsafe(unsigned char *buf, int n)
{
	int showcolumn = 0;
	int i;
	for (i = 0; i < n; i++) {
		if (buf[i] == '\r' || buf[i] == '\n') {
			putchar('\n');
			showcolumn = 0;
		}
		else if (buf[i] < 32 || buf[i] > 126) {
			putchar('.');
			showcolumn ++;
		}
		else {
			putchar(buf[i]);
			showcolumn ++;
		}
		if (showcolumn == 79) {
			putchar('\n');
			showcolumn = 0;
		}
	}
}

enum { DRAWPNM, DRAWTXT, DRAWXML };

struct benchmark
{
	int pages;
	long min;
	int minpage;
	long avg;
	long max;
	int maxpage;
};

static fz_glyphcache *drawcache = nil;
static int drawmode = DRAWPNM;
static char *drawpattern = nil;
static pdf_page *drawpage = nil;
static float drawzoom = 1.0;
static int drawrotate = 0;
static int drawbands = 1;
static int drawcount = 0;
static int benchmark = 0;
static int checksum = 0;

static void local_cleanup(void)
{
	if (xref && xref->store)
	{
		pdf_dropstore(xref->store);
		xref->store = nil;
	}
	if (drawcache)
	{
		fz_freeglyphcache(drawcache);
		drawcache = nil;
	}
}

static void drawusage(void)
{
	fprintf(stderr,
		"usage: pdfdraw [options] [file.pdf pages ... ]\n"
		"  -p -\tpassword for decryption\n"
		"  -o -\tpattern (%%d for page number) for output file\n"
		"  -r -\tresolution in dpi\n"
		"  -m\tprint benchmark results\n"
		"  -s\tprint MD5 checksum of page pixel data\n"
		"  -t\ttext extraction made\n"
		"  -x\txml trace mode\n"
		"  example:\n"
		"    pdfdraw -o output%%03d.pnm input.pdf 1-3,5,9-\n");
	exit(1);
}

static void gettime(long *time_)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0)
		abort();

	*time_ = tv.tv_sec * 1000000 + tv.tv_usec;
}

static void drawloadpage(int pagenum, struct benchmark *loadtimes)
{
	fz_error error;
	fz_obj *pageobj;
	long start;
	long end;
	long elapsed;

	fprintf(stdout, "draw %s:%03d ", basename, pagenum);
	if (benchmark && loadtimes)
	{
		fflush(stdout);
		gettime(&start);
	}

	pageobj = pdf_getpageobject(xref, pagenum);
	error = pdf_loadpage(&drawpage, xref, pageobj);
	if (error)
		die(error);

	if (benchmark && loadtimes)
	{
		gettime(&end);
		elapsed = end - start;

		if (elapsed < loadtimes->min)
		{
			loadtimes->min = elapsed;
			loadtimes->minpage = pagenum;
		}
		if (elapsed > loadtimes->max)
		{
			loadtimes->max = elapsed;
			loadtimes->maxpage = pagenum;
		}
		loadtimes->avg += elapsed;
		loadtimes->pages++;
	}

	if (benchmark)
		fflush(stdout);
}

static void drawfreepage(void)
{
	pdf_droppage(drawpage);
	drawpage = nil;

	flushxref();

	/* Flush resources between pages.
	 * TODO: should check memory usage before deciding to do this.
	 */
	if (xref && xref->store)
	{
		/* pdf_debugstore(xref->store); */
		pdf_agestoreditems(xref->store);
		pdf_evictageditems(xref->store);
		fflush(stdout);
	}
}

static void drawpnm(int pagenum, struct benchmark *loadtimes, struct benchmark *drawtimes)
{
	static int fd = -1;
	fz_error error;
	fz_matrix ctm;
	fz_bbox bbox;
	fz_pixmap *pix;
	char name[256];
	char pnmhdr[256];
	int i, x, y, w, h, b, bh;
	long start;
	long end;
	long elapsed;
	fz_md5 digest;

	if (checksum)
		fz_md5init(&digest);

	drawloadpage(pagenum, loadtimes);

	if (benchmark)
		gettime(&start);

	ctm = fz_identity();
	ctm = fz_concat(ctm, fz_translate(0, -drawpage->mediabox.y1));
	ctm = fz_concat(ctm, fz_scale(drawzoom, -drawzoom));
	ctm = fz_concat(ctm, fz_rotate(drawrotate + drawpage->rotate));

	bbox = fz_roundrect(fz_transformrect(ctm, drawpage->mediabox));
	w = bbox.x1 - bbox.x0;
	h = bbox.y1 - bbox.y0;
	bh = h / drawbands;

	if (drawpattern)
	{
		if (strchr(drawpattern, '%') || fd < 0)
		{
			sprintf(name, drawpattern, drawcount++);
			fd = open(name, O_BINARY|O_WRONLY|O_CREAT|O_TRUNC, 0666);
			if (fd < 0)
				die(fz_throw("ioerror: could not open file '%s'", name));
		}

		sprintf(pnmhdr, "P6\n%d %d\n255\n", w, h);
		write(fd, pnmhdr, strlen(pnmhdr));
	}

	pix = fz_newpixmap(pdf_devicergb, bbox.x0, bbox.y0, w, bh);
	fz_clearpixmap(pix, 0xFF);

	memset(pix->samples, 0xff, pix->h * pix->w * pix->n);

	for (b = 0; b < drawbands; b++)
	{
		fz_device *dev;

		if (drawbands > 1)
			fprintf(stdout, "drawing band %d / %d\n", b + 1, drawbands);

		dev = fz_newdrawdevice(drawcache, pix);
		error = pdf_runcontentstream(dev, ctm, xref, drawpage->resources, drawpage->contents);
		if (error)
			die(error);
		fz_freedevice(dev);

		if (drawpattern)
		{
			for (y = 0; y < pix->h; y++)
			{
				unsigned char *src = pix->samples + y * pix->w * 4;
				unsigned char *dst = src;

				for (x = 0; x < pix->w; x++)
				{
					dst[x * 3 + 0] = src[x * 4 + 1];
					dst[x * 3 + 1] = src[x * 4 + 2];
					dst[x * 3 + 2] = src[x * 4 + 3];
				}

				write(fd, dst, pix->w * 3);
			}
		}

		if (checksum)
			fz_md5update(&digest, pix->samples, pix->h * pix->w * 4);

		pix->y += bh;
		if (pix->y + pix->h > bbox.y1)
			pix->h = bbox.y1 - pix->y;
	}

	fz_droppixmap(pix);

	if (checksum)
	{
		unsigned char buf[16];
		fz_md5final(&digest, buf);
		for (i = 0; i < 16; i++)
			fprintf(stdout, "%02x", buf[i]);
		fprintf(stdout, " ");
	}

	if (drawpattern && strchr(drawpattern, '%'))
		close(fd);

	drawfreepage();

	if (benchmark)
	{
		gettime(&end);
		elapsed = end - start;

		if (elapsed < drawtimes->min)
		{
			drawtimes->min = elapsed;
			drawtimes->minpage = pagenum;
		}
		if (elapsed > drawtimes->max)
		{
			drawtimes->max = elapsed;
			drawtimes->maxpage = pagenum;
		}
		drawtimes->avg += elapsed;
		drawtimes->pages++;

		fprintf(stdout, "time %.3fs",
			elapsed / 1000000.0);
	}

	fprintf(stdout, "\n");
}

static void drawtxt(int pagenum)
{
	fz_error error;
	fz_matrix ctm;
	fz_obj *pageobj;
	fz_textspan *text;
	fz_device *dev;

	pageobj = pdf_getpageobject(xref, pagenum);
	error = pdf_loadpage(&drawpage, xref, pageobj);
	if (error)
		die(error);

	ctm = fz_identity();

	text = fz_newtextspan();
	dev = fz_newtextdevice(text);

	error = pdf_runcontentstream(dev, ctm, xref, drawpage->resources, drawpage->contents);
	if (error)
		die(error);

	printf("[Page %d]\n", pagenum);
	fz_debugtextspan(text);
	printf("\n");

	fz_freedevice(dev);
	fz_freetextspan(text);
}

static void drawxml(int pagenum)
{
	fz_error error;
	fz_obj *pageobj;
	fz_matrix ctm;
	fz_device *dev;

	pageobj = pdf_getpageobject(xref, pagenum);
	error = pdf_loadpage(&drawpage, xref, pageobj);
	if (error)
		die(error);

	ctm = fz_identity();

	dev = fz_newtracedevice();
	printf("<?xml version=\"1.0\"?>\n");
	printf("<page number=\"%d\">\n", pagenum);

	error = pdf_runcontentstream(dev, ctm, xref, drawpage->resources, drawpage->contents);
	if (error)
		die(error);

	fz_freedevice(dev);

	printf("</page>\n");
}

static void drawpages(char *pagelist)
{
	int page, spage, epage;
	char *spec, *dash;
	struct benchmark loadtimes, drawtimes;

	if (!xref)
		drawusage();

	if (benchmark)
	{
		memset(&loadtimes, 0x00, sizeof (loadtimes));
		loadtimes.min = LONG_MAX;
		memset(&drawtimes, 0x00, sizeof (drawtimes));
		drawtimes.min = LONG_MAX;
	}

	spec = fz_strsep(&pagelist, ",");
	while (spec)
	{
		dash = strchr(spec, '-');

		if (dash == spec)
			spage = epage = 1;
		else
			spage = epage = atoi(spec);

		if (dash)
		{
			if (strlen(dash) > 1)
				epage = atoi(dash + 1);
			else
				epage = pagecount;
		}

		if (spage > epage)
			page = spage, spage = epage, epage = page;

		if (spage < 1)
			spage = 1;
		if (epage > pagecount)
			epage = pagecount;

		for (page = spage; page <= epage; page++)
		{
			switch (drawmode)
			{
			case DRAWPNM: drawpnm(page, &loadtimes, &drawtimes); break;
			case DRAWTXT: drawtxt(page); break;
			case DRAWXML: drawxml(page); break;
			}
		}

		spec = fz_strsep(&pagelist, ",");
	}

	if (benchmark)
	{
		if (loadtimes.pages > 0)
		{
			loadtimes.avg /= loadtimes.pages;
			drawtimes.avg /= drawtimes.pages;

			printf("benchmark[load]: min: %6.3fs (page % 4d), avg: %6.3fs, max: %6.3fs (page % 4d)\n",
				loadtimes.min / 1000000.0, loadtimes.minpage,
				loadtimes.avg / 1000000.0,
				loadtimes.max / 1000000.0, loadtimes.maxpage);
			printf("benchmark[draw]: min: %6.3fs (page % 4d), avg: %6.3fs, max: %6.3fs (page % 4d)\n",
				drawtimes.min / 1000000.0, drawtimes.minpage,
				drawtimes.avg / 1000000.0,
				drawtimes.max / 1000000.0, drawtimes.maxpage);
		}
	}
}

int main(int argc, char **argv)
{
	char *password = "";
	int c;
	enum { NO_FILE_OPENED, NO_PAGES_DRAWN, DREW_PAGES } state;

	fz_cpudetect();
	fz_accelerate();

	while ((c = fz_getopt(argc, argv, "b:p:o:r:txms")) != -1)
	{
		switch (c)
		{
		case 'b': drawbands = atoi(fz_optarg); break;
		case 'p': password = fz_optarg; break;
		case 'o': drawpattern = fz_optarg; break;
		case 'r': drawzoom = atof(fz_optarg) / 72.0; break;
		case 't': drawmode = DRAWTXT; break;
		case 'x': drawmode = DRAWXML; break;
		case 'm': benchmark = 1; break;
		case 's': checksum = 1; break;
		default:
			drawusage();
			break;
		}
	}

	if (fz_optind == argc)
		drawusage();

	setcleanup(local_cleanup);

	state = NO_FILE_OPENED;
	while (fz_optind < argc)
	{
		if (strstr(argv[fz_optind], ".pdf") || strstr(argv[fz_optind], ".PDF"))
		{
			if (state == NO_PAGES_DRAWN)
				drawpages("1-");

			closexref();

			drawcache = fz_newglyphcache();

			openxref(argv[fz_optind], password, 0);
			state = NO_PAGES_DRAWN;
		}
		else
		{
			drawpages(argv[fz_optind]);
			state = DREW_PAGES;
		}
		fz_optind++;
	}

	if (state == NO_PAGES_DRAWN)
		drawpages("1-");

	closexref();
}

