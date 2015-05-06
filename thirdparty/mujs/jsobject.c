#include "jsi.h"
#include "jsvalue.h"
#include "jsbuiltin.h"

static void jsB_new_Object(js_State *J)
{
	if (js_gettop(J) == 1 || js_isundefined(J, 1) || js_isnull(J, 1))
		js_newobject(J);
	else
		js_pushobject(J, js_toobject(J, 1));
}

static void jsB_Object(js_State *J)
{
	if (js_gettop(J) == 1 || js_isundefined(J, 1) || js_isnull(J, 1))
		js_newobject(J);
	else
		js_pushobject(J, js_toobject(J, 1));
}

static void Op_toString(js_State *J)
{
	js_Object *self = js_toobject(J, 0);
	switch (self->type) {
	case JS_COBJECT: js_pushliteral(J, "[object Object]"); break;
	case JS_CARRAY: js_pushliteral(J, "[object Array]"); break;
	case JS_CFUNCTION: js_pushliteral(J, "[object Function]"); break;
	case JS_CSCRIPT: js_pushliteral(J, "[object Function]"); break;
	case JS_CCFUNCTION: js_pushliteral(J, "[object Function]"); break;
	case JS_CERROR: js_pushliteral(J, "[object Error]"); break;
	case JS_CBOOLEAN: js_pushliteral(J, "[object Boolean]"); break;
	case JS_CNUMBER: js_pushliteral(J, "[object Number]"); break;
	case JS_CSTRING: js_pushliteral(J, "[object String]"); break;
	case JS_CREGEXP: js_pushliteral(J, "[object RegExp]"); break;
	case JS_CDATE: js_pushliteral(J, "[object Date]"); break;
	case JS_CMATH: js_pushliteral(J, "[object Math]"); break;
	case JS_CJSON: js_pushliteral(J, "[object JSON]"); break;
	case JS_CITERATOR: js_pushliteral(J, "[Iterator]"); break;
	case JS_CUSERDATA:
		js_pushliteral(J, "[object ");
		js_pushliteral(J, self->u.user.tag);
		js_concat(J);
		js_pushliteral(J, "]");
		js_concat(J);
		break;
	}
}

static void Op_valueOf(js_State *J)
{
	js_copy(J, 0);
}

static void Op_hasOwnProperty(js_State *J)
{
	js_Object *self = js_toobject(J, 0);
	const char *name = js_tostring(J, 1);
	js_Property *ref = jsV_getownproperty(J, self, name);
	js_pushboolean(J, ref != NULL);
}

static void Op_isPrototypeOf(js_State *J)
{
	js_Object *self = js_toobject(J, 0);
	if (js_isobject(J, 1)) {
		js_Object *V = js_toobject(J, 1);
		do {
			V = V->prototype;
			if (V == self) {
				js_pushboolean(J, 1);
				return;
			}
		} while (V);
	}
	js_pushboolean(J, 0);
}

static void Op_propertyIsEnumerable(js_State *J)
{
	js_Object *self = js_toobject(J, 0);
	const char *name = js_tostring(J, 1);
	js_Property *ref = jsV_getownproperty(J, self, name);
	js_pushboolean(J, ref && !(ref->atts & JS_DONTENUM));
}

static void O_getPrototypeOf(js_State *J)
{
	js_Object *obj;
	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");
	obj = js_toobject(J, 1);
	if (obj->prototype)
		js_pushobject(J, obj->prototype);
	else
		js_pushnull(J);
}

static void O_getOwnPropertyDescriptor(js_State *J)
{
	js_Object *obj;
	js_Property *ref;
	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");
	obj = js_toobject(J, 1);
	ref = jsV_getproperty(J, obj, js_tostring(J, 2));
	if (!ref)
		js_pushundefined(J);
	else {
		js_newobject(J);
		if (!ref->getter && !ref->setter) {
			js_pushvalue(J, ref->value);
			js_setproperty(J, -2, "value");
			js_pushboolean(J, !(ref->atts & JS_READONLY));
			js_setproperty(J, -2, "writable");
		} else {
			if (ref->getter)
				js_pushobject(J, ref->getter);
			else
				js_pushundefined(J);
			js_setproperty(J, -2, "get");
			if (ref->setter)
				js_pushobject(J, ref->setter);
			else
				js_pushundefined(J);
			js_setproperty(J, -2, "set");
		}
		js_pushboolean(J, !(ref->atts & JS_DONTENUM));
		js_setproperty(J, -2, "enumerable");
		js_pushboolean(J, !(ref->atts & JS_DONTCONF));
		js_setproperty(J, -2, "configurable");
	}
}

