#include "jsi.h"
#include "jsvalue.h"
#include "jsbuiltin.h"

unsigned int js_getlength(js_State *J, int idx)
{
	unsigned int len;
	js_getproperty(J, idx, "length");
	len = js_touint32(J, -1);
	js_pop(J, 1);
	return len;
}

void js_setlength(js_State *J, int idx, unsigned int len)
{
	js_pushnumber(J, len);
	js_setproperty(J, idx < 0 ? idx - 1: idx, "length");
}

int js_hasindex(js_State *J, int idx, unsigned int i)
{
	char buf[32];
	return js_hasproperty(J, idx, js_itoa(buf, i));
}

void js_getindex(js_State *J, int idx, unsigned int i)
{
	char buf[32];
	js_getproperty(J, idx, js_itoa(buf, i));
}

void js_setindex(js_State *J, int idx, unsigned int i)
{
	char buf[32];
	js_setproperty(J, idx, js_itoa(buf, i));
}

void js_delindex(js_State *J, int idx, unsigned int i)
{
	char buf[32];
	js_delproperty(J, idx, js_itoa(buf, i));
}

static void jsB_new_Array(js_State *J)
{
	unsigned int i, top = js_gettop(J);

	js_newarray(J);

	if (top == 2) {
		if (js_isnumber(J, 1)) {
			js_copy(J, 1);
			js_setproperty(J, -2, "length");
		} else {
			js_copy(J, 1);
			js_setindex(J, -2, 0);
		}
	} else {
		for (i = 1; i < top; ++i) {
			js_copy(J, i);
			js_setindex(J, -2, i - 1);
		}
	}
}

static void Ap_concat(js_State *J)
{
	unsigned int i, top = js_gettop(J);
	unsigned int n, k, len;

	js_newarray(J);
	n = 0;

	for (i = 0; i < top; ++i) {
		js_copy(J, i);
		if (js_isarray(J, -1)) {
			len = js_getlength(J, -1);
			for (k = 0; k < len; ++k)
				if (js_hasindex(J, -1, k))
					js_setindex(J, -3, n++);
			js_pop(J, 1);
		} else {
			js_setindex(J, -2, n++);
		}
	}
}

static void Ap_join(js_State *J)
{
	char * volatile out = NULL;
	const char *sep;
	const char *r;
	unsigned int seplen;
	unsigned int k, n, len;

	len = js_getlength(J, 0);

	if (js_isdefined(J, 1)) {
		sep = js_tostring(J, 1);
		seplen = strlen(sep);
	} else {
		sep = ",";
		seplen = 1;
	}

	if (len == 0) {
		js_pushliteral(J, "");
		return;
	}

	if (js_try(J)) {
		js_free(J, out);
		js_throw(J);
	}

	n = 1;
	for (k = 0; k < len; ++k) {
		js_getindex(J, 0, k);
		if (js_isundefined(J, -1) || js_isnull(J, -1))
			r = "";
		else
			r = js_tostring(J, -1);
		n += strlen(r);

		if (k == 0) {
			out = js_malloc(J, n);
			strcpy(out, r);
		} else {
			n += seplen;
			out = realloc(out, n);
			strcat(out, sep);
			strcat(out, r);
		}

		js_pop(J, 1);
	}

	js_pushstring(J, out);
	js_endtry(J);
	js_free(J, out);
}

static void Ap_pop(js_State *J)
{
	unsigned int n;

	n = js_getlength(J, 0);

	if (n > 0) {
		js_getindex(J, 0, n - 1);
		js_delindex(J, 0, n - 1);
		js_setlength(J, 0, n - 1);
	} else {
		js_setlength(J, 0, 0);
		js_pushundefined(J);
	}
}

static void Ap_push(js_State *J)
{
	unsigned int i, top = js_gettop(J);
	unsigned int n;

	n = js_getlength(J, 0);

	for (i = 1; i < top; ++i, ++n) {
		js_copy(J, i);
		js_setindex(J, 0, n);
	}

	js_setlength(J, 0, n);

	js_pushnumber(J, n);
}

