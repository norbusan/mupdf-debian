#include "mupdf/fitz.h"

typedef struct fz_display_node_s fz_display_node;

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

struct fz_display_node_s
{
	fz_display_command cmd;
	fz_display_node *next;
	fz_rect rect;
	union {
		fz_path *path;
		fz_text *text;
		fz_shade *shade;
		fz_image *image;
		int blendmode;
	} item;
	fz_stroke_state *stroke;
	int flag; /* even_odd, accumulate, isolated/knockout... */
	fz_matrix ctm;
	fz_colorspace *colorspace;
	float alpha;
	float color[FZ_MAX_COLORS];
};

struct fz_display_list_s
{
	fz_storable storable;
	fz_display_node *first;
	fz_display_node *last;
	int len;

	int top;
	struct {
		fz_rect *update;
		fz_rect rect;
	} stack[STACK_SIZE];
	int tiled;
};

enum { ISOLATED = 1, KNOCKOUT = 2 };

static fz_display_node *
fz_new_display_node(fz_context *ctx, fz_display_command cmd, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	int i;

	node = fz_malloc_struct(ctx, fz_display_node);
	node->cmd = cmd;
	node->next = NULL;
	node->rect = fz_empty_rect;
	node->item.path = NULL;
	node->stroke = NULL;
	node->flag = (cmd == FZ_CMD_BEGIN_TILE ? fz_gen_id(ctx) : 0);
	node->ctm = *ctm;
	if (colorspace)
	{
		node->colorspace = fz_keep_colorspace(ctx, colorspace);
		if (color)
		{
			for (i = 0; i < node->colorspace->n; i++)
				node->color[i] = color[i];
		}
	}
	else
	{
		node->colorspace = NULL;
	}
	node->alpha = alpha;

	return node;
}

static void
fz_append_display_node(fz_display_list *list, fz_display_node *node)
{
	switch (node->cmd)
	{
	case FZ_CMD_CLIP_PATH:
	case FZ_CMD_CLIP_STROKE_PATH:
	case FZ_CMD_CLIP_IMAGE_MASK:
		if (list->top < STACK_SIZE)
		{
			list->stack[list->top].update = &node->rect;
			list->stack[list->top].rect = fz_empty_rect;
		}
		list->top++;
		break;
	case FZ_CMD_CLIP_TEXT:
		/* don't reset the clip rect for accumulated text */
		if (node->flag == 2)
			break;
		/* fallthrough */
	case FZ_CMD_END_MASK:
	case FZ_CMD_CLIP_STROKE_TEXT:
		if (list->top < STACK_SIZE)
		{
			list->stack[list->top].update = NULL;
			list->stack[list->top].rect = fz_empty_rect;
		}
		list->top++;
		break;
	case FZ_CMD_BEGIN_TILE:
		list->tiled++;
		if (list->top > 0 && list->top <= STACK_SIZE)
		{
			list->stack[list->top-1].rect = fz_infinite_rect;
		}
		break;
	case FZ_CMD_END_TILE:
		list->tiled--;
		break;
	case FZ_CMD_END_GROUP:
		break;
	case FZ_CMD_POP_CLIP:
		if (list->top > STACK_SIZE)
		{
			list->top--;
			node->rect = fz_infinite_rect;
		}
		else if (list->top > 0)
		{
			fz_rect *update;
			list->top--;
			update = list->stack[list->top].update;
			if (list->tiled == 0)
			{
				if (update)
				{
					fz_intersect_rect(update, &list->stack[list->top].rect);
					node->rect = *update;
				}
				else
					node->rect = list->stack[list->top].rect;
			}
			else
				node->rect = fz_infinite_rect;
		}
		/* fallthrough */
	default:
		if (list->top > 0 && list->tiled == 0 && list->top <= STACK_SIZE)
			fz_union_rect(&list->stack[list->top-1].rect, &node->rect);
		break;
	}
	if (!list->first)
	{
		list->first = node;
		list->last = node;
	}
	else
	{
		list->last->next = node;
		list->last = node;
	}
	list->len++;
}