static void O_getOwnPropertyNames(js_State *J)
{
	js_Object *obj;
	js_Property *ref;
	unsigned int k;
	int i;

	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");
	obj = js_toobject(J, 1);

	js_newarray(J);

	i = 0;
	for (ref = obj->head; ref; ref = ref->next) {
		js_pushliteral(J, ref->name);
		js_setindex(J, -2, i++);
	}

	if (obj->type == JS_CARRAY) {
		js_pushliteral(J, "length");
		js_setindex(J, -2, i++);
	}

	if (obj->type == JS_CSTRING) {
		js_pushliteral(J, "length");
		js_setindex(J, -2, i++);
		for (k = 0; k < obj->u.s.length; ++k) {
			js_pushnumber(J, k);
			js_setindex(J, -2, i++);
		}
	}

	if (obj->type == JS_CREGEXP) {
		js_pushliteral(J, "source");
		js_setindex(J, -2, i++);
		js_pushliteral(J, "global");
		js_setindex(J, -2, i++);
		js_pushliteral(J, "ignoreCase");
		js_setindex(J, -2, i++);
		js_pushliteral(J, "multiline");
		js_setindex(J, -2, i++);
		js_pushliteral(J, "lastIndex");
		js_setindex(J, -2, i++);
	}
}

static void ToPropertyDescriptor(js_State *J, js_Object *obj, const char *name, js_Object *desc)
{
	int haswritable = 0;
	int hasvalue = 0;
	int enumerable = 0;
	int configurable = 0;
	int writable = 0;
	int atts = 0;

	js_pushobject(J, obj);
	js_pushobject(J, desc);

	if (js_hasproperty(J, -1, "writable")) {
		haswritable = 1;
		writable = js_toboolean(J, -1);
		js_pop(J, 1);
	}
	if (js_hasproperty(J, -1, "enumerable")) {
		enumerable = js_toboolean(J, -1);
		js_pop(J, 1);
	}
	if (js_hasproperty(J, -1, "configurable")) {
		configurable = js_toboolean(J, -1);
		js_pop(J, 1);
	}
	if (js_hasproperty(J, -1, "value")) {
		hasvalue = 1;
		js_setproperty(J, -3, name);
	}

	if (!writable) atts |= JS_READONLY;
	if (!enumerable) atts |= JS_DONTENUM;
	if (!configurable) atts |= JS_DONTCONF;

	if (js_hasproperty(J, -1, "get")) {
		if (haswritable || hasvalue)
			js_typeerror(J, "value/writable and get/set attributes are exclusive");
	} else {
		js_pushundefined(J);
	}

	if (js_hasproperty(J, -2, "set")) {
		if (haswritable || hasvalue)
			js_typeerror(J, "value/writable and get/set attributes are exclusive");
	} else {
		js_pushundefined(J);
	}

	js_defaccessor(J, -4, name, atts);

	js_pop(J, 2);
}

static void O_defineProperty(js_State *J)
{
	if (!js_isobject(J, 1)) js_typeerror(J, "not an object");
	if (!js_isobject(J, 3)) js_typeerror(J, "not an object");
	ToPropertyDescriptor(J, js_toobject(J, 1), js_tostring(J, 2), js_toobject(J, 3));
	js_copy(J, 1);
}

static void O_defineProperties(js_State *J)
{
	js_Object *props;
	js_Property *ref;

	if (!js_isobject(J, 1)) js_typeerror(J, "not an object");
	if (!js_isobject(J, 2)) js_typeerror(J, "not an object");

	props = js_toobject(J, 2);
	for (ref = props->head; ref; ref = ref->next) {
		if (!(ref->atts & JS_DONTENUM)) {
			js_pushvalue(J, ref->value);
			ToPropertyDescriptor(J, js_toobject(J, 1), ref->name, js_toobject(J, -1));
			js_pop(J, 1);
		}
	}

	js_copy(J, 1);
}

static void O_create(js_State *J)
{
	js_Object *obj;
	js_Object *proto;
	js_Object *props;
	js_Property *ref;

	if (js_isobject(J, 1))
		proto = js_toobject(J, 1);
	else if (js_isnull(J, 1))
		proto = NULL;
	else
		js_typeerror(J, "not an object or null");

	obj = jsV_newobject(J, JS_COBJECT, proto);
	js_pushobject(J, obj);

	if (js_isdefined(J, 2)) {
		if (!js_isobject(J, 2)) js_typeerror(J, "not an object");
		props = js_toobject(J, 2);
		for (ref = props->head; ref; ref = ref->next) {
			if (!(ref->atts & JS_DONTENUM)) {
				if (ref->value.type != JS_TOBJECT) js_typeerror(J, "not an object");
				ToPropertyDescriptor(J, obj, ref->name, ref->value.u.object);
			}
		}
	}
}

