#include "jsi.h"
#include "jsvalue.h"
#include "jsbuiltin.h"

#define QQ(X) #X
#define Q(X) QQ(X)

static void jsB_stacktrace(js_State *J, int skip)
{
	int n;
	char buf[256];
	for (n = J->tracetop - skip; n >= 0; --n) {
		const char *name = J->trace[n].name;
		const char *file = J->trace[n].file;
		int line = J->trace[n].line;
		if (line > 0)
			snprintf(buf, sizeof buf, "\n\t%s:%d: in function '%s'", file, line, name);
		else
			snprintf(buf, sizeof buf, "\n\t%s: in function '%s'", file, name);
		js_pushstring(J, buf);
		if (n < J->tracetop - skip)
			js_concat(J);
	}
}

static void Ep_toString(js_State *J)
{
	char buf[256];
	const char *name = "Error";
	const char *message = "";

	if (!js_isobject(J, -1))
		js_typeerror(J, "not an object");

	if (js_hasproperty(J, 0, "name"))
		name = js_tostring(J, -1);
	if (js_hasproperty(J, 0, "message"))
		message = js_tostring(J, -1);

	snprintf(buf, sizeof buf, "%s: %s", name, message);
	js_pushstring(J, buf);

	if (js_hasproperty(J, 0, "stackTrace"))
		js_concat(J);
}

static int jsB_ErrorX(js_State *J, js_Object *prototype)
{
	unsigned int top = js_gettop(J);
	js_pushobject(J, jsV_newobject(J, JS_CERROR, prototype));
	if (top > 1) {
		js_pushstring(J, js_tostring(J, 1));
		js_setproperty(J, -2, "message");
	}
	jsB_stacktrace(J, 1);
	js_setproperty(J, -2, "stackTrace");
	return 1;
}

static void js_newerrorx(js_State *J, const char *message, js_Object *prototype)
{
	js_pushobject(J, jsV_newobject(J, JS_CERROR, prototype));
	js_pushstring(J, message);
	js_setproperty(J, -2, "message");
	jsB_stacktrace(J, 0);
	js_setproperty(J, -2, "stackTrace");
}

#define DERROR(name, Name) \
	static void jsB_##Name(js_State *J) { \
		jsB_ErrorX(J, J->Name##_prototype); \
	} \
	void js_new##name(js_State *J, const char *s) { \
		js_newerrorx(J, s, J->Name##_prototype); \
	} \
	void js_##name(js_State *J, const char *fmt, ...) { \
		va_list ap; \
		char buf[256]; \
		va_start(ap, fmt); \
		vsnprintf(buf, sizeof buf, fmt, ap); \
		va_end(ap); \
		js_newerrorx(J, buf, J->Name##_prototype); \
		js_throw(J); \
	}

DERROR(error, Error)
DERROR(evalerror, EvalError)
DERROR(rangeerror, RangeError)
DERROR(referenceerror, ReferenceError)
DERROR(syntaxerror, SyntaxError)
DERROR(typeerror, TypeError)
DERROR(urierror, URIError)

#undef DERROR

void jsB_initerror(js_State *J)
{
	js_pushobject(J, J->Error_prototype);
	{
			jsB_props(J, "name", "Error");
			jsB_props(J, "message", "an error has occurred");
			jsB_propf(J, "toString", Ep_toString, 0);
	}
	js_newcconstructor(J, jsB_Error, jsB_Error, "Error", 1);
	js_defglobal(J, "Error", JS_DONTENUM);

	#define IERROR(NAME) \
		js_pushobject(J, J->NAME##_prototype); \
		jsB_props(J, "name", Q(NAME)); \
		js_newcconstructor(J, jsB_##NAME, jsB_##NAME, Q(NAME), 1); \
		js_defglobal(J, Q(NAME), JS_DONTENUM);

	IERROR(EvalError);
	IERROR(RangeError);
	IERROR(ReferenceError);
	IERROR(SyntaxError);
	IERROR(TypeError);
	IERROR(URIError);

	#undef IERROR
}
