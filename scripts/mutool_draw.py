import collections
import getopt
import os
import re
import sys
import time

import mupdf


# Force stderr to be line-buffered - i.e. python will flush to the underlying
# stderr stream every newline. This ensures that our output interleaves with
# the output of mupdf C code, making it easier to compare our output with that
# of mutool.
#
sys.stderr = os.fdopen( os.dup( sys.stderr.fileno()), 'w', 1)


OUT_NONE    = 0
OUT_PNG     = 1
OUT_PNM     = 2
OUT_PGM     = 3
OUT_PPM     = 4
OUT_PAM     = 5
OUT_PBM     = 6
OUT_PKM     = 7
OUT_PWG     = 8
OUT_PCL     = 9
OUT_PS      = 10
OUT_PSD     = 11
OUT_TEXT    = 12
OUT_HTML    = 13
OUT_XHTML   = 14
OUT_STEXT   = 15
OUT_PCLM    = 16
OUT_TRACE   = 17
OUT_BBOX    = 18
OUT_SVG     = 19
OUT_XMLTEXT = 20

CS_INVALID      = 0
CS_UNSET        = 1
CS_MONO         = 2
CS_GRAY         = 3
CS_GRAY_ALPHA   = 4
CS_RGB          = 5
CS_RGB_ALPHA    = 6
CS_CMYK         = 7
CS_CMYK_ALPHA   = 8
CS_ICC          = 9

CS_INVALID      = 0
CS_UNSET        = 1
CS_MONO         = 2
CS_GRAY         = 3
CS_GRAY_ALPHA   = 4
CS_RGB          = 5
CS_RGB_ALPHA    = 6
CS_CMYK         = 7
CS_CMYK_ALPHA   = 8
CS_ICC          = 9

SPOTS_NONE          = 0
SPOTS_OVERPRINT_SIM = 1
SPOTS_FULL          = 2


class suffix_t:
    def __init__( self, suffix, format_, spots):
        self.suffix = suffix
        self.format = format_
        self.spots = spots

suffix_table = [
        suffix_t( ".png", OUT_PNG, 0 ),
        suffix_t( ".pgm", OUT_PGM, 0 ),
        suffix_t( ".ppm", OUT_PPM, 0 ),
        suffix_t( ".pnm", OUT_PNM, 0 ),
        suffix_t( ".pam", OUT_PAM, 0 ),
        suffix_t( ".pbm", OUT_PBM, 0 ),
        suffix_t( ".pkm", OUT_PKM, 0 ),
        suffix_t( ".svg", OUT_SVG, 0 ),
        suffix_t( ".pwg", OUT_PWG, 0 ),
        suffix_t( ".pclm", OUT_PCLM, 0 ),
        suffix_t( ".pcl", OUT_PCL, 0 ),
        suffix_t( ".psd", OUT_PSD, 1 ),
        suffix_t( ".ps", OUT_PS, 0 ),

        suffix_t( ".txt", OUT_TEXT, 0 ),
        suffix_t( ".text", OUT_TEXT, 0 ),
        suffix_t( ".html", OUT_HTML, 0 ),
        suffix_t( ".xhtml", OUT_XHTML, 0 ),
        suffix_t( ".stext", OUT_STEXT, 0 ),

        suffix_t( ".trace", OUT_TRACE, 0 ),
        suffix_t( ".raw", OUT_XMLTEXT, 0 ),
        suffix_t( ".bbox", OUT_BBOX, 0 ),
        ]

class cs_name_t:
    def __init__( self, name, colorspace):
        self.name = name
        self.colorspace = colorspace

cs_name_table = dict(
        m           = CS_MONO,
        mono        = CS_MONO,
        g           = CS_GRAY,
        gray        = CS_GRAY,
        grey        = CS_GRAY,
        ga          = CS_GRAY_ALPHA,
        grayalpha   = CS_GRAY_ALPHA,
        greyalpha   = CS_GRAY_ALPHA,
        rgb         = CS_RGB,
        rgba        = CS_RGB_ALPHA,
        rgbalpha    = CS_RGB_ALPHA,
        cmyk        = CS_CMYK,
        cmyka       = CS_CMYK_ALPHA,
        cmykalpha   = CS_CMYK_ALPHA,
        )


class format_cs_table_t:
    def __init__( self, format_, default_cs, permitted_cs):
        self.format = format_
        self.default_cs = default_cs
        self.permitted_cs = permitted_cs

format_cs_table = [
        format_cs_table_t( OUT_PNG, CS_RGB, [ CS_GRAY, CS_GRAY_ALPHA, CS_RGB, CS_RGB_ALPHA, CS_ICC ] ),
        format_cs_table_t( OUT_PPM, CS_RGB, [ CS_GRAY, CS_RGB ] ),
        format_cs_table_t( OUT_PNM, CS_GRAY, [ CS_GRAY, CS_RGB ] ),
        format_cs_table_t( OUT_PAM, CS_RGB_ALPHA, [ CS_GRAY, CS_GRAY_ALPHA, CS_RGB, CS_RGB_ALPHA, CS_CMYK, CS_CMYK_ALPHA ] ),
        format_cs_table_t( OUT_PGM, CS_GRAY, [ CS_GRAY, CS_RGB ] ),
        format_cs_table_t( OUT_PBM, CS_MONO, [ CS_MONO ] ),
        format_cs_table_t( OUT_PKM, CS_CMYK, [ CS_CMYK ] ),
        format_cs_table_t( OUT_PWG, CS_RGB, [ CS_MONO, CS_GRAY, CS_RGB, CS_CMYK ] ),
        format_cs_table_t( OUT_PCL, CS_MONO, [ CS_MONO, CS_RGB ] ),
        format_cs_table_t( OUT_PCLM, CS_RGB, [ CS_RGB, CS_GRAY ] ),
        format_cs_table_t( OUT_PS, CS_RGB, [ CS_GRAY, CS_RGB, CS_CMYK ] ),
        format_cs_table_t( OUT_PSD, CS_CMYK, [ CS_GRAY, CS_GRAY_ALPHA, CS_RGB, CS_RGB_ALPHA, CS_CMYK, CS_CMYK_ALPHA, CS_ICC ] ),

        format_cs_table_t( OUT_TRACE, CS_RGB, [ CS_RGB ] ),
        format_cs_table_t( OUT_XMLTEXT, CS_RGB, [ CS_RGB ] ),
        format_cs_table_t( OUT_BBOX, CS_RGB, [ CS_RGB ] ),
        format_cs_table_t( OUT_SVG, CS_RGB, [ CS_RGB ] ),

        format_cs_table_t( OUT_TEXT, CS_RGB, [ CS_RGB ] ),
        format_cs_table_t( OUT_HTML, CS_RGB, [ CS_RGB ] ),
        format_cs_table_t( OUT_XHTML, CS_RGB, [ CS_RGB ] ),
        format_cs_table_t( OUT_STEXT, CS_RGB, [ CS_RGB ] ),
        ]

