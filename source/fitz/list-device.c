#include "mupdf/fitz.h"

typedef struct fz_display_node_s fz_display_node;
typedef struct fz_list_device_s fz_list_device;

#define STACK_SIZE 96

typedef enum fz_display_command_e
{
	FZ_CMD_BEGIN_PAGE,
	FZ_CMD_END_PAGE,
	FZ_CMD_FILL_PATH,
	FZ_CMD_STROKE_PATH,
	FZ_CMD_CLIP_PATH,
	FZ_CMD_CLIP_STROKE_PATH,
	FZ_CMD_FILL_TEXT,
	FZ_CMD_STROKE_TEXT,
	FZ_CMD_CLIP_TEXT,
	FZ_CMD_CLIP_STROKE_TEXT,
	FZ_CMD_IGNORE_TEXT,
	FZ_CMD_FILL_SHADE,
	FZ_CMD_FILL_IMAGE,
	FZ_CMD_FILL_IMAGE_MASK,
	FZ_CMD_CLIP_IMAGE_MASK,
	FZ_CMD_POP_CLIP,
	FZ_CMD_BEGIN_MASK,
	FZ_CMD_END_MASK,
	FZ_CMD_BEGIN_GROUP,
	FZ_CMD_END_GROUP,
	FZ_CMD_BEGIN_TILE,
	FZ_CMD_END_TILE
} fz_display_command;

/* The display list is a list of nodes.
 * Each node is a structure consisting of a bitfield (that packs into a
 * 32 bit word).
 * The different fields in the bitfield identify what information is
 * present in the node.
 *
 *	cmd:	What type of node this is.
 *
 *	size:	The number of sizeof(fz_display_node) bytes that this nodes
 *		data occupies. (i.e. &node[node->size] = the next node in the
 *		chain; 0 for end of list).
 *
 *	rect:	0 for unchanged, 1 for present.
 *
 *	path:	0 for unchanged, 1 for present.
 *
 *	cs:	0 for unchanged
 *		1 for devicegray (color defaults to 0)
 *		2 for devicegray (color defaults to 1)
 *		3 for devicergb (color defaults to 0,0,0)
 *		4 for devicergb (color defaults to 1,1,1)
 *		5 for devicecmyk (color defaults to 0,0,0,0)
 *		6 for devicecmyk (color defaults to 0,0,0,1)
 *		7 for present (color defaults to 0)
 *
 *	color:	0 for unchanged color, 1 for present.
 *
 *	alpha:	0 for unchanged, 1 for solid, 2 for transparent, 3
 *		for alpha value present.
 *
 *	ctm:	0 for unchanged,
 *		1 for change ad
 *		2 for change bc
 *		4 for change ef.
 *
 *	stroke:	0 for unchanged, 1 for present.
 *
 *	flags:	Flags (node specific meanings)
 *
 * Nodes are packed in the order:
 * header, rect, colorspace, color, alpha, ctm, stroke_state, path, private data.
 */
struct fz_display_node_s
{
	unsigned int cmd    : 5;
	unsigned int size   : 9;
	unsigned int rect   : 1;
	unsigned int path   : 1;
	unsigned int cs     : 3;
	unsigned int color  : 1;
	unsigned int alpha  : 2;
	unsigned int ctm    : 3;
	unsigned int stroke : 1;
	unsigned int flags  : 6;
};

enum {
	CS_UNCHANGED = 0,
	CS_GRAY_0    = 1,
	CS_GRAY_1    = 2,
	CS_RGB_0     = 3,
	CS_RGB_1     = 4,
	CS_CMYK_0    = 5,
	CS_CMYK_1    = 6,
	CS_OTHER_0   = 7,

	ALPHA_UNCHANGED = 0,
	ALPHA_1         = 1,
	ALPHA_0         = 2,
	ALPHA_PRESENT   = 3,

	CTM_UNCHANGED = 0,
	CTM_CHANGE_AD = 1,
	CTM_CHANGE_BC = 2,
	CTM_CHANGE_EF = 4,

	MAX_NODE_SIZE = (1<<9)-sizeof(fz_display_node)
};

struct fz_display_list_s
{
	fz_storable storable;
	fz_display_node *list;
	int max;
	int len;
};

struct fz_list_device_s
{
	fz_device super;

	fz_display_list *list;

	fz_path *path;
	float alpha;
	fz_matrix ctm;
	fz_stroke_state *stroke;
	fz_colorspace *colorspace;
	float color[FZ_MAX_COLORS];
	fz_rect rect;

	int top;
	struct {
		fz_rect *update;
		fz_rect rect;
	} stack[STACK_SIZE];
	int tiled;
};

enum { ISOLATED = 1, KNOCKOUT = 2 };

#define SIZE_IN_NODES(t) \
	((t + sizeof(fz_display_node) - 1) / sizeof(fz_display_node))

