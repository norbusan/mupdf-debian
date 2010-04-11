#include "fitz.h"

fz_rect fz_infiniterect = { 1, 1, -1, -1 };
fz_rect fz_emptyrect = { 0, 0, 0, 0 };

static fz_bbox empty = { 0, 0, 0, 0 };

fz_bbox
fz_roundrect(fz_rect f)
{
	fz_bbox i;
	i.x0 = floor(f.x0);
	i.y0 = floor(f.y0);
	i.x1 = ceil(f.x1);
	i.y1 = ceil(f.y1);
	return i;
}

fz_bbox
fz_intersectbbox(fz_bbox a, fz_bbox b)
{
	fz_bbox r;
	if (fz_isinfiniterect(a)) return b;
	if (fz_isinfiniterect(b)) return a;
	r.x0 = MAX(a.x0, b.x0);
	r.y0 = MAX(a.y0, b.y0);
	r.x1 = MIN(a.x1, b.x1);
	r.y1 = MIN(a.y1, b.y1);
	return (r.x1 < r.x0 || r.y1 < r.y0) ? empty : r;
}