def stat_mtime(path):
    try:
        return os.path.getmtime(path)
    except Exception:
        return 0


class worker_t:
    def __init__( self):
        self.num = 0
        self.band = 0
        self.list = None
        self.ctm = None
        self.tbounds = None
        self.pix = None
        self.bit = None
        self.cookie = mupdf.Cookie()

class state:

    output = None
    out = None
    output_pagenum = 0
    output_file_per_page = 0

    format_ = None
    output_format = OUT_NONE

    rotation = 0
    resolution = 72
    res_specified = 0
    width = 0
    height = 0
    fit = 0

    layout_w = mupdf.FZ_DEFAULT_LAYOUT_W
    layout_h = mupdf.FZ_DEFAULT_LAYOUT_H
    layout_em = mupdf.FZ_DEFAULT_LAYOUT_EM
    layout_css = None
    layout_use_doc_css = 1
    min_line_width = 0.0

    showfeatures = 0
    showtime = 0
    showmemory = 0
    showmd5 = 0

    no_icc = 0
    ignore_errors = 0
    uselist = 1
    alphabits_text = 8
    alphabits_graphics = 8

    out_cs = CS_UNSET
    proof_filename = None
    proof_cs = mupdf.Colorspace()
    icc_filename = None
    gamma_value = 1
    invert = 0
    band_height = 0
    lowmemory = 0

    quiet = 0
    errored = 0
    colorspace = mupdf.Colorspace()
    oi = None
    spots = SPOTS_OVERPRINT_SIM
    alpha = 0
    useaccel = 1
    filename = None
    files = 0
    num_workers = 0
    workers = None
    bander = None

    layer_config = None


class bgprint:
    active = 0
    started = 0
    pagenum = 0
    filename = None
    list_ = None
    page = None
    interptime = 0
    seps = None


class timing:
    count = 0
    total = 0
    min_ = 0
    max_ = 0
    mininterp = 0
    maxinterp = 0
    minpage = 0
    maxpage = 0
    minfilename = None
    maxfilename = None
    layout = 0
    minlayout = 0
    maxlayout = 0
    minlayoutfilename = None
    maxlayoutfilename = None



def usage():
    sys.stderr.write( f'''
            mudraw version {mupdf.FZ_VERSION} "
            Usage: mudraw [options] file [pages]
            \t-p -\tpassword

            \t-o -\toutput file name (%d for page number)
            \t-F -\toutput format (default inferred from output file name)
            \t\traster: png, pnm, pam, pbm, pkm, pwg, pcl, ps
            \t\tvector: svg, pdf, trace
            \t\ttext: txt, html, stext

            \t-q\tbe quiet (don't print progress messages)
            \t-s -\tshow extra information:
            \t\tm - show memory use
            \t\tt - show timings
            \t\tf - show page features
            \t\t5 - show md5 checksum of rendered image

            \t-R -\trotate clockwise (default: 0 degrees)
            \t-r -\tresolution in dpi (default: 72)
            \t-w -\twidth (in pixels) (maximum width if -r is specified)
            \t-h -\theight (in pixels) (maximum height if -r is specified)
            \t-f -\tfit width and/or height exactly; ignore original aspect ratio
            \t-B -\tmaximum band_height (pXm, pcl, pclm, ps, psd and png output only)
            \t-T -\tnumber of threads to use for rendering (banded mode only)

            \t-W -\tpage width for EPUB layout
            \t-H -\tpage height for EPUB layout
            \t-S -\tfont size for EPUB layout
            \t-U -\tfile name of user stylesheet for EPUB layout
            \t-X\tdisable document styles for EPUB layout
            \t-a\tdisable usage of accelerator file

            \t-c -\tcolorspace (mono, gray, grayalpha, rgb, rgba, cmyk, cmykalpha, filename of ICC profile)
            \t-e -\tproof icc profile (filename of ICC profile)
            \t-G -\tapply gamma correction
            \t-I\tinvert colors

            \t-A -\tnumber of bits of antialiasing (0 to 8)
            \t-A -/-\tnumber of bits of antialiasing (0 to 8) (graphics, text)
            \t-l -\tminimum stroked line width (in pixels)
            \t-D\tdisable use of display list
            \t-i\tignore errors
            \t-L\tlow memory mode (avoid caching, clear objects after each page)
            \t-P\tparallel interpretation/rendering
            \t-N\tdisable ICC workflow (\"N\"o color management)
            \t-O -\tControl spot/overprint rendering
            \t\t 0 = No spot rendering
            \t\t 1 = Overprint simulation (default)
            \t\t 2 = Full spot rendering

            \t-y l\tList the layer configs to stderr
            \t-y -\tSelect layer config (by number)
            \t-y -{{,-}}*\tSelect layer config (by number), and toggle the listed entries

            \tpages\tcomma separated list of page numbers and ranges
            ''')
    sys.exit(1)