static void
fz_append_display_node(
	fz_context *ctx,
	fz_device *dev,
	fz_display_command cmd,
	int flags,
	const fz_rect *rect,
	fz_path *path,
	float *color,
	fz_colorspace *colorspace,
	float *alpha,
	const fz_matrix *ctm,
	fz_stroke_state *stroke,
	void *private_data,
	int private_data_len)
{
	fz_display_node node = { 0 };
	fz_display_node *node_ptr;
	fz_list_device *writer = (fz_list_device *)dev;
	fz_display_list *list = writer->list;
	int size;
	int rect_off = 0;
	int path_off = 0;
	int color_off = 0;
	int colorspace_off = 0;
	int alpha_off = 0;
	int ctm_off = 0;
	int stroke_off = 0;
	int rect_for_updates = 0;
	int private_off = 0;
	fz_path *my_path = NULL;
	fz_stroke_state *my_stroke = NULL;
	fz_rect local_rect;
	int path_size = 0;

	switch (cmd)
	{
	case FZ_CMD_CLIP_PATH:
	case FZ_CMD_CLIP_STROKE_PATH:
	case FZ_CMD_CLIP_IMAGE_MASK:
		if (writer->top < STACK_SIZE)
		{
			rect_for_updates = 1;
			writer->stack[writer->top].rect = fz_empty_rect;
		}
		writer->top++;
		break;
	case FZ_CMD_CLIP_TEXT:
		/* don't reset the clip rect for accumulated text */
		if (flags == 2)
			break;
		/* fallthrough */
	case FZ_CMD_END_MASK:
	case FZ_CMD_CLIP_STROKE_TEXT:
		if (writer->top < STACK_SIZE)
		{
			writer->stack[writer->top].update = NULL;
			writer->stack[writer->top].rect = fz_empty_rect;
		}
		writer->top++;
		break;
	case FZ_CMD_BEGIN_TILE:
		writer->tiled++;
		if (writer->top > 0 && writer->top <= STACK_SIZE)
		{
			writer->stack[writer->top-1].rect = fz_infinite_rect;
		}
		break;
	case FZ_CMD_END_TILE:
		writer->tiled--;
		break;
	case FZ_CMD_END_GROUP:
		break;
	case FZ_CMD_POP_CLIP:
		if (writer->top > STACK_SIZE)
		{
			writer->top--;
			rect = &fz_infinite_rect;
		}
		else if (writer->top > 0)
		{
			fz_rect *update;
			writer->top--;
			update = writer->stack[writer->top].update;
			if (writer->tiled == 0)
			{
				if (update)
				{
					fz_intersect_rect(update, &writer->stack[writer->top].rect);
					local_rect = *update;
					rect = &local_rect;
				}
				else
					rect = &writer->stack[writer->top].rect;
			}
			else
				rect = &fz_infinite_rect;
		}
		/* fallthrough */
	default:
		if (writer->top > 0 && writer->tiled == 0 && writer->top <= STACK_SIZE && rect)
			fz_union_rect(&writer->stack[writer->top-1].rect, rect);
		break;
	}

	size = 1; /* 1 for the fz_display_node */
	node.cmd = cmd;

	/* Figure out what we need to write, and the offsets at which we will
	 * write it. */
	if (rect_for_updates || (rect != NULL && (writer->rect.x0 != rect->x0 || writer->rect.y0 != rect->y0 || writer->rect.x1 != rect->x1 || writer->rect.y1 != rect->y1)))
	{
		node.rect = 1;
		rect_off = size;
		size += SIZE_IN_NODES(sizeof(fz_rect));
	}
	if (color && !colorspace)
	{
		/* SoftMasks can omit a colorspace, but we know what they mean */
		colorspace = fz_device_gray(ctx);
	}
	if (colorspace)
	{
		if (colorspace != writer->colorspace)
		{
			assert(color);
			if (colorspace == fz_device_gray(ctx))
			{
				if (color[0] == 0.0f)
					node.cs = CS_GRAY_0, color = NULL;
				else
				{
					node.cs = CS_GRAY_1;
					if (color[0] == 1.0f)
						color = NULL;
				}
			}
			else if (colorspace == fz_device_rgb(ctx))
			{
				if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f)
					node.cs = CS_RGB_0, color = NULL;
				else
				{
					node.cs = CS_RGB_1;
					if (color[0] == 1.0f && color[1] == 1.0f && color[2] == 1.0f)
						color = NULL;
				}
			}
			else if (colorspace == fz_device_cmyk(ctx))
			{
				node.cs = CS_CMYK_0;
				if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f)
				{
					if (color[3] == 0.0f)
						color = NULL;
					else
					{
						node.cs = CS_CMYK_1;
						if (color[3] == 1.0f)
							color = NULL;
					}
				}
			}
			else
			{
				int i;
				int n = colorspace->n;

				colorspace_off = size;
				size += SIZE_IN_NODES(sizeof(fz_colorspace *));
				node.cs = CS_OTHER_0;
				for (i = 0; i < n; i++)
					if (color[i] != 0.0f)
						break;
				if (i == n)
					color = NULL;
				memset(writer->color, 0, sizeof(float)*n);
			}
		}
		else
		{
			/* Colorspace is unchanged, but color may have changed
			 * to something best coded as a colorspace change */
			if (colorspace == fz_device_gray(ctx))
			{
				if (writer->color[0] != color[0])
				{
					if (color[0] == 0.0f)
					{
						node.cs = CS_GRAY_0;
						color = NULL;
					}
					else if (color[0] == 1.0f)
					{
						node.cs = CS_GRAY_1;
						color = NULL;
					}
				}
			}
			else if (colorspace == fz_device_rgb(ctx))
			{
				if (writer->color[0] != color[0] || writer->color[1] != color[1] || writer->color[2] != color[2])
				{
					if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f)
					{
						node.cs = CS_RGB_0;
						color = NULL;
					}
					else if (color[0] == 1.0f && color[1] == 1.0f && color[2] == 1.0f)
					{
						node.cs = CS_RGB_1;
						color = NULL;
					}
				}
			}
			else if (colorspace == fz_device_cmyk(ctx))
			{
				if (writer->color[0] != color[0] || writer->color[1] != color[1] || writer->color[2] != color[2] || writer->color[3] != color[3])
				{
					if (color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f)
					{
						if (color[3] == 0.0f)
						{
							node.cs = CS_CMYK_0;
							color = NULL;
						}
						else if (color[3] == 1.0f)
						{
							node.cs = CS_CMYK_1;
							color = NULL;
						}
					}
				}
			}
			else
			{
				int i;
				int n = colorspace->n;
				for (i=0; i < n; i++)
					if (color[i] != 0.0f)
						break;
				if (i == n)
				{
					node.cs = CS_OTHER_0;
					colorspace_off = size;
					size += SIZE_IN_NODES(sizeof(fz_colorspace *));
					color = NULL;
				}
			}
		}
	}
	if (color)
	{

		int i, n;
		const float *wc = &writer->color[0];

		assert(colorspace != NULL);
		n = colorspace->n;
		i = 0;
		/* Only check colors if the colorspace is unchanged. If the
		 * colorspace *has* changed and the colors are implicit then
		 * this will have been caught above. */
		if (colorspace == writer->colorspace)
			for (; i < n; i++)
				if (color[i] != wc[i])
					break;

		if (i != n)
		{
			node.color = 1;
			color_off = size;
			size += n * SIZE_IN_NODES(sizeof(float));
		}
	}
	if (alpha && (*alpha != writer->alpha))
	{
		if (*alpha >= 1.0)
			node.alpha = ALPHA_1;
		else if (*alpha <= 0.0)
			node.alpha = ALPHA_0;
		else
		{
			alpha_off = size;
			size += SIZE_IN_NODES(sizeof(float));
			node.alpha = ALPHA_PRESENT;
		}
	}
	if (ctm && (ctm->a != writer->ctm.a || ctm->b != writer->ctm.b || ctm->c != writer->ctm.c || ctm->d != writer->ctm.d || ctm->e != writer->ctm.e || ctm->f != writer->ctm.f))
	{
		int flags;

		ctm_off = size;
		flags = CTM_UNCHANGED;
		if (ctm->a != writer->ctm.a || ctm->d != writer->ctm.d)
			flags = CTM_CHANGE_AD, size += SIZE_IN_NODES(2*sizeof(float));
		if (ctm->b != writer->ctm.b || ctm->c != writer->ctm.c)
			flags |= CTM_CHANGE_BC, size += SIZE_IN_NODES(2*sizeof(float));
		if (ctm->e != writer->ctm.e || ctm->f != writer->ctm.f)
			flags |= CTM_CHANGE_EF, size += SIZE_IN_NODES(2*sizeof(float));
		node.ctm = flags;
	}
	if (stroke && (writer->stroke == NULL || stroke != writer->stroke))
	{
		stroke_off = size;
		size += SIZE_IN_NODES(sizeof(fz_stroke_state *));
		node.stroke = 1;
	}
	if (path && (writer->path == NULL || path != writer->path))
	{
		int max = SIZE_IN_NODES(MAX_NODE_SIZE) - size - SIZE_IN_NODES(private_data_len);
		path_size = SIZE_IN_NODES(fz_pack_path(ctx, NULL, max, path));
		node.path = 1;
		path_off = size;

		size += path_size;
	}
	if (private_data != NULL)
	{
		private_off = size;
		size += SIZE_IN_NODES(private_data_len);
	}

	if (list->len + size > list->max)
	{
		int newsize = list->max * 2;
		fz_display_node *old = list->list;
		ptrdiff_t diff;
		int i, n;

		if (newsize < 256)
			newsize = 256;
		list->list = fz_resize_array(ctx, list->list, newsize, sizeof(fz_display_node));
		list->max = newsize;
		diff = (char *)(list->list) - (char *)old;
		n = (writer->top < STACK_SIZE ? writer->top : STACK_SIZE);
		for (i = 0; i < n; i++)
		{
			if (writer->stack[i].update != NULL)
				writer->stack[i].update = (fz_rect *)(((char *)writer->stack[i].update) + diff);
		}
		if (writer->path)
			writer->path = (fz_path *)(((char *)writer->path) + diff);
	}

	/* Write the node to the list */
	node.size = size;
	node.flags = flags;
	assert(size < (1<<9));
	node_ptr = &list->list[list->len];
	*node_ptr = node;

	/* Path is the most frequent one, so try to avoid the try/catch in
	 * this case */
	if (path_off)
	{
		my_path = (void *)(&node_ptr[path_off]);
		(void)fz_pack_path(ctx, (void *)my_path, path_size * sizeof(fz_display_node), path);
	}

	if (stroke_off)
	{
		fz_try(ctx)
		{
			my_stroke = fz_keep_stroke_state(ctx, stroke);
		}
		fz_catch(ctx)
		{
			fz_drop_path(ctx, my_path);
			fz_rethrow(ctx);
		}
	}

	if (rect_off)
	{
		fz_rect *out_rect = (fz_rect *)(void *)(&node_ptr[rect_off]);
		writer->rect = *rect;
		*out_rect = *rect;
		if (rect_for_updates)
			writer->stack[writer->top-1].update = out_rect;
	}
	if (path_off)
	{
		fz_drop_path(ctx, writer->path);
		writer->path = fz_keep_path(ctx, my_path); /* Can never fail */
	}
	if (node.cs)
	{
		fz_drop_colorspace(ctx, writer->colorspace);
		switch(node.cs)
		{
		case CS_GRAY_0:
			writer->colorspace = fz_device_gray(ctx);
			writer->color[0] = 0;
			break;
		case CS_GRAY_1:
			writer->colorspace = fz_device_gray(ctx);
			writer->color[0] = 1;
			break;
		case CS_RGB_0:
			writer->color[0] = 0;
			writer->color[1] = 0;
			writer->color[2] = 0;
			writer->colorspace = fz_device_rgb(ctx);
			break;
		case CS_RGB_1:
			writer->color[0] = 1;
			writer->color[1] = 1;
			writer->color[2] = 1;
			writer->colorspace = fz_device_rgb(ctx);
			break;
		case CS_CMYK_0:
			writer->color[0] = 0;
			writer->color[1] = 0;
			writer->color[2] = 0;
			writer->color[3] = 0;
			writer->colorspace = fz_device_cmyk(ctx);
			break;
		case CS_CMYK_1:
			writer->color[0] = 0;
			writer->color[1] = 0;
			writer->color[2] = 0;
			writer->color[3] = 1;
			writer->colorspace = fz_device_cmyk(ctx);
			break;
		default:
		{
			fz_colorspace **out_colorspace = (fz_colorspace **)(void *)(&node_ptr[colorspace_off]);
			int i, n = colorspace->n;
			*out_colorspace = fz_keep_colorspace(ctx, colorspace);

			writer->colorspace = fz_keep_colorspace(ctx, colorspace);
			for (i = 0; i < n; i++)
				writer->color[i] = 0;
			break;
		}
		}
	}
	if (color_off)
	{
		float *out_color = (float *)(void *)(&node_ptr[color_off]);
		memcpy(writer->color, color, colorspace->n * sizeof(float));
		memcpy(out_color, color, colorspace->n * sizeof(float));
	}
	if (node.alpha)
	{
		writer->alpha = *alpha;
		if (alpha_off)
		{
			float *out_alpha = (float *)(void *)(&node_ptr[alpha_off]);
			*out_alpha = *alpha;
		}
	}
	if (ctm_off)
	{
		float *out_ctm = (float *)(void *)(&node_ptr[ctm_off]);
		if (node.ctm & CTM_CHANGE_AD)
		{
			writer->ctm.a = *out_ctm++ = ctm->a;
			writer->ctm.d = *out_ctm++ = ctm->d;
		}
		if (node.ctm & CTM_CHANGE_BC)
		{
			writer->ctm.b = *out_ctm++ = ctm->b;
			writer->ctm.c = *out_ctm++ = ctm->c;
		}
		if (node.ctm & CTM_CHANGE_EF)
		{
			writer->ctm.e = *out_ctm++ = ctm->e;
			writer->ctm.f = *out_ctm = ctm->f;
		}
	}
	if (stroke_off)
	{
		fz_stroke_state **out_stroke = (fz_stroke_state **)(void *)(&node_ptr[stroke_off]);
		*out_stroke = my_stroke;
		fz_drop_stroke_state(ctx, writer->stroke);
		/* Can never fail as my_stroke was kept above */
		writer->stroke = fz_keep_stroke_state(ctx, my_stroke);
	}
	if (private_off)
	{
		char *out_private = (char *)(void *)(&node_ptr[private_off]);
		memcpy(out_private, private_data, private_data_len);
	}
	list->len += size;
}