static void O_keys(js_State *J)
{
	js_Object *obj;
	js_Property *ref;
	unsigned int k;
	int i;

	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");
	obj = js_toobject(J, 1);

	js_newarray(J);

	i = 0;
	for (ref = obj->head; ref; ref = ref->next) {
		if (!(ref->atts & JS_DONTENUM)) {
			js_pushliteral(J, ref->name);
			js_setindex(J, -2, i++);
		}
	}

	if (obj->type == JS_CSTRING) {
		for (k = 0; k < obj->u.s.length; ++k) {
			js_pushnumber(J, k);
			js_setindex(J, -2, i++);
		}
	}
}

static void O_preventExtensions(js_State *J)
{
	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");
	js_toobject(J, 1)->extensible = 0;
	js_copy(J, 1);
}

static void O_isExtensible(js_State *J)
{
	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");
	js_pushboolean(J, js_toobject(J, 1)->extensible);
}

static void O_seal(js_State *J)
{
	js_Object *obj;
	js_Property *ref;

	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");

	obj = js_toobject(J, 1);
	obj->extensible = 0;

	for (ref = obj->head; ref; ref = ref->next)
		ref->atts |= JS_DONTCONF;

	js_copy(J, 1);
}

static void O_isSealed(js_State *J)
{
	js_Object *obj;
	js_Property *ref;

	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");

	obj = js_toobject(J, 1);
	if (obj->extensible) {
		js_pushboolean(J, 0);
		return;
	}

	for (ref = obj->head; ref; ref = ref->next) {
		if (!(ref->atts & JS_DONTCONF)) {
			js_pushboolean(J, 0);
			return;
		}
	}

	js_pushboolean(J, 1);
}

static void O_freeze(js_State *J)
{
	js_Object *obj;
	js_Property *ref;

	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");

	obj = js_toobject(J, 1);
	obj->extensible = 0;

	for (ref = obj->head; ref; ref = ref->next)
		ref->atts |= JS_READONLY | JS_DONTCONF;

	js_copy(J, 1);
}

static void O_isFrozen(js_State *J)
{
	js_Object *obj;
	js_Property *ref;

	if (!js_isobject(J, 1))
		js_typeerror(J, "not an object");

	obj = js_toobject(J, 1);
	if (obj->extensible) {
		js_pushboolean(J, 0);
		return;
	}

	for (ref = obj->head; ref; ref = ref->next) {
		if (!(ref->atts & (JS_READONLY | JS_DONTCONF))) {
			js_pushboolean(J, 0);
			return;
		}
	}

	js_pushboolean(J, 1);
}

void jsB_initobject(js_State *J)
{
	js_pushobject(J, J->Object_prototype);
	{
		jsB_propf(J, "toString", Op_toString, 0);
		jsB_propf(J, "toLocaleString", Op_toString, 0);
		jsB_propf(J, "valueOf", Op_valueOf, 0);
		jsB_propf(J, "hasOwnProperty", Op_hasOwnProperty, 1);
		jsB_propf(J, "isPrototypeOf", Op_isPrototypeOf, 1);
		jsB_propf(J, "propertyIsEnumerable", Op_propertyIsEnumerable, 1);
	}
	js_newcconstructor(J, jsB_Object, jsB_new_Object, "Object", 1);
	{
		/* ES5 */
		jsB_propf(J, "getPrototypeOf", O_getPrototypeOf, 1);
		jsB_propf(J, "getOwnPropertyDescriptor", O_getOwnPropertyDescriptor, 2);
		jsB_propf(J, "getOwnPropertyNames", O_getOwnPropertyNames, 1);
		jsB_propf(J, "create", O_create, 2);
		jsB_propf(J, "defineProperty", O_defineProperty, 3);
		jsB_propf(J, "defineProperties", O_defineProperties, 2);
		jsB_propf(J, "seal", O_seal, 1);
		jsB_propf(J, "freeze", O_freeze, 1);
		jsB_propf(J, "preventExtensions", O_preventExtensions, 1);
		jsB_propf(J, "isSealed", O_isSealed, 1);
		jsB_propf(J, "isFrozen", O_isFrozen, 1);
		jsB_propf(J, "isExtensible", O_isExtensible, 1);
		jsB_propf(J, "keys", O_keys, 1);
	}
	js_defglobal(J, "Object", JS_DONTENUM);
}