static void
fz_free_display_node(fz_context *ctx, fz_display_node *node)
{
	switch (node->cmd)
	{
	case FZ_CMD_FILL_PATH:
	case FZ_CMD_STROKE_PATH:
	case FZ_CMD_CLIP_PATH:
	case FZ_CMD_CLIP_STROKE_PATH:
		fz_free_path(ctx, node->item.path);
		break;
	case FZ_CMD_FILL_TEXT:
	case FZ_CMD_STROKE_TEXT:
	case FZ_CMD_CLIP_TEXT:
	case FZ_CMD_CLIP_STROKE_TEXT:
	case FZ_CMD_IGNORE_TEXT:
		fz_free_text(ctx, node->item.text);
		break;
	case FZ_CMD_FILL_SHADE:
		fz_drop_shade(ctx, node->item.shade);
		break;
	case FZ_CMD_FILL_IMAGE:
	case FZ_CMD_FILL_IMAGE_MASK:
	case FZ_CMD_CLIP_IMAGE_MASK:
		fz_drop_image(ctx, node->item.image);
		break;
	case FZ_CMD_POP_CLIP:
	case FZ_CMD_BEGIN_MASK:
	case FZ_CMD_END_MASK:
	case FZ_CMD_BEGIN_GROUP:
	case FZ_CMD_END_GROUP:
	case FZ_CMD_BEGIN_TILE:
	case FZ_CMD_END_TILE:
	case FZ_CMD_BEGIN_PAGE:
	case FZ_CMD_END_PAGE:
		break;
	}
	if (node->stroke)
		fz_drop_stroke_state(ctx, node->stroke);
	if (node->colorspace)
		fz_drop_colorspace(ctx, node->colorspace);
	fz_free(ctx, node);
}