static void
fz_list_begin_page(fz_context *ctx, fz_device *dev, const fz_rect *mediabox, const fz_matrix *ctm)
{
	fz_rect rect = *mediabox;

	fz_transform_rect(&rect, ctm);
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_BEGIN_PAGE,
		0, /* flags */
		&rect,
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		ctm,
		NULL, /* stroke_state */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_end_page(fz_context *ctx, fz_device *dev)
{
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_END_PAGE,
		0, /* flags */
		NULL, /* rect */
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke_state */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_fill_path(fz_context *ctx, fz_device *dev, fz_path *path, int even_odd, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_rect rect;

	fz_bound_path(ctx, path, NULL, ctm, &rect);
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_FILL_PATH,
		even_odd, /* flags */
		&rect,
		path, /* path */
		color,
		colorspace,
		&alpha, /* alpha */
		ctm,
		NULL, /* stroke_state */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_stroke_path(fz_context *ctx, fz_device *dev, fz_path *path, fz_stroke_state *stroke,
	const fz_matrix *ctm, fz_colorspace *colorspace, float *color, float alpha)
{
	fz_rect rect;

	fz_bound_path(ctx, path, stroke, ctm, &rect);
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_STROKE_PATH,
		0, /* flags */
		&rect,
		path, /* path */
		color,
		colorspace,
		&alpha, /* alpha */
		ctm, /* ctm */
		stroke,
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_clip_path(fz_context *ctx, fz_device *dev, fz_path *path, const fz_rect *rect, int even_odd, const fz_matrix *ctm)
{
	fz_rect rect2;

	fz_bound_path(ctx, path, NULL, ctm, &rect2);
	if (rect)
		fz_intersect_rect(&rect2, rect);
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_CLIP_PATH,
		even_odd, /* flags */
		&rect2,
		path, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		ctm, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_clip_stroke_path(fz_context *ctx, fz_device *dev, fz_path *path, const fz_rect *rect, fz_stroke_state *stroke, const fz_matrix *ctm)
{
	fz_rect rect2;

	fz_bound_path(ctx, path, stroke, ctm, &rect2);
	if (rect)
		fz_intersect_rect(&rect2, rect);
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_CLIP_STROKE_PATH,
		0, /* flags */
		&rect2,
		path, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		ctm, /* ctm */
		stroke, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_fill_text(fz_context *ctx, fz_device *dev, fz_text *text, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_rect rect;
	fz_text *cloned_text = fz_keep_text(ctx, text);

	fz_try(ctx)
	{
		fz_bound_text(ctx, text, NULL, ctm, &rect);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_FILL_TEXT,
			0, /* flags */
			&rect,
			NULL, /* path */
			color, /* color */
			colorspace, /* colorspace */
			&alpha, /* alpha */
			ctm, /* ctm */
			NULL, /* stroke */
			&cloned_text, /* private_data */
			sizeof(cloned_text)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, cloned_text);
		fz_rethrow(ctx);
	}
}

static void
fz_list_stroke_text(fz_context *ctx, fz_device *dev, fz_text *text, fz_stroke_state *stroke, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_rect rect;
	fz_text *cloned_text = fz_keep_text(ctx, text);

	fz_try(ctx)
	{
		fz_bound_text(ctx, text, stroke, ctm, &rect);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_STROKE_TEXT,
			0, /* flags */
			&rect,
			NULL, /* path */
			color, /* color */
			colorspace, /* colorspace */
			&alpha, /* alpha */
			ctm, /* ctm */
			stroke,
			&cloned_text, /* private_data */
			sizeof(cloned_text)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, cloned_text);
		fz_rethrow(ctx);
	}
}

static void
fz_list_clip_text(fz_context *ctx, fz_device *dev, fz_text *text, const fz_matrix *ctm, int accumulate)
{
	fz_rect rect;
	fz_text *cloned_text = fz_keep_text(ctx, text);

	fz_try(ctx)
	{
		if (accumulate)
			rect = fz_infinite_rect;
		else
			fz_bound_text(ctx, text, NULL, ctm, &rect);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_CLIP_TEXT,
			accumulate, /* flags */
			&rect,
			NULL, /* path */
			NULL, /* color */
			NULL, /* colorspace */
			NULL, /* alpha */
			ctm, /* ctm */
			NULL, /* stroke */
			&cloned_text, /* private_data */
			sizeof(cloned_text)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, cloned_text);
		fz_rethrow(ctx);
	}
}

static void
fz_list_clip_stroke_text(fz_context *ctx, fz_device *dev, fz_text *text, fz_stroke_state *stroke, const fz_matrix *ctm)
{
	fz_rect rect;
	fz_text *cloned_text = fz_keep_text(ctx, text);

	fz_try(ctx)
	{
		fz_bound_text(ctx, text, stroke, ctm, &rect);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_CLIP_STROKE_TEXT,
			0, /* flags */
			&rect,
			NULL, /* path */
			NULL, /* color */
			NULL, /* colorspace */
			NULL, /* alpha */
			ctm, /* ctm */
			stroke, /* stroke */
			&cloned_text, /* private_data */
			sizeof(cloned_text)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, cloned_text);
		fz_rethrow(ctx);
	}
}

static void
fz_list_ignore_text(fz_context *ctx, fz_device *dev, fz_text *text, const fz_matrix *ctm)
{
	fz_rect rect;
	fz_text *cloned_text = fz_keep_text(ctx, text);

	fz_try(ctx)
	{
		fz_bound_text(ctx, text, NULL, ctm, &rect);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_IGNORE_TEXT,
			0, /* flags */
			&rect,
			NULL, /* path */
			NULL, /* color */
			NULL, /* colorspace */
			NULL, /* alpha */
			ctm, /* ctm */
			NULL, /* stroke */
			&cloned_text, /* private_data */
			sizeof(cloned_text)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_text(ctx, cloned_text);
		fz_rethrow(ctx);
	}
}

static void
fz_list_pop_clip(fz_context *ctx, fz_device *dev)
{
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_POP_CLIP,
		0, /* flags */
		NULL, /* rect */
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_fill_shade(fz_context *ctx, fz_device *dev, fz_shade *shade, const fz_matrix *ctm, float alpha)
{
	fz_shade *shade2 = fz_keep_shade(ctx, shade);
	fz_rect rect;

	fz_try(ctx)
	{
		fz_bound_shade(ctx, shade, ctm, &rect);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_FILL_SHADE,
			0, /* flags */
			&rect,
			NULL, /* path */
			NULL, /* color */
			NULL, /* colorspace */
			&alpha, /* alpha */
			ctm,
			NULL, /* stroke */
			&shade2, /* private_data */
			sizeof(shade2)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_shade(ctx, shade2);
		fz_rethrow(ctx);
	}
}

static void
fz_list_fill_image(fz_context *ctx, fz_device *dev, fz_image *image, const fz_matrix *ctm, float alpha)
{
	fz_image *image2 = fz_keep_image(ctx, image);
	fz_rect rect = fz_unit_rect;

	fz_try(ctx)
	{
		fz_transform_rect(&rect, ctm);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_FILL_IMAGE,
			0, /* flags */
			&rect,
			NULL, /* path */
			NULL, /* color */
			NULL, /* colorspace */
			&alpha, /* alpha */
			ctm,
			NULL, /* stroke */
			&image2, /* private_data */
			sizeof(image2)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_image(ctx, image2);
		fz_rethrow(ctx);
	}
}

static void
fz_list_fill_image_mask(fz_context *ctx, fz_device *dev, fz_image *image, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_image *image2 = fz_keep_image(ctx, image);
	fz_rect rect = fz_unit_rect;

	fz_try(ctx)
	{
		fz_transform_rect(&rect, ctm);
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_FILL_IMAGE_MASK,
			0, /* flags */
			&rect,
			NULL, /* path */
			color,
			colorspace,
			&alpha, /* alpha */
			ctm,
			NULL, /* stroke */
			&image2, /* private_data */
			sizeof(image2)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_image(ctx, image2);
		fz_rethrow(ctx);
	}
}

static void
fz_list_clip_image_mask(fz_context *ctx, fz_device *dev, fz_image *image, const fz_rect *rect, const fz_matrix *ctm)
{
	fz_image *image2 = fz_keep_image(ctx, image);
	fz_rect rect2 = fz_unit_rect;

	fz_transform_rect(&rect2, ctm);
	if (rect)
		fz_intersect_rect(&rect2, rect);
	fz_try(ctx)
	{
		fz_append_display_node(
			ctx,
			dev,
			FZ_CMD_CLIP_IMAGE_MASK,
			0, /* flags */
			&rect2,
			NULL, /* path */
			NULL, /* color */
			NULL, /* colorspace */
			NULL, /* alpha */
			ctm,
			NULL, /* stroke */
			&image2, /* private_data */
			sizeof(image2)); /* private_data_len */
	}
	fz_catch(ctx)
	{
		fz_drop_image(ctx, image2);
		fz_rethrow(ctx);
	}
}

static void
fz_list_begin_mask(fz_context *ctx, fz_device *dev, const fz_rect *rect, int luminosity, fz_colorspace *colorspace, float *color)
{
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_BEGIN_MASK,
		luminosity, /* flags */
		rect,
		NULL, /* path */
		color,
		colorspace,
		NULL, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_end_mask(fz_context *ctx, fz_device *dev)
{
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_END_MASK,
		0, /* flags */
		NULL, /* rect */
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_begin_group(fz_context *ctx, fz_device *dev, const fz_rect *rect, int isolated, int knockout, int blendmode, float alpha)
{
	int flags;

	flags = (blendmode<<2);
	if (isolated)
		flags |= ISOLATED;
	if (knockout)
		flags |= KNOCKOUT;
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_BEGIN_GROUP,
		flags,
		rect,
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		&alpha, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
fz_list_end_group(fz_context *ctx, fz_device *dev)
{
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_END_GROUP,
		0, /* flags */
		NULL, /* rect */
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

typedef struct fz_list_tile_data_s fz_list_tile_data;

struct fz_list_tile_data_s
{
	float xstep;
	float ystep;
	fz_rect view;
};

static int
fz_list_begin_tile(fz_context *ctx, fz_device *dev, const fz_rect *area, const fz_rect *view, float xstep, float ystep, const fz_matrix *ctm, int id)
{
	fz_list_tile_data tile;

	tile.xstep = xstep;
	tile.ystep = ystep;
	tile.view = *view;
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_BEGIN_TILE,
		0, /* flags */
		area,
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		ctm,
		NULL, /* stroke */
		&tile, /* private_data */
		sizeof(tile)); /* private_data_len */

	return 0;
}

static void
fz_list_end_tile(fz_context *ctx, fz_device *dev)
{
	fz_append_display_node(
		ctx,
		dev,
		FZ_CMD_END_TILE,
		0, /* flags */
		NULL,
		NULL, /* path */
		NULL, /* color */
		NULL, /* colorspace */
		NULL, /* alpha */
		NULL, /* ctm */
		NULL, /* stroke */
		NULL, /* private_data */
		0); /* private_data_len */
}

static void
drop_writer(fz_context *ctx, fz_device *dev)
{
	fz_list_device *writer = (fz_list_device *)dev;

	fz_drop_colorspace(ctx, writer->colorspace);
	fz_drop_stroke_state(ctx, writer->stroke);
	fz_drop_path(ctx, writer->path);
}

fz_device *
fz_new_list_device(fz_context *ctx, fz_display_list *list)
{
	fz_list_device *dev;

	dev = fz_new_device(ctx, sizeof(fz_list_device));

	dev->super.begin_page = fz_list_begin_page;
	dev->super.end_page = fz_list_end_page;

	dev->super.fill_path = fz_list_fill_path;
	dev->super.stroke_path = fz_list_stroke_path;
	dev->super.clip_path = fz_list_clip_path;
	dev->super.clip_stroke_path = fz_list_clip_stroke_path;

	dev->super.fill_text = fz_list_fill_text;
	dev->super.stroke_text = fz_list_stroke_text;
	dev->super.clip_text = fz_list_clip_text;
	dev->super.clip_stroke_text = fz_list_clip_stroke_text;
	dev->super.ignore_text = fz_list_ignore_text;

	dev->super.fill_shade = fz_list_fill_shade;
	dev->super.fill_image = fz_list_fill_image;
	dev->super.fill_image_mask = fz_list_fill_image_mask;
	dev->super.clip_image_mask = fz_list_clip_image_mask;

	dev->super.pop_clip = fz_list_pop_clip;

	dev->super.begin_mask = fz_list_begin_mask;
	dev->super.end_mask = fz_list_end_mask;
	dev->super.begin_group = fz_list_begin_group;
	dev->super.end_group = fz_list_end_group;

	dev->super.begin_tile = fz_list_begin_tile;
	dev->super.end_tile = fz_list_end_tile;

	dev->super.drop_imp = drop_writer;

	dev->list = list;
	dev->path = NULL;
	dev->alpha = 1.0f;
	dev->ctm = fz_identity;
	dev->stroke = NULL;
	dev->colorspace = fz_device_gray(ctx);
	memset(dev->color, 0, sizeof(float)*FZ_MAX_COLORS);
	dev->top = 0;
	dev->tiled = 0;

	return &dev->super;
}

static void
fz_drop_display_list_imp(fz_context *ctx, fz_storable *list_)
{
	fz_display_list *list = (fz_display_list *)list_;
	fz_display_node *node = list->list;
	fz_display_node *node_end = list->list + list->len;
	int cs_n = 1;

	if (list == NULL)
		return;
	while (node != node_end)
	{
		fz_display_node n = *node;
		fz_display_node *next = node + n.size;

		node++;
		if (n.rect)
		{
			node += SIZE_IN_NODES(sizeof(fz_rect));
		}
		switch (n.cs)
		{
		default:
		case CS_UNCHANGED:
			break;
		case CS_GRAY_0:
		case CS_GRAY_1:
			cs_n = 1;
			break;
		case CS_RGB_0:
		case CS_RGB_1:
			cs_n = 3;
			break;
		case CS_CMYK_0:
		case CS_CMYK_1:
			cs_n = 4;
			break;
		case CS_OTHER_0:
			cs_n = (*(fz_colorspace **)node)->n;
			fz_drop_colorspace(ctx, *(fz_colorspace **)node);
			node += SIZE_IN_NODES(sizeof(fz_colorspace *));
			break;
		}
		if (n.color)
		{
			node += SIZE_IN_NODES(cs_n * sizeof(float));
		}
		if (n.alpha == ALPHA_PRESENT)
		{
			node += SIZE_IN_NODES(sizeof(float));
		}
		if (n.ctm & CTM_CHANGE_AD)
			node += SIZE_IN_NODES(2*sizeof(float));
		if (n.ctm & CTM_CHANGE_BC)
			node += SIZE_IN_NODES(2*sizeof(float));
		if (n.ctm & CTM_CHANGE_EF)
			node += SIZE_IN_NODES(2*sizeof(float));
		if (n.stroke)
		{
			fz_drop_stroke_state(ctx, *(fz_stroke_state **)node);
			node += SIZE_IN_NODES(sizeof(fz_stroke_state *));
		}
		if (n.path)
		{
			int path_size = fz_packed_path_size((fz_path *)node);
			fz_drop_path(ctx, (fz_path *)node);
			node += SIZE_IN_NODES(path_size);
		}
		switch(n.cmd)
		{
		case FZ_CMD_FILL_TEXT:
		case FZ_CMD_STROKE_TEXT:
		case FZ_CMD_CLIP_TEXT:
		case FZ_CMD_CLIP_STROKE_TEXT:
		case FZ_CMD_IGNORE_TEXT:
			fz_drop_text(ctx, *(fz_text **)node);
			break;
		case FZ_CMD_FILL_SHADE:
			fz_drop_shade(ctx, *(fz_shade **)node);
			break;
		case FZ_CMD_FILL_IMAGE:
		case FZ_CMD_FILL_IMAGE_MASK:
		case FZ_CMD_CLIP_IMAGE_MASK:
			fz_drop_image(ctx, *(fz_image **)node);
			break;
		}

		node = next;
	}
	fz_free(ctx, list->list);
	fz_free(ctx, list);
}

fz_display_list *
fz_new_display_list(fz_context *ctx)
{
	fz_display_list *list = fz_malloc_struct(ctx, fz_display_list);
	FZ_INIT_STORABLE(list, 1, fz_drop_display_list_imp);
	list->list = NULL;
	list->max = 0;
	list->len = 0;
	return list;
}

fz_display_list *
fz_keep_display_list(fz_context *ctx, fz_display_list *list)
{
	return (fz_display_list *)fz_keep_storable(ctx, &list->storable);
}

void
fz_drop_display_list(fz_context *ctx, fz_display_list *list)
{
	fz_drop_storable(ctx, &list->storable);
}

void
fz_run_display_list(fz_context *ctx, fz_display_list *list, fz_device *dev, const fz_matrix *top_ctm, const fz_rect *scissor, fz_cookie *cookie)
{
	fz_display_node *node;
	fz_display_node *node_end;
	fz_display_node *next_node;
	int clipped = 0;
	int tiled = 0;
	int progress = 0;

	/* Current graphics state as unpacked from list */
	fz_path *path = NULL;
	float alpha = 1.0f;
	fz_matrix ctm = fz_identity;
	fz_stroke_state *stroke = NULL;
	float color[FZ_MAX_COLORS] = { 0 };
	fz_colorspace *colorspace = fz_device_gray(ctx);
	fz_rect rect = { 0 };

	/* Transformed versions of graphic state entries */
	fz_rect trans_rect;
	fz_matrix trans_ctm;
	int tile_skip_depth = 0;

	fz_var(colorspace);

	if (!scissor)
		scissor = &fz_infinite_rect;

	if (cookie)
	{
		cookie->progress_max = list->len;
		cookie->progress = 0;
	}

	node = list->list;
	node_end = &list->list[list->len];
	for (; node != node_end ; node = next_node)
	{
		int empty;
		fz_display_node n = *node;

		next_node = node + n.size;

		/* Check the cookie for aborting */
		if (cookie)
		{
			if (cookie->abort)
				break;
			cookie->progress = progress++;
		}

		node++;
		if (n.rect)
		{
			rect = *(fz_rect *)node;
			node += SIZE_IN_NODES(sizeof(fz_rect));
		}
		if (n.cs)
		{
			int i;

			fz_drop_colorspace(ctx, colorspace);
			switch (n.cs)
			{
			default:
			case CS_GRAY_0:
				colorspace = fz_device_gray(ctx);
				color[0] = 0.0f;
				break;
			case CS_GRAY_1:
				colorspace = fz_device_gray(ctx);
				color[0] = 1.0f;
				break;
			case CS_RGB_0:
				colorspace = fz_device_rgb(ctx);
				color[0] = 0.0f;
				color[1] = 0.0f;
				color[2] = 0.0f;
				break;
			case CS_RGB_1:
				colorspace = fz_device_rgb(ctx);
				color[0] = 1.0f;
				color[1] = 1.0f;
				color[2] = 1.0f;
				break;
			case CS_CMYK_0:
				colorspace = fz_device_cmyk(ctx);
				color[0] = 0.0f;
				color[1] = 0.0f;
				color[2] = 0.0f;
				color[3] = 0.0f;
				break;
			case CS_CMYK_1:
				colorspace = fz_device_cmyk(ctx);
				color[0] = 0.0f;
				color[1] = 0.0f;
				color[2] = 0.0f;
				color[3] = 1.0f;
				break;
			case CS_OTHER_0:
				colorspace = fz_keep_colorspace(ctx, *(fz_colorspace **)(node));
				node += SIZE_IN_NODES(sizeof(fz_colorspace *));
				for (i = 0; i < colorspace->n; i++)
					color[i] = 0.0f;
				break;
			}
		}
		if (n.color)
		{
			memcpy(color, (float *)node, colorspace->n * sizeof(float));
			node += SIZE_IN_NODES(colorspace->n * sizeof(float));
		}
		if (n.alpha)
		{
			switch(n.alpha)
			{
			default:
			case ALPHA_0:
				alpha = 0.0f;
				break;
			case ALPHA_1:
				alpha = 1.0f;
				break;
			case ALPHA_PRESENT:
				alpha = *(float *)node;
				node += SIZE_IN_NODES(sizeof(float));
				break;
			}
		}
		if (n.ctm != 0)
		{
			float *packed_ctm = (float *)node;
			if (n.ctm & CTM_CHANGE_AD)
			{
				ctm.a = *packed_ctm++;
				ctm.d = *packed_ctm++;
				node += SIZE_IN_NODES(2*sizeof(float));
			}
			if (n.ctm & CTM_CHANGE_BC)
			{
				ctm.b = *packed_ctm++;
				ctm.c = *packed_ctm++;
				node += SIZE_IN_NODES(2*sizeof(float));
			}
			if (n.ctm & CTM_CHANGE_EF)
			{
				ctm.e = *packed_ctm++;
				ctm.f = *packed_ctm;
				node += SIZE_IN_NODES(2*sizeof(float));
			}
		}
		if (n.stroke)
		{
			fz_drop_stroke_state(ctx, stroke);
			stroke = fz_keep_stroke_state(ctx, *(fz_stroke_state **)node);
			node += SIZE_IN_NODES(sizeof(fz_stroke_state *));
		}
		if (n.path)
		{
			fz_drop_path(ctx, path);
			path = fz_keep_path(ctx, (fz_path *)node);
			node += SIZE_IN_NODES(fz_packed_path_size(path));
		}

		if (tile_skip_depth > 0)
		{
			if (n.cmd == FZ_CMD_BEGIN_TILE)
				tile_skip_depth++;
			else if (n.cmd == FZ_CMD_END_TILE)
				tile_skip_depth--;
			if (tile_skip_depth > 0)
				continue;
		}

		trans_rect = rect;
		fz_transform_rect(&trans_rect, top_ctm);

		/* cull objects to draw using a quick visibility test */

		if (tiled ||
			n.cmd == FZ_CMD_BEGIN_TILE || n.cmd == FZ_CMD_END_TILE ||
			n.cmd == FZ_CMD_BEGIN_PAGE || n.cmd == FZ_CMD_END_PAGE)
		{
			empty = 0;
		}
		else
		{
			fz_rect irect = trans_rect;
			fz_intersect_rect(&irect, scissor);
			empty = fz_is_empty_rect(&irect);
		}

		if (clipped || empty)
		{
			switch (n.cmd)
			{
			case FZ_CMD_CLIP_PATH:
			case FZ_CMD_CLIP_STROKE_PATH:
			case FZ_CMD_CLIP_STROKE_TEXT:
			case FZ_CMD_CLIP_IMAGE_MASK:
			case FZ_CMD_BEGIN_MASK:
			case FZ_CMD_BEGIN_GROUP:
				clipped++;
				continue;
			case FZ_CMD_CLIP_TEXT:
				/* Accumulated text has no extra pops */
				if (n.flags != 2)
					clipped++;
				continue;
			case FZ_CMD_POP_CLIP:
			case FZ_CMD_END_GROUP:
				if (!clipped)
					goto visible;
				clipped--;
				continue;
			case FZ_CMD_END_MASK:
				if (!clipped)
					goto visible;
				continue;
			default:
				continue;
			}
		}

visible:
		fz_concat(&trans_ctm, &ctm, top_ctm);

		fz_try(ctx)
		{
			switch (n.cmd)
			{
			case FZ_CMD_BEGIN_PAGE:
				fz_begin_page(ctx, dev, &trans_rect, &trans_ctm);
				break;
			case FZ_CMD_END_PAGE:
				fz_end_page(ctx, dev);
				break;
			case FZ_CMD_FILL_PATH:
				fz_fill_path(ctx, dev, path, n.flags, &trans_ctm, colorspace, color, alpha);
				break;
			case FZ_CMD_STROKE_PATH:
				fz_stroke_path(ctx, dev, path, stroke, &trans_ctm, colorspace, color, alpha);
				break;
			case FZ_CMD_CLIP_PATH:
				fz_clip_path(ctx, dev, path, &trans_rect, n.flags, &trans_ctm);
				break;
			case FZ_CMD_CLIP_STROKE_PATH:
				fz_clip_stroke_path(ctx, dev, path, &trans_rect, stroke, &trans_ctm);
				break;
			case FZ_CMD_FILL_TEXT:
				fz_fill_text(ctx, dev, *(fz_text **)node, &trans_ctm, colorspace, color, alpha);
				break;
			case FZ_CMD_STROKE_TEXT:
				fz_stroke_text(ctx, dev, *(fz_text **)node, stroke, &trans_ctm, colorspace, color, alpha);
				break;
			case FZ_CMD_CLIP_TEXT:
				fz_clip_text(ctx, dev, *(fz_text **)node, &trans_ctm, n.flags);
				break;
			case FZ_CMD_CLIP_STROKE_TEXT:
				fz_clip_stroke_text(ctx, dev, *(fz_text **)node, stroke, &trans_ctm);
				break;
			case FZ_CMD_IGNORE_TEXT:
				fz_ignore_text(ctx, dev, *(fz_text **)node, &trans_ctm);
				break;
			case FZ_CMD_FILL_SHADE:
				if ((dev->hints & FZ_IGNORE_SHADE) == 0)
					fz_fill_shade(ctx, dev, *(fz_shade **)node, &trans_ctm, alpha);
				break;
			case FZ_CMD_FILL_IMAGE:
				if ((dev->hints & FZ_IGNORE_IMAGE) == 0)
					fz_fill_image(ctx, dev, *(fz_image **)node, &trans_ctm, alpha);
				break;
			case FZ_CMD_FILL_IMAGE_MASK:
				if ((dev->hints & FZ_IGNORE_IMAGE) == 0)
					fz_fill_image_mask(ctx, dev, *(fz_image **)node, &trans_ctm, colorspace, color, alpha);
				break;
			case FZ_CMD_CLIP_IMAGE_MASK:
				if ((dev->hints & FZ_IGNORE_IMAGE) == 0)
					fz_clip_image_mask(ctx, dev, *(fz_image **)node, &trans_rect, &trans_ctm);
				break;
			case FZ_CMD_POP_CLIP:
				fz_pop_clip(ctx, dev);
				break;
			case FZ_CMD_BEGIN_MASK:
				fz_begin_mask(ctx, dev, &trans_rect, n.flags, colorspace, color);
				break;
			case FZ_CMD_END_MASK:
				fz_end_mask(ctx, dev);
				break;
			case FZ_CMD_BEGIN_GROUP:
				fz_begin_group(ctx, dev, &trans_rect, (n.flags & ISOLATED) != 0, (n.flags & KNOCKOUT) != 0, (n.flags>>2), alpha);
				break;
			case FZ_CMD_END_GROUP:
				fz_end_group(ctx, dev);
				break;
			case FZ_CMD_BEGIN_TILE:
			{
				int cached;
				fz_list_tile_data *data = (fz_list_tile_data *)node;
				fz_rect tile_rect;
				tiled++;
				tile_rect = data->view;
				cached = fz_begin_tile_id(ctx, dev, &rect, &tile_rect, data->xstep, data->ystep, &trans_ctm, n.flags);
				if (cached)
					tile_skip_depth = 1;
				break;
			}
			case FZ_CMD_END_TILE:
				tiled--;
				fz_end_tile(ctx, dev);
				break;
			}
		}
		fz_catch(ctx)
		{
			/* Swallow the error */
			if (cookie)
				cookie->errors++;
			if (fz_caught(ctx) == FZ_ERROR_ABORT)
				break;
			fz_warn(ctx, "Ignoring error during interpretation");
		}
	}
	fz_drop_colorspace(ctx, colorspace);
	fz_drop_stroke_state(ctx, stroke);
	fz_drop_path(ctx, path);
}