gettime_first = None

def gettime():
    global gettime_first
    if gettime_first is None:
        gettime_first = time.time()

    now = time.time()
    return (now - gettime_first) * 1000


def has_percent_d(s):
    # find '%[0-9]*d' */
    m = re.search( '%[0-9]*d', s)
    if m:
        return 1
    return 0



# Output file level (as opposed to page level) headers
def file_level_headers():

    if state.output_format in (OUT_STEXT, OUT_TRACE, OUT_XMLTEXT, OUT_BBOX):
        state.out.write_string( "<?xml version=\"1.0\"?>\n")

    if state.output_format == OUT_HTML:
        state.out.print_stext_header_as_html()
    if state.output_format == OUT_XHTML:
        state.out.print_stext_header_as_xhtml()

    if state.output_format in (OUT_STEXT, OUT_TRACE, OUT_BBOX):
        state.out.write_string( f'<document name="{state.filename}">\n')

    if state.output_format == OUT_PS:
        state.out.write_ps_file_header()

    if state.output_format == OUT_PWG:
        state.out.write_pwg_file_header()

    if state.output_format == OUT_PCLM:
        opts = mupdf.PclmOptions( 'compression=flate')
        state.bander = mupdf.BandWriter(state.out, opts)

def file_level_trailers():
    if state.output_format in (OUT_STEXT, OUT_TRACE, OUT_BBOX):
        state.out.write_string( "</document>\n")

    if state.output_format == OUT_HTML:
        state.out.print_stext_trailer_as_html()
    if state.output_format == OUT_XHTML:
        state.out.print_stext_trailer_as_xhtml()

    if state.output_format == OUT_PS:
        state.out.write_ps_file_trailer( state.output_pagenum)

def drawband( page, list_, ctm, tbounds, cookie, band_start, pix):

    bit = None

    if pix.alpha():
        pix.clear_pixmap()
    else:
        pix.clear_pixmap_with_value( 255)

    dev = mupdf.Device( mupdf.Matrix(), pix, state.proof_cs)
    if state.lowmemory:
        dev.enable_device_hints( mupdf.FZ_NO_CACHE)
    if state.alphabits_graphics == 0:
        dev.enable_device_hints( mupdf.FZ_DONT_INTERPOLATE_IMAGES)
    if list_:
        list_.run_display_list( dev, ctm, tbounds, cookie)
    else:
        page.run( dev, ctm, cookie)
    dev.close_device()
    dev = None

    if state.invert:
        pix.invert_pixmap()
    if state.gamma_value != 1:
        pix.gamma_pixmap( state.gamma_value)

    if ((state.output_format == OUT_PCL or state.output_format == OUT_PWG) and state.out_cs == CS_MONO) or (state.output_format == OUT_PBM) or (state.output_format == OUT_PKM):
        bit = mupdf.Bitmap( pix, mupdf.Halftone(), band_start)
    return bit






