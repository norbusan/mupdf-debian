#include "fitz_base.h"
#include "fitz_tree.h"

/*
 * Over
 */

fz_error
fz_newovernode(fz_node **nodep)
{
	fz_node *node;

	node = *nodep = fz_malloc(sizeof (fz_overnode));
	if (!node)
		return fz_rethrow(-1, "out of memory");

	fz_initnode(node, FZ_NOVER);

	return fz_okay;
}

fz_rect
fz_boundovernode(fz_overnode *node, fz_matrix ctm)
{
	fz_node *child;
	fz_rect bbox;
	fz_rect temp;

	child = node->super.first;
	if (!child)
		return fz_emptyrect;

	bbox = fz_boundnode(child, ctm);

	child = child->next;
	while (child)
	{
		temp = fz_boundnode(child, ctm);
		bbox = fz_mergerects(temp, bbox);
		child = child->next;
	}

	return bbox;
}

/*
 * Mask
 */

fz_error
fz_newmasknode(fz_node **nodep)
{
	fz_node *node;

	node = *nodep = fz_malloc(sizeof (fz_masknode));
	if (!node)
		return fz_rethrow(-1, "out of memory");

	fz_initnode(node, FZ_NMASK);

	return fz_okay;
}

fz_rect
fz_boundmasknode(fz_masknode *node, fz_matrix ctm)
{
	fz_node *shape;
	fz_node *color;
	fz_rect one, two;

	shape = node->super.first;
	color = shape->next;

	one = fz_boundnode(shape, ctm);
	two = fz_boundnode(color, ctm);
	return fz_intersectrects(one, two);
}

/*
 * Blend
 */

fz_error
fz_newblendnode(fz_node **nodep, fz_blendkind b, int i, int k)
{
	fz_blendnode *node;

	node = fz_malloc(sizeof (fz_blendnode));
	if (!node)
		return fz_rethrow(-1, "out of memory");
	*nodep = (fz_node*)node;

	fz_initnode((fz_node*)node, FZ_NBLEND);
	node->mode = b;
	node->isolated = i;
	node->knockout = k;

	return fz_okay;
}

fz_rect
fz_boundblendnode(fz_blendnode *node, fz_matrix ctm)
{
	fz_node *child;
	fz_rect bbox;
	fz_rect temp;

	child = node->super.first;
	if (!child)
		return fz_emptyrect;

	bbox = fz_boundnode(child, ctm);

	child = child->next;
	while (child)
	{
		temp = fz_boundnode(child, ctm);
		bbox = fz_mergerects(temp, bbox);
		child = child->next;
	}

	return bbox;
}

void
fz_dropblendnode(fz_blendnode *node)
{
	fz_dropcolorspace(node->cs);
}

/*
 * Transform
 */

fz_error
fz_newtransformnode(fz_node **nodep, fz_matrix m)
{
	fz_transformnode *node;

	node = fz_malloc(sizeof (fz_transformnode));
	if (!node)
		return fz_rethrow(-1, "out of memory");
	*nodep = (fz_node*)node;

	fz_initnode((fz_node*)node, FZ_NTRANSFORM);
	node->m = m;

	return fz_okay;
}

fz_rect
fz_boundtransformnode(fz_transformnode *node, fz_matrix ctm)
{
	if (!node->super.first)
		return fz_emptyrect;
	return fz_boundnode(node->super.first, fz_concat(node->m, ctm));
}

/*
 * Link to tree
 */

fz_error
fz_newlinknode(fz_node **nodep, fz_tree *subtree)
{
	fz_linknode *node;

	node = fz_malloc(sizeof (fz_linknode));
	if (!node)
		return fz_rethrow(-1, "out of memory");
	*nodep = (fz_node*)node;

	fz_initnode((fz_node*)node, FZ_NLINK);
	node->tree = fz_keeptree(subtree);

	return fz_okay;
}

void
fz_droplinknode(fz_linknode *node)
{
	fz_droptree(node->tree);
}

fz_rect
fz_boundlinknode(fz_linknode *node, fz_matrix ctm)
{
	return fz_boundtree(node->tree, ctm);
}

/*
 * Solid color
 */

fz_error
fz_newsolidnode(fz_node **nodep, float a, fz_colorspace *cs, int n, float *v)
{
	fz_solidnode *node;
	int i;

	node = fz_malloc(sizeof(fz_solidnode) + sizeof(float) * n);
	if (!node)
		return fz_rethrow(-1, "out of memory");
	*nodep = (fz_node*)node;

	fz_initnode((fz_node*)node, FZ_NCOLOR);
	node->a = a;
	node->cs = fz_keepcolorspace(cs);
	node->n = n;
	for (i = 0; i < n; i++)
		node->samples[i] = v[i];

	return fz_okay;
}

fz_rect
fz_boundsolidnode(fz_solidnode *node, fz_matrix ctm)
{
	return fz_infiniterect;
}

void
fz_dropsolidnode(fz_solidnode *node)
{
	fz_dropcolorspace(node->cs);
}

/*
 * Image node
 */

fz_error
fz_newimagenode(fz_node **nodep, fz_image *image)
{
	fz_imagenode *node;

	node = fz_malloc(sizeof (fz_imagenode));
	if (!node)
		return fz_rethrow(-1, "out of memory");
	*nodep = (fz_node*)node;

	fz_initnode((fz_node*)node, FZ_NIMAGE);
	node->image = fz_keepimage(image);

	return fz_okay;
}

void
fz_dropimagenode(fz_imagenode *node)
{
	fz_dropimage(node->image);
}

fz_rect
fz_boundimagenode(fz_imagenode *node, fz_matrix ctm)
{
	fz_rect bbox;
	bbox.x0 = 0;
	bbox.y0 = 0;
	bbox.x1 = 1;
	bbox.y1 = 1;
	return fz_transformaabb(ctm, bbox);
}

/*
 * Shade node
 */

fz_error
fz_newshadenode(fz_node **nodep, fz_shade *shade)
{
	fz_shadenode *node;

	node = fz_malloc(sizeof (fz_shadenode));
	if (!node)
		return fz_rethrow(-1, "out of memory");
	*nodep = (fz_node*)node;

	fz_initnode((fz_node*)node, FZ_NSHADE);
	node->shade = fz_keepshade(shade);

	return fz_okay;
}

void
fz_dropshadenode(fz_shadenode *node)
{
	fz_dropshade(node->shade);
}

fz_rect
fz_boundshadenode(fz_shadenode *node, fz_matrix ctm)
{
	return fz_boundshade(node->shade, ctm);
}