static void Ap_reverse(js_State *J)
{
	unsigned int len, middle, lower;

	len = js_getlength(J, 0);
	middle = len / 2;
	lower = 0;

	while (lower != middle) {
		unsigned int upper = len - lower - 1;
		int haslower = js_hasindex(J, 0, lower);
		int hasupper = js_hasindex(J, 0, upper);
		if (haslower && hasupper) {
			js_setindex(J, 0, lower);
			js_setindex(J, 0, upper);
		} else if (hasupper) {
			js_setindex(J, 0, lower);
			js_delindex(J, 0, upper);
		} else if (haslower) {
			js_setindex(J, 0, upper);
			js_delindex(J, 0, lower);
		}
		++lower;
	}

	js_copy(J, 0);
}

static void Ap_shift(js_State *J)
{
	unsigned int k, len;

	len = js_getlength(J, 0);

	if (len == 0) {
		js_setlength(J, 0, 0);
		js_pushundefined(J);
		return;
	}

	js_getindex(J, 0, 0);

	for (k = 1; k < len; ++k) {
		if (js_hasindex(J, 0, k))
			js_setindex(J, 0, k - 1);
		else
			js_delindex(J, 0, k - 1);
	}

	js_delindex(J, 0, len - 1);
	js_setlength(J, 0, len - 1);
}

static void Ap_slice(js_State *J)
{
	unsigned int len, s, e, n;
	double sv, ev;

	js_newarray(J);

	len = js_getlength(J, 0);
	sv = js_tointeger(J, 1);
	ev = js_isdefined(J, 2) ? js_tointeger(J, 2) : len;

	if (sv < 0) sv = sv + len;
	if (ev < 0) ev = ev + len;

	s = sv < 0 ? 0 : sv > len ? len : sv;
	e = ev < 0 ? 0 : ev > len ? len : ev;

	for (n = 0; s < e; ++s, ++n)
		if (js_hasindex(J, 0, s))
			js_setindex(J, -2, n);
}

static int compare(js_State *J, unsigned int x, unsigned int y, int *hasx, int *hasy, int hasfn)
{
	const char *sx, *sy;
	int c;

	*hasx = js_hasindex(J, 0, x);
	*hasy = js_hasindex(J, 0, y);

	if (*hasx && *hasy) {
		int unx = js_isundefined(J, -2);
		int uny = js_isundefined(J, -1);
		if (unx && uny) return 0;
		if (unx) return 1;
		if (uny) return -1;

		if (hasfn) {
			js_copy(J, 1); /* copy function */
			js_pushundefinedthis(J); /* set this object */
			js_copy(J, -4); /* copy x */
			js_copy(J, -4); /* copy y */
			js_call(J, 2);
			c = js_tonumber(J, -1);
			js_pop(J, 1);
			return c;
		}

		sx = js_tostring(J, -2);
		sy = js_tostring(J, -1);
		return strcmp(sx, sy);
	}

	if (*hasx) return -1;
	if (*hasy) return 1;
	return 0;
}

static void Ap_sort(js_State *J)
{
	unsigned int len, i, k;
	int hasx, hasy, hasfn;

	len = js_getlength(J, 0);

	hasfn = js_iscallable(J, 1);

	for (i = 1; i < len; ++i) {
		k = i;
		while (k > 0 && compare(J, k - 1, k, &hasx, &hasy, hasfn) > 0) {
			if (hasx && hasy) {
				js_setindex(J, 0, k - 1);
				js_setindex(J, 0, k);
			} else if (hasx) {
				js_delindex(J, 0, k - 1);
				js_setindex(J, 0, k);
			} else if (hasy) {
				js_setindex(J, 0, k - 1);
				js_delindex(J, 0, k);
			}
			--k;
		}
	}

	js_copy(J, 0);
}

static void Ap_splice(js_State *J)
{
	unsigned int top = js_gettop(J);
	unsigned int len, start, del, add, k;
	double f;

	js_newarray(J);

	len = js_getlength(J, 0);

	f = js_tointeger(J, 1);
	if (f < 0) f = f + len;
	start = f < 0 ? 0 : f > len ? len : f;

	f = js_tointeger(J, 2);
	del = f < 0 ? 0 : f > len - start ? len - start : f;

	/* copy deleted items to return array */
	for (k = 0; k < del; ++k)
		if (js_hasindex(J, 0, start + k))
			js_setindex(J, -2, k);
	js_setlength(J, -1, del);

	/* shift the tail to resize the hole left by deleted items */
	add = top - 3;
	if (add < del) {
		for (k = start; k < len - del; ++k) {
			if (js_hasindex(J, 0, k + del))
				js_setindex(J, 0, k + add);
			else
				js_delindex(J, 0, k + add);
		}
		for (k = len; k > len - del + add; --k)
			js_delindex(J, 0, k - 1);
	} else if (add > del) {
		for (k = len - del; k > start; --k) {
			if (js_hasindex(J, 0, k + del - 1))
				js_setindex(J, 0, k + add - 1);
			else
				js_delindex(J, 0, k + add - 1);
		}
	}

	/* copy new items into the hole */
	for (k = 0; k < add; ++k) {
		js_copy(J, 3 + k);
		js_setindex(J, 0, start + k);
	}

	js_setlength(J, 0, len - del + add);
}