def dodrawpage( page, list_, pagenum, cookie, start, interptime, filename, bg, seps):

    if state.output_file_per_page:
        file_level_headers()

    if list_:
        mediabox = mupdf.Rect( list_)
    else:
        mediabox = page.bound_page()

    if state.output_format == OUT_TRACE:
        state.out.write_string( "<page mediabox=\"%g %g %g %g\">\n" % (
                mediabox.x0, mediabox.y0, mediabox.x1, mediabox.y1))
        dev = mupdf.Device( state.out)
        if state.lowmemory:
            dev.enable_device_hints( mupdf.FZ_NO_CACHE)
        if list_:
            list_.run_display_list( dev, mupdf.Matrix(), mupdf.Rect(mupdf.fz_infinite_rect), cookie)
        else:
            page.run( dev, fz_identity, cookie)
        state.out.write_string( "</page>\n")
        dev.close_device()
        dev = None

    elif state.output_format == OUT_XMLTEXT:
        state.out.write_string( "<page mediabox=\"%g %g %g %g\">\n" % (
                mediabox.x0, mediabox.y0, mediabox.x1, mediabox.y1))
        dev = mupdf.Device.new_raw_device( state.out)
        if list_:
            list_.run_display_list( dev, mupdf.Matrix(), mupdf.Rect(mupdf.fz_infinite_rect), cookie)
        else:
            page.run( dev, fz_identity, cookie)
        state.out.write_string( "</page>\n")
        dev.close_device()
        dev = None

    elif state.output_format == OUT_BBOX:
        bbox = mupdf.Rect( mupdf.Rect.Fixed_EMPTY)
        dev = mupdf.Device( bbox)
        if state.lowmemory:
            dev.enable_device_hints( mupdf.FZ_NO_CACHE)
        if list_:
            list_.run_display_list( dev, fz_identity, mupdf.Rect(mupdf.fz_infinite_rect), cookie)
        else:
            page.run( dev, fz_identity, cookie)
        dev.close_device()
        state.out.write_string( "<page bbox=\"%s %s %s %s\" mediabox=\"%s %s %s %s\" />\n",
                bbox.x0,
                bbox.y0,
                bbox.x1,
                bbox.y1,
                mediabox.x0,
                mediabox.y0,
                mediabox.x1,
                mediabox.y1,
                )

    elif state.output_format in (OUT_TEXT, OUT_HTML, OUT_XHTML, OUT_STEXT):
        zoom = state.resolution / 72
        ctm = mupdf.Matrix(mupdf.pre_scale(mupdf.rotate(state.rotation), zoom, zoom))

        stext_options = mupdf.StextOptions()

        stext_options.flags = mupdf.FZ_STEXT_PRESERVE_IMAGES if (state.output_format == OUT_HTML or state.output_format == OUT_XHTML) else 0
        text = mupdf.StextPage( mediabox)
        dev = mupdf.Device( text, stext_options)
        if state.lowmemory:
            fz_enable_device_hints( dev, FZ_NO_CACHE)
        if list_:
            list_.run_display_list( dev, ctm, mupdf.Rect(mupdf.fz_infinite_rect), cookie)
        else:
            page.run( dev, ctm, cookie)
        dev.close_device()
        dev = None
        if state.output_format == OUT_STEXT:
            state.out.print_stext_page_as_xml( text, pagenum)
        elif state.output_format == OUT_HTML:
            state.out.print_stext_page_as_html( text, pagenum)
        elif state.output_format == OUT_XHTML:
            state.out.print_stext_page_as_xhtml( text, pagenum)
        elif state.output_format == OUT_TEXT:
            state.out.print_stext_page_as_text( text)
            state.out.write_string( "\f\n")

    elif state.output_format == OUT_SVG:
        zoom = state.resolution / 72
        ctm = mupdf.Matrix(zoom, zoom)
        ctm.pre_rotate( state.rotation)
        tbounds = mupdf.Rect(mediabox, ctm)

        if not state.output or state.output == "-":
            state.out = mupdf.Output( mupdf.Output.Fixed_STDOUT)
        else:
            buf = mupdf.format_output_path( state.output, pagenum)
            state.out = mupdf.Output( buf, 0)

        dev = mupdf.Device( state.out, tbounds.x1-tbounds.x0, tbounds.y1-tbounds.y0, mupdf.FZ_SVG_TEXT_AS_PATH, 1)
        if state.lowmemory:
            dev.enable_device_hints( dev, mupdf.FZ_NO_CACHE)
        if list_:
            list_.run_display_list( dev, ctm, tbounds, cookie)
        else:
            page.run( dev, ctm, cookie)
        dev.close_device()
        state.out.close_output()
    else:
        zoom = state.resolution / 72
        m = mupdf.rotate(state.rotation)
        ctm = mupdf.Matrix( mupdf.pre_scale( mupdf.rotate(state.rotation), zoom, zoom))
        tbounds = mupdf.Rect(mediabox, ctm)
        ibounds = tbounds.round_rect()

        # Make local copies of our width/height
        w = state.width
        h = state.height

        # If a resolution is specified, check to see whether w/h are
        # exceeded; if not, unset them. */
        if state.res_specified:
            t = ibounds.x1 - ibounds.x0
            if w and t <= w:
                w = 0
            t = ibounds.y1 - ibounds.y0
            if h and t <= h:
                h = 0

        # Now w or h will be 0 unless they need to be enforced.
        if w or h:
            scalex = w / (tbounds.x1 - tbounds.x0)
            scaley = h / (tbounds.y1 - tbounds.y0)

            if state.fit:
                if w == 0:
                    scalex = 1.0
                if h == 0:
                    scaley = 1.0
            else:
                if w == 0:
                    scalex = scaley
                if h == 0:
                    scaley = scalex
            if not state.fit:
                if scalex > scaley:
                    scalex = scaley
                else:
                    scaley = scalex
            scale_mat = mupdf.Matrix.scale(scalex, scaley)
            ctm = mupdf.Matrix( mupdf.concat(ctm.internal(), scale_mat.internal()))
            tbounds = mupdf.Rect( mediabox, ctm)
        ibounds = tbounds.round_rect()
        tbounds = ibounds.rect_from_irect()

        band_ibounds = ibounds
        bands = 1
        totalheight = ibounds.y1 - ibounds.y0
        drawheight = totalheight

        if state.band_height != 0:
            # Banded rendering; we'll only render to a
            # given height at a time.
            drawheight = state.band_height
            if totalheight > state.band_height:
                band_ibounds.y1 = band_ibounds.y0 + state.band_height
            bands = (totalheight + state.band_height-1)/state.band_height
            tbounds.y1 = tbounds.y0 + state.band_height + 2
            #DEBUG_THREADS(("Using %d Bands\n", bands));

        if state.num_workers > 0:
            for band in range( min(state.num_workers, bands)):
                state.workers[band].band = band
                state.workers[band].ctm = ctm
                state.workers[band].tbounds = tbounds
                state.workers[band].cookie = mupdf.Cookie()
                state.workers[band].list = list_
                state.workers[band].pix = mupdf.Pixmap( state.colorspace, band_ibounds, seps, state.alpha)
                state.workers[band].pix.set_pixmap_resolution( state.resolution, state.resolution)
                ctm.f -= drawheight
            pix = state.workers[0].pix
        else:
            pix = mupdf.Pixmap( state.colorspace, band_ibounds, seps, state.alpha)
            pix.set_pixmap_resolution( int(state.resolution), int(state.resolution))

        # Output any page level headers (for banded formats)
        if state.output:
            state.bander = None
            if state.output_format == OUT_PGM or state.output_format == OUT_PPM or state.output_format == OUT_PNM:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PNM)
            elif state.output_format == OUT_PAM:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PAM)
            elif state.output_format == OUT_PNG:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PNG)
            elif state.output_format == OUT_PBM:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PBM)
            elif state.output_format == OUT_PKM:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PKM)
            elif state.output_format == OUT_PS:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PS)
            elif state.output_format == OUT_PSD:
                state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.PSD)
            elif state.output_format == OUT_PWG:
                if state.out_cs == CS_MONO:
                    state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.MONO, mupdf.PwgOptions())
                else:
                    state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.COLOR, mupdf.PwgOptions())
            elif state.output_format == OUT_PCL:
                if state.out_cs == CS_MONO:
                    state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.MONO, mupdf.PclOptions())
                else:
                    state.bander = mupdf.BandWriter( state.out, mupdf.BandWriter.COLOR, mupdf.PclOptions())
            if state.bander:
                state.bander.write_header( pix.w(), totalheight, pix.n(), pix.alpha(), pix.xres(), pix.yres(), state.output_pagenum, pix.colorspace(), pix.seps())
                state.output_pagenum += 1

        for band in range( bands):
            if state.num_workers > 0:
                w = state.workers[band % state.num_workers]
                pix = w.pix
                bit = w.bit
                w.bit = None
                cookie.increment_errors(w.cookie.errors())

            else:
                bit = drawband( page, list_, ctm, tbounds, cookie, band * state.band_height, pix)

            if state.output:
                if state.bander:
                    if bit:
                        state.bander.write_band( bit.stride(), drawheight, bit.samples())
                    else:
                        state.bander.write_band( pix.stride(), drawheight, pix.samples())
                bit = None

            if state.num_workers > 0 and band + state.num_workers < bands:
                w = state.workers[band % state.num_workers]
                w.band = band + state.num_workers
                w.ctm = ctm
                w.tbounds = tbounds
                w.cookie = mupdf.Cookie()
            ctm.f -= drawheight

        # FIXME
        if state.showmd5:
            digest = pix.md5_pixmap()
            sys.stderr.write( ' ')
            for i in range(16):
                sys.stderr.write( '%02x', digest[i])

    if state.output_file_per_page:
        file_level_trailers()

    if state.showtime:
        end = gettime()
        diff = end - start

        if bg:
            if diff + interptime < timing.min:
                timing.min = diff + interptime
                timing.mininterp = interptime
                timing.minpage = pagenum
                timing.minfilename = filename
            if diff + interptime > timing.max:
                timing.max = diff + interptime
                timing.maxinterp = interptime
                timing.maxpage = pagenum
                timing.maxfilename = filename
            timing.count += 1

            sys.stderr.write( " %dms (interpretation) %dms (rendering) %dms (total)" % (interptime, diff, diff + interptime))
        else:
            if diff < timing.min:
                timing.min = diff
                timing.minpage = pagenum
                timing.minfilename = filename
            if diff > timing.max:
                timing.max = diff
                timing.maxpage = pagenum
                timing.maxfilename = filename

            timing.total += diff
            timing.count += 1

            sys.stderr.write( " %dms" % diff)

    if not state.quiet or state.showfeatures or state.showtime or state.showmd5:
        sys.stderr.write( "\n")

    if state.lowmemory:
        mupdf.empty_store()

    if state.showmemory:
        mupdf.dump_glyph_cache_stats(mupdf.stderr_())

    mupdf.flush_warnings()

    if cookie.get_errors():
        state.errored = 1



