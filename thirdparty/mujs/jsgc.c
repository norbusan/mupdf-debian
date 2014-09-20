#include "jsi.h"
#include "jscompile.h"
#include "jsvalue.h"
#include "jsrun.h"

#include "regex.h"

static void jsG_markobject(js_State *J, int mark, js_Object *obj);

static void jsG_freeenvironment(js_State *J, js_Environment *env)
{
	js_free(J, env);
}

static void jsG_freefunction(js_State *J, js_Function *fun)
{
	js_free(J, fun->funtab);
	js_free(J, fun->numtab);
	js_free(J, fun->strtab);
	js_free(J, fun->vartab);
	js_free(J, fun->code);
	js_free(J, fun);
}

static void jsG_freeproperty(js_State *J, js_Property *node)
{
	while (node) {
		js_Property *next = node->next;
		js_free(J, node);
		node = next;
	}
}

static void jsG_freeiterator(js_State *J, js_Iterator *node)
{
	while (node) {
		js_Iterator *next = node->next;
		js_free(J, node);
		node = next;
	}
}

static void jsG_freeobject(js_State *J, js_Object *obj)
{
	if (obj->head)
		jsG_freeproperty(J, obj->head);
	if (obj->type == JS_CREGEXP)
		js_regfree(obj->u.r.prog);
	if (obj->type == JS_CITERATOR)
		jsG_freeiterator(J, obj->u.iter.head);
	js_free(J, obj);
}

static void jsG_markfunction(js_State *J, int mark, js_Function *fun)
{
	unsigned int i;
	fun->gcmark = mark;
	for (i = 0; i < fun->funlen; ++i)
		if (fun->funtab[i]->gcmark != mark)
			jsG_markfunction(J, mark, fun->funtab[i]);
}

static void jsG_markenvironment(js_State *J, int mark, js_Environment *env)
{
	do {
		env->gcmark = mark;
		if (env->variables->gcmark != mark)
			jsG_markobject(J, mark, env->variables);
		env = env->outer;
	} while (env && env->gcmark != mark);
}

static void jsG_markproperty(js_State *J, int mark, js_Property *node)
{
	while (node) {
		if (node->value.type == JS_TOBJECT && node->value.u.object->gcmark != mark)
			jsG_markobject(J, mark, node->value.u.object);
		if (node->getter && node->getter->gcmark != mark)
			jsG_markobject(J, mark, node->getter);
		if (node->setter && node->setter->gcmark != mark)
			jsG_markobject(J, mark, node->setter);
		node = node->next;
	}
}

static void jsG_markobject(js_State *J, int mark, js_Object *obj)
{
	obj->gcmark = mark;
	if (obj->head)
		jsG_markproperty(J, mark, obj->head);
	if (obj->prototype && obj->prototype->gcmark != mark)
		jsG_markobject(J, mark, obj->prototype);
	if (obj->type == JS_CITERATOR) {
		jsG_markobject(J, mark, obj->u.iter.target);
	}
	if (obj->type == JS_CFUNCTION || obj->type == JS_CSCRIPT) {
		if (obj->u.f.scope && obj->u.f.scope->gcmark != mark)
			jsG_markenvironment(J, mark, obj->u.f.scope);
		if (obj->u.f.function && obj->u.f.function->gcmark != mark)
			jsG_markfunction(J, mark, obj->u.f.function);
	}
}

static void jsG_markstack(js_State *J, int mark)
{
	js_Value *v = J->stack;
	int n = J->top;
	while (n--) {
		if (v->type == JS_TOBJECT && v->u.object->gcmark != mark)
			jsG_markobject(J, mark, v->u.object);
		++v;
	}
}

void js_gc(js_State *J, int report)
{
	js_Function *fun, *nextfun, **prevnextfun;
	js_Object *obj, *nextobj, **prevnextobj;
	js_Environment *env, *nextenv, **prevnextenv;
	int nenv = 0, nfun = 0, nobj = 0;
	int genv = 0, gfun = 0, gobj = 0;
	int mark;

	mark = J->gcmark = J->gcmark == 1 ? 2 : 1;

	jsG_markobject(J, mark, J->Object_prototype);
	jsG_markobject(J, mark, J->Array_prototype);
	jsG_markobject(J, mark, J->Function_prototype);
	jsG_markobject(J, mark, J->Boolean_prototype);
	jsG_markobject(J, mark, J->Number_prototype);
	jsG_markobject(J, mark, J->String_prototype);

	jsG_markobject(J, mark, J->Error_prototype);
	jsG_markobject(J, mark, J->EvalError_prototype);
	jsG_markobject(J, mark, J->RangeError_prototype);
	jsG_markobject(J, mark, J->ReferenceError_prototype);
	jsG_markobject(J, mark, J->SyntaxError_prototype);
	jsG_markobject(J, mark, J->TypeError_prototype);
	jsG_markobject(J, mark, J->URIError_prototype);

	jsG_markobject(J, mark, J->R);
	jsG_markobject(J, mark, J->G);

	jsG_markstack(J, mark);
	jsG_markenvironment(J, mark, J->E);

	prevnextenv = &J->gcenv;
	for (env = J->gcenv; env; env = nextenv) {
		nextenv = env->gcnext;
		if (env->gcmark != mark) {
			*prevnextenv = nextenv;
			jsG_freeenvironment(J, env);
			++genv;
		} else {
			prevnextenv = &env->gcnext;
		}
		++nenv;
	}

	prevnextfun = &J->gcfun;
	for (fun = J->gcfun; fun; fun = nextfun) {
		nextfun = fun->gcnext;
		if (fun->gcmark != mark) {
			*prevnextfun = nextfun;
			jsG_freefunction(J, fun);
			++gfun;
		} else {
			prevnextfun = &fun->gcnext;
		}
		++nfun;
	}

	prevnextobj = &J->gcobj;
	for (obj = J->gcobj; obj; obj = nextobj) {
		nextobj = obj->gcnext;
		if (obj->gcmark != mark) {
			*prevnextobj = nextobj;
			jsG_freeobject(J, obj);
			++gobj;
		} else {
			prevnextobj = &obj->gcnext;
		}
		++nobj;
	}

	if (report)
		printf("garbage collected: %d/%d envs, %d/%d funs, %d/%d objs\n",
			genv, nenv, gfun, nfun, gobj, nobj);
}

void js_freestate(js_State *J)
{
	js_Function *fun, *nextfun;
	js_Object *obj, *nextobj;
	js_Environment *env, *nextenv;

	for (env = J->gcenv; env; env = nextenv)
		nextenv = env->gcnext, jsG_freeenvironment(J, env);
	for (fun = J->gcfun; fun; fun = nextfun)
		nextfun = fun->gcnext, jsG_freefunction(J, fun);
	for (obj = J->gcobj; obj; obj = nextobj)
		nextobj = obj->gcnext, jsG_freeobject(J, obj);

	jsS_freestrings(J);

	js_free(J, J->lexbuf.text);
	J->alloc(J->actx, J->stack, 0);
	J->alloc(J->actx, J, 0);
}