static void Ap_unshift(js_State *J)
{
	unsigned int i, top = js_gettop(J);
	unsigned int k, len;

	len = js_getlength(J, 0);

	for (k = len; k > 0; --k) {
		int from = k - 1;
		int to = k + top - 2;
		if (js_hasindex(J, 0, from))
			js_setindex(J, 0, to);
		else
			js_delindex(J, 0, to);
	}

	for (i = 1; i < top; ++i) {
		js_copy(J, i);
		js_setindex(J, 0, i - 1);
	}

	js_setlength(J, 0, len + top - 1);

	js_pushnumber(J, len + top - 1);
}

static void Ap_toString(js_State *J)
{
	unsigned int top = js_gettop(J);
	js_pop(J, top - 1);
	Ap_join(J);
}

static void Ap_indexOf(js_State *J)
{
	int k, len, from;

	len = js_getlength(J, 0);
	from = js_isdefined(J, 2) ? js_tointeger(J, 2) : 0;
	if (from < 0) from = len + from;
	if (from < 0) from = 0;

	js_copy(J, 1);
	for (k = from; k < len; ++k) {
		if (js_hasindex(J, 0, k)) {
			if (js_strictequal(J)) {
				js_pushnumber(J, k);
				return;
			}
			js_pop(J, 1);
		}
	}

	js_pushnumber(J, -1);
}

static void Ap_lastIndexOf(js_State *J)
{
	int k, len, from;

	len = js_getlength(J, 0);
	from = js_isdefined(J, 2) ? js_tointeger(J, 2) : len - 1;
	if (from > len - 1) from = len - 1;
	if (from < 0) from = len + from;

	js_copy(J, 1);
	for (k = from; k >= 0; --k) {
		if (js_hasindex(J, 0, k)) {
			if (js_strictequal(J)) {
				js_pushnumber(J, k);
				return;
			}
			js_pop(J, 1);
		}
	}

	js_pushnumber(J, -1);
}

static void Ap_every(js_State *J)
{
	int hasthis = js_gettop(J) >= 3;
	int k, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	len = js_getlength(J, 0);
	for (k = 0; k < len; ++k) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			if (hasthis)
				js_copy(J, 2);
			else
				js_pushundefined(J);
			js_copy(J, -3);
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 3);
			if (!js_toboolean(J, -1))
				return;
			js_pop(J, 2);
		}
	}

	js_pushboolean(J, 1);
}

static void Ap_some(js_State *J)
{
	int hasthis = js_gettop(J) >= 3;
	int k, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	len = js_getlength(J, 0);
	for (k = 0; k < len; ++k) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			if (hasthis)
				js_copy(J, 2);
			else
				js_pushundefined(J);
			js_copy(J, -3);
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 3);
			if (js_toboolean(J, -1))
				return;
			js_pop(J, 2);
		}
	}

	js_pushboolean(J, 0);
}

static void Ap_forEach(js_State *J)
{
	int hasthis = js_gettop(J) >= 3;
	int k, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	len = js_getlength(J, 0);
	for (k = 0; k < len; ++k) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			if (hasthis)
				js_copy(J, 2);
			else
				js_pushundefined(J);
			js_copy(J, -3);
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 3);
			js_pop(J, 2);
		}
	}

	js_pushundefined(J);
}

static void Ap_map(js_State *J)
{
	int hasthis = js_gettop(J) >= 3;
	int k, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	js_newarray(J);

	len = js_getlength(J, 0);
	for (k = 0; k < len; ++k) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			if (hasthis)
				js_copy(J, 2);
			else
				js_pushundefined(J);
			js_copy(J, -3);
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 3);
			js_setindex(J, -3, k);
			js_pop(J, 1);
		}
	}
}