def bgprint_flush():
    if not bgprint.active or not bgprint.started:
        return
    bgprint.started = 0



def drawpage( doc, pagenum):
    list_ = None
    cookie = mupdf.Cookie()
    seps = None
    features = ""

    start = gettime() if state.showtime else 0

    page = mupdf.Page( doc, pagenum - 1)

    if state.spots != SPOTS_NONE:
        seps = page.page_separations()
        if seps.m_internal:
            n = seps.count_separations()
            if state.spots == SPOTS_FULL:
                for i in range(n):
                    seps.set_separation_behavior( i, mupdf.FZ_SEPARATION_SPOT)
            else:
                for i in range(n):
                    seps.set_separation_behavior( i, mupdf.FZ_SEPARATION_COMPOSITE)
        elif page.page_uses_overprint():
            # This page uses overprint, so we need an empty
            # sep object to force the overprint simulation on.
            seps = mupdf.Separations(0)
        elif state.oi and state.oi.m_internal and state.oi.colorspace_n() != state.colorspace.colorspace_n():
            # We have an output intent, and it's incompatible
            # with the colorspace our device needs. Force the
            # overprint simulation on, because this ensures that
            # we 'simulate' the output intent too. */
            seps = mupdf.Separations(0)

    if state.uselist:
        list_ = mupdf.DisplayList( page.bound_page())
        dev = mupdf.Device( list_)
        if state.lowmemory:
            dev.enable_device_hints( FZ_NO_CACHE)
        page.run( dev, mupdf.Matrix(), cookie)
        dev.close_device()

        if bgprint.active and state.showtime:
            end = gettime()
            start = end - start

    if state.showfeatures:
        # SWIG doesn't appear to handle the out-param is_color in
        # mupdf.Device() constructor that wraps fz_new_test_device(), so we use
        # the underlying mupdf function() instead.
        #
        dev, iscolor = mupdf.new_test_device( 0.02, 0, None)
        dev = mupdf.Device( dev)
        if state.lowmemory:
            dev.enable_device_hints( mupdf.FZ_NO_CACHE)
        if list_:
            list_.run_display_list( dev, mupdf.Matrix(mupdf.fz_identity), mupdf.Rect(mupdf.fz_infinite_rect), mupdf.Cookie())
        else:
            page.run( dev, fz_identity, cookie)
        dev.close_device()
        features = " color" if iscolor else " grayscale"

    if state.output_file_per_page:
        bgprint_flush()
        if state.out:
            state.out.close_output()
        text_buffer = mupdf.format_output_path( state.output, pagenum)
        state.out = mupdf.Output( text_buffer, 0)

    if bgprint.active:
        bgprint_flush()
        if bgprint.active:
            if not state.quiet or state.showfeatures or state.showtime or state.showmd5:
                sys.stderr.write( "page %s %d%s" % (state.filename, pagenum, features))

        bgprint.started = 1
        bgprint.page = page
        bgprint.list = list_
        bgprint.seps = seps
        bgprint.filename = state.filename
        bgprint.pagenum = pagenum
        bgprint.interptime = start
    else:
        if not state.quiet or state.showfeatures or state.showtime or state.showmd5:
            sys.stderr.write( "page %s %d%s" % (state.filename, pagenum, features))
        dodrawpage( page, list_, pagenum, cookie, start, 0, state.filename, 0, seps)











