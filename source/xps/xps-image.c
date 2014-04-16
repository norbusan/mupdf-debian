#include "mupdf/xps.h"

static fz_image *
xps_load_image(fz_context *ctx, xps_part *part)
{
	/* Ownership of data always passes in here */
	unsigned char *data = part->data;
	part->data = NULL;
	return fz_new_image_from_data(ctx, data, part->size);
}

/* FIXME: area unused! */
static void
xps_paint_image_brush(xps_document *doc, const fz_matrix *ctm, const fz_rect *area, char *base_uri, xps_resource *dict,
	fz_xml *root, void *vimage)
{
	fz_image *image = vimage;
	float xs, ys;
	fz_matrix local_ctm = *ctm;

	if (image->xres == 0 || image->yres == 0)
		return;
	xs = image->w * 96 / image->xres;
	ys = image->h * 96 / image->yres;
	fz_pre_scale(&local_ctm, xs, ys);
	fz_fill_image(doc->dev, image, &local_ctm, doc->opacity[doc->opacity_top]);
}

static void
xps_find_image_brush_source_part(xps_document *doc, char *base_uri, fz_xml *root, xps_part **image_part, xps_part **profile_part)
{
	char *image_source_att;
	char buf[1024];
	char partname[1024];
	char *image_name;
	char *profile_name;
	char *p;

	image_source_att = fz_xml_att(root, "ImageSource");
	if (!image_source_att)
		fz_throw(doc->ctx, FZ_ERROR_GENERIC, "cannot find image source attribute");

	/* "{ColorConvertedBitmap /Resources/Image.tiff /Resources/Profile.icc}" */
	if (strstr(image_source_att, "{ColorConvertedBitmap") == image_source_att)
	{
		image_name = NULL;
		profile_name = NULL;

		fz_strlcpy(buf, image_source_att, sizeof buf);
		p = strchr(buf, ' ');
		if (p)
		{
			image_name = p + 1;
			p = strchr(p + 1, ' ');
			if (p)
			{
				*p = 0;
				profile_name = p + 1;
				p = strchr(p + 1, '}');
				if (p)
					*p = 0;
			}
		}
	}
	else
	{
		image_name = image_source_att;
		profile_name = NULL;
	}

	if (!image_name)
		fz_throw(doc->ctx, FZ_ERROR_GENERIC, "cannot find image source");

	if (image_part)
	{
		xps_resolve_url(partname, base_uri, image_name, sizeof partname);
		*image_part = xps_read_part(doc, partname);
	}

	if (profile_part)
	{
		if (profile_name)
		{
			xps_resolve_url(partname, base_uri, profile_name, sizeof partname);
			*profile_part = xps_read_part(doc, partname);
		}
		else
			*profile_part = NULL;
	}
}

void
xps_parse_image_brush(xps_document *doc, const fz_matrix *ctm, const fz_rect *area,
	char *base_uri, xps_resource *dict, fz_xml *root)
{
	xps_part *part;
	fz_image *image;

	fz_try(doc->ctx)
	{
		xps_find_image_brush_source_part(doc, base_uri, root, &part, NULL);
	}
	fz_catch(doc->ctx)
	{
		fz_rethrow_if(doc->ctx, FZ_ERROR_TRYLATER);
		fz_warn(doc->ctx, "cannot find image source");
		return;
	}

	fz_try(doc->ctx)
	{
		image = xps_load_image(doc->ctx, part);
		image->invert_cmyk_jpeg = 1;
	}
	fz_always(doc->ctx)
	{
		xps_free_part(doc, part);
	}
	fz_catch(doc->ctx)
	{
		fz_rethrow_if(doc->ctx, FZ_ERROR_TRYLATER);
		fz_warn(doc->ctx, "cannot decode image resource");
		return;
	}

	xps_parse_tiling_brush(doc, ctm, area, base_uri, dict, root, xps_paint_image_brush, image);

	fz_drop_image(doc->ctx, image);
}