static void Ap_filter(js_State *J)
{
	int hasthis = js_gettop(J) >= 3;
	int k, to, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	js_newarray(J);
	to = 0;

	len = js_getlength(J, 0);
	for (k = 0; k < len; ++k) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			if (hasthis)
				js_copy(J, 2);
			else
				js_pushundefined(J);
			js_copy(J, -3);
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 3);
			if (js_toboolean(J, -1)) {
				js_pop(J, 1);
				js_setindex(J, -2, to++);
			} else {
				js_pop(J, 2);
			}
		}
	}
}

static void Ap_reduce(js_State *J)
{
	int hasinitial = js_gettop(J) >= 3;
	int k, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	len = js_getlength(J, 0);
	k = 0;

	if (len == 0 && !hasinitial)
		js_typeerror(J, "no initial value");

	/* initial value of accumulator */
	if (hasinitial)
		js_copy(J, 2);
	else {
		while (k < len)
			if (js_hasindex(J, 0, k++))
				break;
		if (k == len)
			js_typeerror(J, "no initial value");
	}

	while (k < len) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			js_pushundefined(J);
			js_rot(J, 4); /* accumulator on top */
			js_rot(J, 4); /* property on top */
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 4); /* calculate new accumulator */
		}
		++k;
	}

	/* return accumulator */
}

static void Ap_reduceRight(js_State *J)
{
	int hasinitial = js_gettop(J) >= 3;
	int k, len;

	if (!js_iscallable(J, 1))
		js_typeerror(J, "callback is not a function");

	len = js_getlength(J, 0);
	k = len - 1;

	if (len == 0 && !hasinitial)
		js_typeerror(J, "no initial value");

	/* initial value of accumulator */
	if (hasinitial)
		js_copy(J, 2);
	else {
		while (k >= 0)
			if (js_hasindex(J, 0, k--))
				break;
		if (k < 0)
			js_typeerror(J, "no initial value");
	}

	while (k >= 0) {
		if (js_hasindex(J, 0, k)) {
			js_copy(J, 1);
			js_pushundefined(J);
			js_rot(J, 4); /* accumulator on top */
			js_rot(J, 4); /* property on top */
			js_pushnumber(J, k);
			js_copy(J, 0);
			js_call(J, 4); /* calculate new accumulator */
		}
		--k;
	}

	/* return accumulator */
}

static void A_isArray(js_State *J)
{
	if (js_isobject(J, 1)) {
		js_Object *T = js_toobject(J, 1);
		js_pushboolean(J, T->type == JS_CARRAY);
	} else {
		js_pushboolean(J, 0);
	}
}

void jsB_initarray(js_State *J)
{
	js_pushobject(J, J->Array_prototype);
	{
		jsB_propf(J, "toString", Ap_toString, 0);
		jsB_propf(J, "concat", Ap_concat, 1);
		jsB_propf(J, "join", Ap_join, 1);
		jsB_propf(J, "pop", Ap_pop, 0);
		jsB_propf(J, "push", Ap_push, 1);
		jsB_propf(J, "reverse", Ap_reverse, 0);
		jsB_propf(J, "shift", Ap_shift, 0);
		jsB_propf(J, "slice", Ap_slice, 2);
		jsB_propf(J, "sort", Ap_sort, 1);
		jsB_propf(J, "splice", Ap_splice, 2);
		jsB_propf(J, "unshift", Ap_unshift, 1);

		/* ES5 */
		jsB_propf(J, "indexOf", Ap_indexOf, 1);
		jsB_propf(J, "lastIndexOf", Ap_lastIndexOf, 1);
		jsB_propf(J, "every", Ap_every, 1);
		jsB_propf(J, "some", Ap_some, 1);
		jsB_propf(J, "forEach", Ap_forEach, 1);
		jsB_propf(J, "map", Ap_map, 1);
		jsB_propf(J, "filter", Ap_filter, 1);
		jsB_propf(J, "reduce", Ap_reduce, 1);
		jsB_propf(J, "reduceRight", Ap_reduceRight, 1);
	}
	js_newcconstructor(J, jsB_new_Array, jsB_new_Array, "Array", 1);
	{
		/* ES5 */
		jsB_propf(J, "isArray", A_isArray, 1);
	}
	js_defglobal(J, "Array", JS_DONTENUM);
}