def drawrange( doc, range_):
    pagecount = doc.count_pages()

    while 1:
        range_, spage, epage = mupdf.parse_page_range( range_, pagecount)
        if range_ is None:
            break
        if spage < epage:
            for page in range(spage, epage+1):
                drawpage( doc, page)
        else:
            for page in range( spage, epage-1, -1):
                drawpage( doc, page)



def parse_colorspace( name):
    ret = cs_name_table.get( name)
    if ret:
        return ret
    state.icc_filename = name
    return CS_ICC


class trace_info:
    def __init__( self):
        self.current = 0
        self.peak = 0
        self.total = 0


def iswhite(ch):
    return (
        ch == '\011' or ch == '\012' or
        ch == '\014' or ch == '\015' or ch == '\040'
        )

def apply_layer_config( doc, lc):
    pass




def convert_to_accel_path(absname):
    tmpdir = os.getenv('TEMP')
    if  not tmpdir:
        tmpdir = os.getenv('TMP')
    if not tmpdir:
        tmpdir = '/var/tmp'
    if not os.path.isdir(tmpdir):
        tmpdir = '/tmp'

    if absname.startswith( '/') or absname.startswith( '\\'):
        absname = absname[1:]

    absname = absname.replace( '/', '%')
    absname = absname.replace( '\\', '%')
    absname = absname.replace( ':', '%')

    return '%s/%s.accel' % (tmpdir, absname)

def get_accelerator_filename( filename):
    absname = os.path.realpath( filename)
    return convert_to_accel_path( absname)

def save_accelerator(doc, filename):
    if not doc.document_supports_accelerator():
        return
    absname = get_accelerator_filename( filename)
    doc.save_accelerator( absname)