static void
fz_list_begin_page(fz_device *dev, const fz_rect *mediabox, const fz_matrix *ctm)
{
	fz_context *ctx = dev->ctx;
	fz_display_node *node = fz_new_display_node(ctx, FZ_CMD_BEGIN_PAGE, ctm, NULL, NULL, 0);
	node->rect = *mediabox;
	fz_transform_rect(&node->rect, ctm);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_end_page(fz_device *dev)
{
	fz_context *ctx = dev->ctx;
	fz_display_node *node = fz_new_display_node(ctx, FZ_CMD_END_PAGE, &fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_path(fz_device *dev, fz_path *path, int even_odd, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_FILL_PATH, ctm, colorspace, color, alpha);
	fz_try(ctx)
	{
		fz_bound_path(dev->ctx, path, NULL, ctm, &node->rect);
		node->item.path = fz_clone_path(dev->ctx, path);
		node->flag = even_odd;
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_stroke_path(fz_device *dev, fz_path *path, fz_stroke_state *stroke,
	const fz_matrix *ctm, fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_STROKE_PATH, ctm, colorspace, color, alpha);
	fz_try(ctx)
	{
		fz_bound_path(dev->ctx, path, stroke, ctm, &node->rect);
		node->item.path = fz_clone_path(dev->ctx, path);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_path(fz_device *dev, fz_path *path, const fz_rect *rect, int even_odd, const fz_matrix *ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_PATH, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		fz_bound_path(dev->ctx, path, NULL, ctm, &node->rect);
		if (rect)
			fz_intersect_rect(&node->rect, rect);
		node->item.path = fz_clone_path(dev->ctx, path);
		node->flag = even_odd;
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_stroke_path(fz_device *dev, fz_path *path, const fz_rect *rect, fz_stroke_state *stroke, const fz_matrix *ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_STROKE_PATH, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		fz_bound_path(dev->ctx, path, stroke, ctm, &node->rect);
		if (rect)
			fz_intersect_rect(&node->rect, rect);
		node->item.path = fz_clone_path(dev->ctx, path);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_text(fz_device *dev, fz_text *text, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_FILL_TEXT, ctm, colorspace, color, alpha);
	fz_try(ctx)
	{
		fz_bound_text(dev->ctx, text, NULL, ctm, &node->rect);
		node->item.text = fz_clone_text(dev->ctx, text);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_stroke_text(fz_device *dev, fz_text *text, fz_stroke_state *stroke, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_STROKE_TEXT, ctm, colorspace, color, alpha);
	node->item.text = NULL;
	fz_try(ctx)
	{
		fz_bound_text(dev->ctx, text, stroke, ctm, &node->rect);
		node->item.text = fz_clone_text(dev->ctx, text);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_text(fz_device *dev, fz_text *text, const fz_matrix *ctm, int accumulate)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_TEXT, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		fz_bound_text(dev->ctx, text, NULL, ctm, &node->rect);
		node->item.text = fz_clone_text(dev->ctx, text);
		node->flag = accumulate;
		/* when accumulating, be conservative about culling */
		if (accumulate)
			node->rect = fz_infinite_rect;
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_stroke_text(fz_device *dev, fz_text *text, fz_stroke_state *stroke, const fz_matrix *ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_CLIP_STROKE_TEXT, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		fz_bound_text(dev->ctx, text, stroke, ctm, &node->rect);
		node->item.text = fz_clone_text(dev->ctx, text);
		node->stroke = fz_keep_stroke_state(dev->ctx, stroke);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_ignore_text(fz_device *dev, fz_text *text, const fz_matrix *ctm)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_IGNORE_TEXT, ctm, NULL, NULL, 0);
	fz_try(ctx)
	{
		fz_bound_text(dev->ctx, text, NULL, ctm, &node->rect);
		node->item.text = fz_clone_text(dev->ctx, text);
	}
	fz_catch(ctx)
	{
		fz_free_display_node(ctx, node);
		fz_rethrow(ctx);
	}
	fz_append_display_node(dev->user, node);
}

static void
fz_list_pop_clip(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_POP_CLIP, &fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_shade(fz_device *dev, fz_shade *shade, const fz_matrix *ctm, float alpha)
{
	fz_display_node *node;
	fz_context *ctx = dev->ctx;
	node = fz_new_display_node(ctx, FZ_CMD_FILL_SHADE, ctm, NULL, NULL, alpha);
	fz_bound_shade(ctx, shade, ctm, &node->rect);
	node->item.shade = fz_keep_shade(ctx, shade);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_image(fz_device *dev, fz_image *image, const fz_matrix *ctm, float alpha)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_FILL_IMAGE, ctm, NULL, NULL, alpha);
	node->rect = fz_unit_rect;
	fz_transform_rect(&node->rect, ctm);
	node->item.image = fz_keep_image(dev->ctx, image);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_fill_image_mask(fz_device *dev, fz_image *image, const fz_matrix *ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_FILL_IMAGE_MASK, ctm, colorspace, color, alpha);
	node->rect = fz_unit_rect;
	fz_transform_rect(&node->rect, ctm);
	node->item.image = fz_keep_image(dev->ctx, image);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_clip_image_mask(fz_device *dev, fz_image *image, const fz_rect *rect, const fz_matrix *ctm)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_CLIP_IMAGE_MASK, ctm, NULL, NULL, 0);
	node->rect = fz_unit_rect;
	fz_transform_rect(&node->rect, ctm);
	if (rect)
		fz_intersect_rect(&node->rect, rect);
	node->item.image = fz_keep_image(dev->ctx, image);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_begin_mask(fz_device *dev, const fz_rect *rect, int luminosity, fz_colorspace *colorspace, float *color)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_BEGIN_MASK, &fz_identity, colorspace, color, 0);
	node->rect = *rect;
	node->flag = luminosity;
	fz_append_display_node(dev->user, node);
}

static void
fz_list_end_mask(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_END_MASK, &fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static void
fz_list_begin_group(fz_device *dev, const fz_rect *rect, int isolated, int knockout, int blendmode, float alpha)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_BEGIN_GROUP, &fz_identity, NULL, NULL, alpha);
	node->rect = *rect;
	node->item.blendmode = blendmode;
	node->flag |= isolated ? ISOLATED : 0;
	node->flag |= knockout ? KNOCKOUT : 0;
	fz_append_display_node(dev->user, node);
}

static void
fz_list_end_group(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_END_GROUP, &fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

static int
fz_list_begin_tile(fz_device *dev, const fz_rect *area, const fz_rect *view, float xstep, float ystep, const fz_matrix *ctm, int id)
{
	/* We ignore id here, as we will pass on our own id */
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_BEGIN_TILE, ctm, NULL, NULL, 0);
	node->rect = *area;
	node->color[0] = xstep;
	node->color[1] = ystep;
	node->color[2] = view->x0;
	node->color[3] = view->y0;
	node->color[4] = view->x1;
	node->color[5] = view->y1;
	fz_append_display_node(dev->user, node);
	return 0;
}

static void
fz_list_end_tile(fz_device *dev)
{
	fz_display_node *node;
	node = fz_new_display_node(dev->ctx, FZ_CMD_END_TILE, &fz_identity, NULL, NULL, 0);
	fz_append_display_node(dev->user, node);
}

fz_device *
fz_new_list_device(fz_context *ctx, fz_display_list *list)
{
	fz_device *dev = fz_new_device(ctx, list);

	dev->begin_page = fz_list_begin_page;
	dev->end_page = fz_list_end_page;

	dev->fill_path = fz_list_fill_path;
	dev->stroke_path = fz_list_stroke_path;
	dev->clip_path = fz_list_clip_path;
	dev->clip_stroke_path = fz_list_clip_stroke_path;

	dev->fill_text = fz_list_fill_text;
	dev->stroke_text = fz_list_stroke_text;
	dev->clip_text = fz_list_clip_text;
	dev->clip_stroke_text = fz_list_clip_stroke_text;
	dev->ignore_text = fz_list_ignore_text;

	dev->fill_shade = fz_list_fill_shade;
	dev->fill_image = fz_list_fill_image;
	dev->fill_image_mask = fz_list_fill_image_mask;
	dev->clip_image_mask = fz_list_clip_image_mask;

	dev->pop_clip = fz_list_pop_clip;

	dev->begin_mask = fz_list_begin_mask;
	dev->end_mask = fz_list_end_mask;
	dev->begin_group = fz_list_begin_group;
	dev->end_group = fz_list_end_group;

	dev->begin_tile = fz_list_begin_tile;
	dev->end_tile = fz_list_end_tile;

	return dev;
}

static void
fz_free_display_list(fz_context *ctx, fz_storable *list_)
{
	fz_display_list *list = (fz_display_list *)list_;
	fz_display_node *node;

	if (list == NULL)
		return;
	node = list->first;
	while (node)
	{
		fz_display_node *next = node->next;
		fz_free_display_node(ctx, node);
		node = next;
	}
	fz_free(ctx, list);
}

fz_display_list *
fz_new_display_list(fz_context *ctx)
{
	fz_display_list *list = fz_malloc_struct(ctx, fz_display_list);
	FZ_INIT_STORABLE(list, 1, fz_free_display_list);
	list->first = NULL;
	list->last = NULL;
	list->len = 0;
	list->top = 0;
	list->tiled = 0;
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

static fz_display_node *
skip_to_end_tile(fz_display_node *node, int *progress)
{
	fz_display_node *next;
	int depth = 1;

	/* Skip through until we find the matching end_tile. Note that
	 * (somewhat nastily) we return the PREVIOUS node to this to help
	 * the calling routine. */
	do
	{
		next = node->next;
		if (next == NULL)
			break;
		if (next->cmd == FZ_CMD_BEGIN_TILE)
			depth++;
		else if (next->cmd == FZ_CMD_END_TILE)
		{
			depth--;
			if (depth == 0)
				return node;
		}
		(*progress)++;
		node = next;
	}
	while (1);

	return NULL;
}

void
fz_run_display_list(fz_display_list *list, fz_device *dev, const fz_matrix *top_ctm, const fz_rect *scissor, fz_cookie *cookie)
{
	fz_display_node *node;
	fz_matrix ctm;
	int clipped = 0;
	int tiled = 0;
	int progress = 0;
	fz_context *ctx = dev->ctx;

	if (!scissor)
		scissor = &fz_infinite_rect;

	if (cookie)
	{
		cookie->progress_max = list->len;
		cookie->progress = 0;
	}

	for (node = list->first; node; node = node->next)
	{
		int empty;

		fz_rect node_rect = node->rect;
		fz_transform_rect(&node_rect, top_ctm);

		/* Check the cookie for aborting */
		if (cookie)
		{
			if (cookie->abort)
				break;
			cookie->progress = progress++;
		}

		/* cull objects to draw using a quick visibility test */

		if (tiled ||
			node->cmd == FZ_CMD_BEGIN_TILE || node->cmd == FZ_CMD_END_TILE ||
			node->cmd == FZ_CMD_BEGIN_PAGE || node->cmd == FZ_CMD_END_PAGE)
		{
			empty = 0;
		}
		else
		{
			fz_rect rect = node_rect;
			fz_intersect_rect(&rect, scissor);
			empty = fz_is_empty_rect(&rect);
		}

		if (clipped || empty)
		{
			switch (node->cmd)
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
				if (node->flag != 2)
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
		fz_concat(&ctm, &node->ctm, top_ctm);

		fz_try(ctx)
		{
			switch (node->cmd)
			{
			case FZ_CMD_BEGIN_PAGE:
				fz_begin_page(dev, &node_rect, &ctm);
				break;
			case FZ_CMD_END_PAGE:
				fz_end_page(dev);
				break;
			case FZ_CMD_FILL_PATH:
				fz_fill_path(dev, node->item.path, node->flag, &ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_STROKE_PATH:
				fz_stroke_path(dev, node->item.path, node->stroke, &ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_CLIP_PATH:
				fz_clip_path(dev, node->item.path, &node_rect, node->flag, &ctm);
				break;
			case FZ_CMD_CLIP_STROKE_PATH:
				fz_clip_stroke_path(dev, node->item.path, &node_rect, node->stroke, &ctm);
				break;
			case FZ_CMD_FILL_TEXT:
				fz_fill_text(dev, node->item.text, &ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_STROKE_TEXT:
				fz_stroke_text(dev, node->item.text, node->stroke, &ctm,
					node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_CLIP_TEXT:
				fz_clip_text(dev, node->item.text, &ctm, node->flag);
				break;
			case FZ_CMD_CLIP_STROKE_TEXT:
				fz_clip_stroke_text(dev, node->item.text, node->stroke, &ctm);
				break;
			case FZ_CMD_IGNORE_TEXT:
				fz_ignore_text(dev, node->item.text, &ctm);
				break;
			case FZ_CMD_FILL_SHADE:
				if ((dev->hints & FZ_IGNORE_SHADE) == 0)
					fz_fill_shade(dev, node->item.shade, &ctm, node->alpha);
				break;
			case FZ_CMD_FILL_IMAGE:
				if ((dev->hints & FZ_IGNORE_IMAGE) == 0)
					fz_fill_image(dev, node->item.image, &ctm, node->alpha);
				break;
			case FZ_CMD_FILL_IMAGE_MASK:
				if ((dev->hints & FZ_IGNORE_IMAGE) == 0)
					fz_fill_image_mask(dev, node->item.image, &ctm,
						node->colorspace, node->color, node->alpha);
				break;
			case FZ_CMD_CLIP_IMAGE_MASK:
				if ((dev->hints & FZ_IGNORE_IMAGE) == 0)
					fz_clip_image_mask(dev, node->item.image, &node_rect, &ctm);
				break;
			case FZ_CMD_POP_CLIP:
				fz_pop_clip(dev);
				break;
			case FZ_CMD_BEGIN_MASK:
				fz_begin_mask(dev, &node_rect, node->flag, node->colorspace, node->color);
				break;
			case FZ_CMD_END_MASK:
				fz_end_mask(dev);
				break;
			case FZ_CMD_BEGIN_GROUP:
				fz_begin_group(dev, &node_rect,
					(node->flag & ISOLATED) != 0, (node->flag & KNOCKOUT) != 0,
					node->item.blendmode, node->alpha);
				break;
			case FZ_CMD_END_GROUP:
				fz_end_group(dev);
				break;
			case FZ_CMD_BEGIN_TILE:
			{
				int cached;
				fz_rect tile_rect;
				tiled++;
				tile_rect.x0 = node->color[2];
				tile_rect.y0 = node->color[3];
				tile_rect.x1 = node->color[4];
				tile_rect.y1 = node->color[5];
				cached = fz_begin_tile_id(dev, &node->rect, &tile_rect, node->color[0], node->color[1], &ctm, node->flag);
				if (cached)
					node = skip_to_end_tile(node, &progress);
				break;
			}
			case FZ_CMD_END_TILE:
				tiled--;
				fz_end_tile(dev);
				break;
			}
		}
		fz_catch(ctx)
		{
			/* Swallow the error */
			if (cookie)
				cookie->errors++;
			fz_warn(ctx, "Ignoring error during interpretation");
		}
	}
}
