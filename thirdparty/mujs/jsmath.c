#include "jsi.h"
#include "jsvalue.h"
#include "jsbuiltin.h"

static void Math_abs(js_State *J)
{
	js_pushnumber(J, fabs(js_tonumber(J, 1)));
}

static void Math_acos(js_State *J)
{
	js_pushnumber(J, acos(js_tonumber(J, 1)));
}

static void Math_asin(js_State *J)
{
	js_pushnumber(J, asin(js_tonumber(J, 1)));
}

static void Math_atan(js_State *J)
{
	js_pushnumber(J, atan(js_tonumber(J, 1)));
}

static void Math_atan2(js_State *J)
{
	double y = js_tonumber(J, 1);
	double x = js_tonumber(J, 2);
	js_pushnumber(J, atan2(y, x));
}

static void Math_ceil(js_State *J)
{
	js_pushnumber(J, ceil(js_tonumber(J, 1)));
}

static void Math_cos(js_State *J)
{
	js_pushnumber(J, cos(js_tonumber(J, 1)));
}

static void Math_exp(js_State *J)
{
	js_pushnumber(J, exp(js_tonumber(J, 1)));
}

static void Math_floor(js_State *J)
{
	js_pushnumber(J, floor(js_tonumber(J, 1)));
}

static void Math_log(js_State *J)
{
	js_pushnumber(J, log(js_tonumber(J, 1)));
}

static void Math_pow(js_State *J)
{
	double x = js_tonumber(J, 1);
	double y = js_tonumber(J, 2);
	if (!isfinite(y) && fabs(x) == 1)
		js_pushnumber(J, NAN);
	else
		js_pushnumber(J, pow(x,y));
}

static void Math_random(js_State *J)
{
	js_pushnumber(J, rand() / (RAND_MAX + 1.0));
}

static void Math_round(js_State *J)
{
	double x = js_tonumber(J, 1);
	double r = round(x);
	if (r - x == -0.5)
		js_pushnumber(J, x == -0.5 ? -0.0 : r + 1.0);
	else
		js_pushnumber(J, r);
}

static void Math_sin(js_State *J)
{
	js_pushnumber(J, sin(js_tonumber(J, 1)));
}

static void Math_sqrt(js_State *J)
{
	js_pushnumber(J, sqrt(js_tonumber(J, 1)));
}

static void Math_tan(js_State *J)
{
	js_pushnumber(J, tan(js_tonumber(J, 1)));
}

static void Math_max(js_State *J)
{
	unsigned int i, n = js_gettop(J);
	double x = -INFINITY;
	for (i = 1; i < n; ++i) {
		double y = js_tonumber(J, i);
		if (isnan(y)) {
			x = y;
			break;
		}
		if (signbit(x) == signbit(y))
			x = x > y ? x : y;
		else if (signbit(x))
			x = y;
	}
	js_pushnumber(J, x);
}

static void Math_min(js_State *J)
{
	unsigned int i, n = js_gettop(J);
	double x = INFINITY;
	for (i = 1; i < n; ++i) {
		double y = js_tonumber(J, i);
		if (isnan(y)) {
			x = y;
			break;
		}
		if (signbit(x) == signbit(y))
			x = x < y ? x : y;
		else if (signbit(y))
			x = y;
	}
	js_pushnumber(J, x);
}

void jsB_initmath(js_State *J)
{
	js_pushobject(J, jsV_newobject(J, JS_CMATH, J->Object_prototype));
	{
		jsB_propn(J, "E", 2.7182818284590452354);
		jsB_propn(J, "LN10", 2.302585092994046);
		jsB_propn(J, "LN2", 0.6931471805599453);
		jsB_propn(J, "LOG2E", 1.4426950408889634);
		jsB_propn(J, "LOG10E", 0.4342944819032518);
		jsB_propn(J, "PI", 3.1415926535897932);
		jsB_propn(J, "SQRT1_2", 0.7071067811865476);
		jsB_propn(J, "SQRT2", 1.4142135623730951);

		jsB_propf(J, "abs", Math_abs, 1);
		jsB_propf(J, "acos", Math_acos, 1);
		jsB_propf(J, "asin", Math_asin, 1);
		jsB_propf(J, "atan", Math_atan, 1);
		jsB_propf(J, "atan2", Math_atan2, 2);
		jsB_propf(J, "ceil", Math_ceil, 1);
		jsB_propf(J, "cos", Math_cos, 1);
		jsB_propf(J, "exp", Math_exp, 1);
		jsB_propf(J, "floor", Math_floor, 1);
		jsB_propf(J, "log", Math_log, 1);
		jsB_propf(J, "max", Math_max, 0);
		jsB_propf(J, "min", Math_min, 0);
		jsB_propf(J, "pow", Math_pow, 2);
		jsB_propf(J, "random", Math_random, 0);
		jsB_propf(J, "round", Math_round, 1);
		jsB_propf(J, "sin", Math_sin, 1);
		jsB_propf(J, "sqrt", Math_sqrt, 1);
		jsB_propf(J, "tan", Math_tan, 1);
	}
	js_defglobal(J, "Math", JS_DONTENUM);
}