def draw( argv):

    password = ''
    info = trace_info()

    items, argv = getopt.getopt( argv, 'qp:o:F:R:r:w:h:fB:c:e:G:Is:A:DiW:H:S:T:U:XLvPl:y:NO:a')
    for option, value in items:
        if 0:   pass
        elif option == '-q':    state.quiet = 1
        elif option == '-p':    password = value
        elif option == '-o':    state.output = value
        elif option == '-F':    state.format_ = value
        elif option == '-R':    state.rotation = float( value)
        elif option == '-r':
            state.resolution = float( value)
            state.res_specified = 1
        elif option == '-w':    state.width = float( value)
        elif option == '-h':    state.height = float( value)
        elif option == '-f':    state.fit = 1
        elif option == '-B':    state.band_height = int( value)
        elif option == '-c':    state.out_cs = parse_colorspace( value)
        elif option == '-e':    state.proof_filename = value
        elif option == '-G':    state.gamma_value = float( value)
        elif option == '-I':    state.invert += 1
        elif option == '-W':    state.layout_w = float( value)
        elif option == '-H':    state.layout_h = float( value)
        elif option == '-S':    state.layout_em = float( value)
        elif option == '-U':    state.layout_css = value
        elif option == '-X':    state.layout_use_doc_css = 0
        elif option == '-O':
            state.spots = float( value)
            if not mupdf.FZ_ENABLE_SPOT_RENDERING:
                sys.stderr.write( 'Spot rendering/Overprint/Overprint simulation not enabled in this build\n')
                state.spots = SPOTS_NONE
        elif option == '-s':
            if 't' in value: state.showtime += 1
            if 'm' in value: state.showmemory += 1
            if 'f' in value: state.showfeatures += 1
            if '5' in value: state.showmd5 += 1

        elif option == '-A':
            state.alphabits_graphics = int(value)
            sep = value.find( '/')
            if sep >= 0:
                state.alphabits_text = int(value[sep+1:])
            else:
                state.alphabits_text = state.alphabits_graphics
        elif option == '-D': state.uselist = 0
        elif option == '-l': state.min_line_width = float(value)
        elif option == '-i': state.ignore_errors = 1
        elif option == '-N': state.no_icc = 1

        elif option == '-T': state.num_workers = int(value)
        elif option == '-L': state.lowmemory = 1
        elif option == '-P': bgprint.active = 1
        elif option == '-y': state.layer_config = value
        elif option == '-a': state.useaccel = 0

        elif option == '-v': sys.stderr.write( f'mudraw version {mupdf.FZ_VERSION}\n')

    if not argv:
        usage()

    if state.num_workers > 0:
        if state.uselist == 0:
            sys.stderr.write('cannot use multiple threads without using display list\n')
            sys.exit(1)

        if state.band_height == 0:
            sys.stderr.write('Using multiple threads without banding is pointless\n')

    if bgprint.active:
        if state.uselist == 0:
            sys.stderr.write('cannot bgprint without using display list\n')
            sys.exit(1)

    if state.proof_filename:
        proof_buffer = mupdf.Buffer( state.proof_filename)
        state.proof_cs = mupdf.Colorspace( FZ_COLORSPACE_NONE, 0, None, proof_buffer)

    mupdf.set_text_aa_level( state.alphabits_text)
    mupdf.set_graphics_aa_level( state.alphabits_graphics)
    mupdf.set_graphics_min_line_width( state.min_line_width)
    if state.no_icc:
        mupdf.disable_icc()
    else:
        mupdf.enable_icc()

    if state.layout_css:
        buf = mupdf.Buffer( state.layout_css)
        mupdf.set_user_css( buf.string_from_buffer())

    mupdf.set_use_document_css( state.layout_use_doc_css)

    # Determine output type
    if state.band_height < 0:
        sys.stderr.write( 'Bandheight must be > 0\n')
        sys.exit(1)

    state.output_format = OUT_PNG
    if state.format_:
        for i in range(len(suffix_table)):
            if state.format_ == suffix_table[i].suffix[1:]:
                state.output_format = suffix_table[i].format
                if state.spots == SPOTS_FULL and suffix_table[i].spots == 0:
                    sys.stderr.write( f'Output format {suffix_table[i].suffix[1:]} does not support spot rendering.\nDoing overprint simulation instead.\n')
                    state.spots = SPOTS_OVERPRINT_SIM
                break
        else:
            sys.stderr.write( f'Unknown output format {format}\n')
            sys.exit(1)
    elif state.output:
        suffix = state.output
        i = 0
        while 1:
            if i == len(suffix_table):
                break
            s = suffix.find( suffix_table[i].suffix)
            if s != -1:
                suffix = suffix_table[i].suffix[s+1:]
                state.output_format = suffix_table[i].format
                if state.spots == SPOTS_FULL and suffix_table[i].spots == 0:
                    sys.stderr.write( 'Output format {suffix_table[i].suffix[1:]} does not support spot rendering\nDoing overprint simulation instead.\n')
                    state.spots = SPOTS_OVERPRINT_SIM
                i = 0
            else:
                i += 1

    if state.band_height:
        if state.output_format not in ( OUT_PAM, OUT_PGM, OUT_PPM, OUT_PNM, OUT_PNG, OUT_PBM, OUT_PKM, OUT_PCL, OUT_PCLM, OUT_PS, OUT_PSD):
            sys.stderr.write( 'Banded operation only possible with PxM, PCL, PCLM, PS, PSD, and PNG outputs\n')
            sys.exit(1)
        if state.showmd5:
            sys.stderr.write( 'Banded operation not compatible with MD5\n')
            sys.exit(1)

    for i in range(len(format_cs_table)):
        if format_cs_table[i].format == state.output_format:
            if state.out_cs == CS_UNSET:
                state.out_cs = format_cs_table[i].default_cs
            for j in range( len(format_cs_table[i].permitted_cs)):
                if format_cs_table[i].permitted_cs[j] == state.out_cs:
                    break
            else:
                sys.stderr.write( 'Unsupported colorspace for this format\n')
                sys.exit(1)

    state.alpha = 1
    if state.out_cs in ( CS_MONO, CS_GRAY, CS_GRAY_ALPHA):
        state.colorspace = mupdf.Colorspace( mupdf.Colorspace.Fixed_GRAY)
        state.alpha = (state.out_cs == CS_GRAY_ALPHA)
    elif state.out_cs in ( CS_RGB, CS_RGB_ALPHA):
        state.colorspace = mupdf.Colorspace( mupdf.Colorspace.Fixed_RGB)
        state.alpha = (state.out_cs == CS_RGB_ALPHA)
    elif state.out_cs in ( CS_CMYK, CS_CMYK_ALPHA):
        state.colorspace = mupdf.Colorspace( mupdf.Colorspace.Fixed_CMYK)
        state.alpha = (state.out_cs == CS_CMYK_ALPHA)
    elif state.out_cs == CS_ICC:
        try:
            icc_buffer = mupdf.Buffer( state.icc_filename)
            state.colorspace = Colorspace( mupdf.FZ_COLORSPACE_NONE, 0, None, icc_buffer)
        except Exception as e:
            sys.stderr.write( 'Invalid ICC destination color space\n')
            sys.exit(1)
        if state.colorspace.m_internal is None:
            sys.stderr.write( 'Invalid ICC destination color space\n')
            sys.exit(1)
        state.alpha = 0
    else:
        sys.stderr.write( 'Unknown colorspace!\n')
        sys.exit(1)

    if state.out_cs != CS_ICC:
        state.colorspace = mupdf.Colorspace( state.colorspace)
    else:
        # Check to make sure this icc profile is ok with the output format */
        okay = 0
        for i in range( len(format_cs_table)):
            if format_cs_table[i].format == state.output_format:
                for j in range( len(format_cs_table[i].permitted_cs)):
                    x = format_cs_table[i].permitted_cs[j]
                    if x in ( CS_MONO, CS_GRAY, CS_GRAY_ALPHA):
                        if state.colorspace.colorspace_is_gray():
                            okay = 1
                        elif x in ( CS_RGB, CS_RGB_ALPHA):
                            if state.colorspace.colorspace_is_rgb():
                                okay = 1
                        elif x in ( CS_CMYK, CS_CMYK_ALPHA):
                            if state.colorspace.colorspace_is_cmyk():
                                okay = 1

        if not okay:
            sys.stderr.write( 'ICC profile uses a colorspace that cannot be used for this format\n')
            sys.exit(1)

    if state.output_format == OUT_SVG:
        # SVG files are always opened for each page. Do not open "output".
        pass
    elif state.output and (not state.output.startswith('-') or len(state.output) >= 2) and len(state.output) >= 1:
        if has_percent_d(state.output):
            state.output_file_per_page = 1
        else:
            state.out = mupdf.Output(state.output, 0)
    else:
        state.quiet = 1 # automatically be quiet if printing to stdout
        if 0:
            # Windows specific code to make stdout binary.
            if state.output_format not in( OUT_TEXT, OUT_STEXT, OUT_HTML, OUT_XHTML, OUT_TRACE, OUT_XMLTEXT):
                setmode(fileno(stdout), O_BINARY)
        state.out = mupdf.Output( mupdf.Output.Fixed_STDOUT)

    state.filename = argv[0]
    if not state.output_file_per_page:
        file_level_headers()

    timing.count = 0
    timing.total = 0
    timing.min = 1 << 30
    timing.max = 0
    timing.mininterp = 1 << 30
    timing.maxinterp = 0
    timing.minpage = 0
    timing.maxpage = 0
    timing.minfilename = ""
    timing.maxfilename = ""
    timing.layout = 0
    timing.minlayout = 1 << 30
    timing.maxlayout = 0
    timing.minlayoutfilename = ""
    timing.maxlayoutfilename = ""

    if state.showtime and bgprint.active:
        timing.total = gettime()

    fz_optind = 0
    try:
        while fz_optind < len( argv):

            try:
                accel = None

                state.filename = argv[fz_optind]
                fz_optind += 1

                state.files += 1

                if not state.useaccel:
                    accel = None
                # If there was an accelerator to load, what would it be called?
                else:
                    accelpath = get_accelerator_filename( state.filename)
                    # Check whether that file exists, and isn't older than
                    # the document.
                    atime = stat_mtime( accelpath)
                    dtime = stat_mtime( state.filename)
                    if atime == 0:
                        # No accelerator
                        pass
                    elif atime > dtime:
                        accel = accelpath
                    else:
                        # Accelerator data is out of date
                        os.unlink( accelpath)
                        accel = None # In case we have jumped up from below

                # Unfortunately if accel=None, SWIG doesn't seem to think of it
                # as a char*, so we end up in fz_open_document_with_stream().
                #
                # If we try to avoid this by setting accel='', SWIG correctly
                # calls Document(const char *filename, const char *accel) =>
                # fz_open_accelerated_document(), but the latter function tests
                # for NULL not "" so fails.
                #
                # So we choose the constructor explicitly rather than leaving
                # it up to SWIG.
                #
                if accel:
                    doc = mupdf.Document(state.filename, accel)
                else:
                    doc = mupdf.Document(state.filename)

                if doc.needs_password():
                    if not doc.authenticate_password( password):
                        raise Exception( f'cannot authenticate password: {state.filename}')

                # Once document is open check for output intent colorspace
                state.oi = doc.document_output_intent()
                if state.oi.m_internal:
                    # See if we had explicitly set a profile to render
                    if state.out_cs != CS_ICC:
                        # In this case, we want to render to the output intent
                        # color space if the number of channels is the same
                        if state.oi.colorspace_n() == state.colorspace.colorspace_n():
                            state.colorspace = state.oi

                layouttime = time.time()
                doc.layout_document( state.layout_w, state.layout_h, state.layout_em)
                doc.count_pages()
                layouttime = time.time() - layouttime

                timing.layout += layouttime
                if layouttime < timing.minlayout:
                    timing.minlayout = layouttime
                    timing.minlayoutfilename = state.filename
                if layouttime > timing.maxlayout:
                    timing.maxlayout = layouttime
                    timing.maxlayoutfilename = state.filename

                if state.layer_config:
                    apply_layer_config( doc, state.layer_config)

                if fz_optind == len(argv) or not mupdf.is_page_range( argv[fz_optind]):
                    drawrange( doc, "1-N")
                if fz_optind < len( argv) and mupdf.is_page_range( argv[fz_optind]):
                    drawrange( doc, argv[fz_optind])
                    fz_optind += 1

                bgprint_flush()

                if state.useaccel:
                    save_accelerator( doc, state.filename)
            except Exception as e:
                if not state.ignore_errors:
                    raise
                bgprint_flush()
                sys.stderr.write( f'ignoring error in {state.filename}\n')

    except Exception as e:
        bgprint_flush()
        sys.stderr.write( f'error: cannot draw \'{state.filename}\' because: {e}\n')
        state.errored = 1

    if not state.output_file_per_page:
        file_level_trailers()

    if state.out:
        state.out.close_output()
    state.out = None

    if state.showtime and timing.count > 0:
        if bgprint.active:
            timing.total = gettime() - timing.total

        if state.files == 1:
            sys.stderr.write( f'total {timing.total:.0f}ms ({timing.layout:.0f}ms layout) / {timing.count} pages for an average of {timing.total / timing.count:.0f}ms\n')
            if bgprint.active:
                sys.stderr.write( f'fastest page {timing.minpage}: {timing.mininterp:.0f}ms (interpretation) {timing.min - timing.mininterp:.0f}ms (rendering) {timing.min:.0f}ms(total)\n')
                sys.stderr.write( f'slowest page {timing.maxpage}: {timing.maxinterp:.0f}ms (interpretation) {timing.max - timing.maxinterp:.0f}ms (rendering) {timing.max:.0f}ms(total)\n')
            else:
                sys.stderr.write( f'fastest page {timing.minpage}: {timing.min:.0f}ms\n')
                sys.stderr.write( f'slowest page {timing.maxpage}: {timing.max:.0f}ms\n')
        else:
            sys.stderr.write( f'total {timing.total:.0f}ms ({timing.layout:.0f}ms layout) / {timing.count} pages for an average of {timing.total / timing.count:.0f}ms in {state.files} files\n')
            sys.stderr.write( f'fastest layout: {timing.minlayout:.0f}ms ({timing.minlayoutfilename})\n')
            sys.stderr.write( f'slowest layout: {timing.maxlayout:.0f}ms ({timing.maxlayoutfilename})\n')
            sys.stderr.write( f'fastest page {timing.minpage}: {timing.min:.0f}ms ({timing.minfilename})\n')
            sys.stderr.write( f'slowest page {timing.maxpage}: {timing.max:.0f}ms ({timing.maxfilename})\n')

    if state.showmemory:
        sys.stderr.write( f'Memory use total={info.total} peak={info.peak} current={info.current}\n')

    return state.errored != 0
