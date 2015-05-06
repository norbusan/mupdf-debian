#ifndef jsi_h
#define jsi_h

#include "mujs.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <float.h>

/* Microsoft Visual C */
#ifdef _MSC_VER
#pragma warning(disable:4996) /* _CRT_SECURE_NO_WARNINGS */
#pragma warning(disable:4244) /* implicit conversion from double to int */
#pragma warning(disable:4267) /* implicit conversion of int to smaller int */
#define inline __inline
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#if _MSC_VER < 1800
#define round(x) floor((x) < 0 ? (x) - 0.5 : (x) + 0.5)
#define isnan(x) _isnan(x)
#define isinf(x) (!_finite(x))
#define isfinite(x) _finite(x)
static __inline int signbit(double x) {union{double d;__int64 i;}u;u.d=x;return u.i>>63;}
#define INFINITY (DBL_MAX+DBL_MAX)
#define NAN (INFINITY-INFINITY)
#endif /* old MSVC */
#endif

#define nelem(a) (sizeof (a) / sizeof (a)[0])

void *js_malloc(js_State *J, unsigned int size);
void *js_realloc(js_State *J, void *ptr, unsigned int size);
void js_free(js_State *J, void *ptr);

typedef struct js_Regexp js_Regexp;
typedef struct js_Value js_Value;
typedef struct js_Object js_Object;
typedef struct js_String js_String;
typedef struct js_Ast js_Ast;
typedef struct js_Function js_Function;
typedef struct js_Environment js_Environment;
typedef struct js_StringNode js_StringNode;
typedef struct js_Jumpbuf js_Jumpbuf;
typedef struct js_StackTrace js_StackTrace;

/* Limits */

#define JS_STACKSIZE 256	/* value stack size */
#define JS_ENVLIMIT 64		/* environment stack size */
#define JS_TRYLIMIT 64		/* exception stack size */
#define JS_GCLIMIT 10000	/* run gc cycle every N allocations */

/* instruction size -- change to unsigned int if you get integer overflow syntax errors */
typedef unsigned short js_Instruction;

/* String interning */

const char *js_intern(js_State *J, const char *s);
void jsS_dumpstrings(js_State *J);
void jsS_freestrings(js_State *J);

/* Portable strtod and printf float formatting */

void js_fmtexp(char *p, int e);
void js_dtoa(double f, char *digits, int *exp, int *neg, int *ndigits);
double js_strtod(const char *as, char **aas);

/* Private stack functions */

void js_newfunction(js_State *J, js_Function *function, js_Environment *scope);
void js_newscript(js_State *J, js_Function *function, js_Environment *scope);
void js_loadeval(js_State *J, const char *filename, const char *source);

js_Regexp *js_toregexp(js_State *J, int idx);
int js_isarrayindex(js_State *J, const char *str, unsigned int *idx);
int js_runeat(js_State *J, const char *s, int i);
int js_utfptrtoidx(const char *s, const char *p);
const char *js_utfidxtoptr(const char *s, int i);

void js_dup(js_State *J);
void js_dup2(js_State *J);
void js_rot2(js_State *J);
void js_rot3(js_State *J);
void js_rot4(js_State *J);
void js_rot2pop1(js_State *J);
void js_rot3pop2(js_State *J);
void js_dup1rot3(js_State *J);
void js_dup1rot4(js_State *J);

void js_pushundefinedthis(js_State *J); /* push 'global' if non-strict, undefined if strict */

void js_RegExp_prototype_exec(js_State *J, js_Regexp *re, const char *text);

void js_trap(js_State *J, int pc); /* dump stack and environment to stdout */

struct js_StackTrace
{
	const char *name;
	const char *file;
	int line;
};

/* Exception handling */

struct js_Jumpbuf
{
	jmp_buf buf;
	js_Environment *E;
	int envtop;
	int tracetop;
	int top, bot;
	js_Instruction *pc;
};

void js_savetry(js_State *J, js_Instruction *pc);

#define js_trypc(J, PC) \
	(js_savetry(J, PC), setjmp(J->trybuf[J->trytop++].buf))

#define js_try(J) \
	(js_savetry(J, NULL), setjmp(J->trybuf[J->trytop++].buf))

#define js_endtry(J) \
	(--J->trytop)

/* State struct */

struct js_State
{
	void *actx;
	void *uctx;
	js_Alloc alloc;
	js_Panic panic;

	js_StringNode *strings;

	int strict;

	/* parser input source */
	const char *filename;
	const char *source;
	int line;

	/* lexer state */
	struct { char *text; unsigned int len, cap; } lexbuf;
	int lexline;
	int lexchar;
	int lasttoken;
	int newline;

	/* parser state */
	int astline;
	int lookahead;
	const char *text;
	double number;
	js_Ast *gcast; /* list of allocated nodes to free after parsing */

	/* runtime environment */
	js_Object *Object_prototype;
	js_Object *Array_prototype;
	js_Object *Function_prototype;
	js_Object *Boolean_prototype;
	js_Object *Number_prototype;
	js_Object *String_prototype;
	js_Object *RegExp_prototype;
	js_Object *Date_prototype;

	js_Object *Error_prototype;
	js_Object *EvalError_prototype;
	js_Object *RangeError_prototype;
	js_Object *ReferenceError_prototype;
	js_Object *SyntaxError_prototype;
	js_Object *TypeError_prototype;
	js_Object *URIError_prototype;

	int nextref; /* for js_ref use */
	js_Object *R; /* registry of hidden values */
	js_Object *G; /* the global object */
	js_Environment *E; /* current environment scope */
	js_Environment *GE; /* global environment scope (at the root) */

	/* execution stack */
	int top, bot;
	js_Value *stack;

	/* garbage collector list */
	int gcmark;
	int gccounter;
	js_Environment *gcenv;
	js_Function *gcfun;
	js_Object *gcobj;
	js_String *gcstr;


	/* environments on the call stack but currently not in scope */
	int envtop;
	js_Environment *envstack[JS_ENVLIMIT];

	/* debug info stack trace */
	int tracetop;
	js_StackTrace trace[JS_ENVLIMIT];

	/* exception stack */
	int trytop;
	js_Jumpbuf trybuf[JS_TRYLIMIT];
};

#endif
