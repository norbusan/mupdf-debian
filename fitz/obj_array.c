#include "fitz_base.h"
#include "fitz_stream.h"

void fz_freearray(fz_obj *obj);

fz_error
fz_newarray(fz_obj **op, int initialcap)
{
	fz_obj *obj;
	int i;

	obj = *op = fz_malloc(sizeof (fz_obj));
	if (!obj)
	    return fz_rethrow(-1, "out of memory: array struct");

	obj->refs = 1;
	obj->kind = FZ_ARRAY;

	obj->u.a.len = 0;
	obj->u.a.cap = initialcap > 0 ? initialcap : 6;

	obj->u.a.items = fz_malloc(sizeof (fz_obj*) * obj->u.a.cap);
	if (!obj->u.a.items)
	{
	    fz_free(obj);
	    return fz_rethrow(-1, "out of memory: array item buffer");
	}

	for (i = 0; i < obj->u.a.cap; i++)
		obj->u.a.items[i] = nil;

	return fz_okay;
}

fz_error
fz_copyarray(fz_obj **op, fz_obj *obj)
{
	fz_error error;
	fz_obj *new;
	int i;

	if (!fz_isarray(obj))
		return fz_throw("assert: not an array (%s)", fz_objkindstr(obj));

	error = fz_newarray(&new, fz_arraylen(obj));
	if (error)
	    return fz_rethrow(error, "cannot create new array");

	for (i = 0; i < fz_arraylen(obj); i++)
	{
		error = fz_arraypush(new, fz_arrayget(obj, i));
		if (error)
		{
		    fz_freearray(new);
		    return fz_rethrow(error, "cannot add item to array");
		}
	}

	*op = new;

	return fz_okay;
}

fz_error
fz_deepcopyarray(fz_obj **op, fz_obj *obj)
{
	fz_error error;
	fz_obj *new;
	fz_obj *val;
	int i;

	if (!fz_isarray(obj))
		return fz_throw("assert: not an array (%s)", fz_objkindstr(obj));

	error = fz_newarray(&new, fz_arraylen(obj));
	if (error)
	    return fz_rethrow(error, "cannot create new array");

	for (i = 0; i < fz_arraylen(obj); i++)
	{
		val = fz_arrayget(obj, i);

		if (fz_isarray(val))
		{
			error = fz_deepcopyarray(&val, val);
			if (error)
			{
			    fz_freearray(new);
			    return fz_rethrow(error, "cannot deep copy item");
			}

			error = fz_arraypush(new, val);
			if (error)
			{
			    fz_dropobj(val);
			    fz_freearray(new);
			    return fz_rethrow(error, "cannot add copied item to array");
			}

			fz_dropobj(val);
		}

		else if (fz_isdict(val))
		{
			error = fz_deepcopydict(&val, val);
			if (error)
			{
			    fz_freearray(new);
			    return fz_rethrow(error, "cannot deep copy item");
			}

			error = fz_arraypush(new, val);
			if (error)
			{
			    fz_dropobj(val);
			    fz_freearray(new);
			    return fz_rethrow(error, "cannot add copied item to array");
			}
			fz_dropobj(val);
		}

		else
		{
			error = fz_arraypush(new, val);
			if (error)
			{
			    fz_freearray(new);
			    return fz_rethrow(error, "cannot add copied item to array");
			}
		}
	}

	*op = new;

	return fz_okay;
}

int
fz_arraylen(fz_obj *obj)
{
	obj = fz_resolveindirect(obj);
	if (!fz_isarray(obj))
		return 0;
	return obj->u.a.len;
}

fz_obj *
fz_arrayget(fz_obj *obj, int i)
{
	obj = fz_resolveindirect(obj);

	if (!fz_isarray(obj))
		return nil;

	if (i < 0 || i >= obj->u.a.len)
		return nil;

	return obj->u.a.items[i];
}

fz_error
fz_arrayput(fz_obj *obj, int i, fz_obj *item)
{
	obj = fz_resolveindirect(obj);

	if (!fz_isarray(obj))
		return fz_throw("assert: not an array (%s)", fz_objkindstr(obj));
	if (i < 0)
		return fz_throw("assert: index %d < 0", i);
	if (i >= obj->u.a.len)
		return fz_throw("assert: index %d > length %d", i, obj->u.a.len);

	if (obj->u.a.items[i])
		fz_dropobj(obj->u.a.items[i]);
	obj->u.a.items[i] = fz_keepobj(item);

	return fz_okay;
}

static fz_error
growarray(fz_obj *obj)
{
	fz_obj **newitems;
	int newcap;
	int i;

	newcap = obj->u.a.cap * 2;
	newitems = fz_realloc(obj->u.a.items, sizeof (fz_obj*) * newcap);
	if (!newitems)
	    return fz_rethrow(-1, "out of memory: resize item buffer");

	obj->u.a.items = newitems;
	for (i = obj->u.a.cap ; i < newcap; i++)
		obj->u.a.items[i] = nil;
	obj->u.a.cap = newcap;

	return fz_okay;
}

fz_error
fz_arraypush(fz_obj *obj, fz_obj *item)
{
	fz_error error;

	obj = fz_resolveindirect(obj);

	if (!fz_isarray(obj))
		return fz_throw("assert: not an array (%s)", fz_objkindstr(obj));

	if (obj->u.a.len + 1 > obj->u.a.cap)
	{
		error = growarray(obj);
		if (error)
		    return fz_rethrow(error, "cannot grow item buffer");
	}

	obj->u.a.items[obj->u.a.len] = fz_keepobj(item);
	obj->u.a.len++;

	return fz_okay;
}

void
fz_freearray(fz_obj *obj)
{
	int i;

	assert(obj->kind == FZ_ARRAY);

	for (i = 0; i < obj->u.a.len; i++)
		if (obj->u.a.items[i])
			fz_dropobj(obj->u.a.items[i]);

	fz_free(obj->u.a.items);
	fz_free(obj);
}

