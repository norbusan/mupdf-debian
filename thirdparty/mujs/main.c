#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mujs.h"

#define PS1 "> "

static void jsB_gc(js_State *J)
{
	int report = js_toboolean(J, 1);
	js_gc(J, report);
	js_pushundefined(J);
}

static void jsB_load(js_State *J)
{
	const char *filename = js_tostring(J, 1);
	int rv = js_dofile(J, filename);
	js_pushboolean(J, !rv);
}

static void jsB_print(js_State *J)
{
	int i, top = js_gettop(J);
	for (i = 1; i < top; ++i) {
		const char *s = js_tostring(J, i);
		if (i > 1) putchar(' ');
		fputs(s, stdout);
	}
	putchar('\n');
	js_pushundefined(J);
}

static void jsB_write(js_State *J)
{
	int i, top = js_gettop(J);
	for (i = 1; i < top; ++i) {
		const char *s = js_tostring(J, i);
		if (i > 1) putchar(' ');
		fputs(s, stdout);
	}
	js_pushundefined(J);
}

static void jsB_read(js_State *J)
{
	const char *filename = js_tostring(J, 1);
	FILE *f;
	char *s;
	int n, t;

	f = fopen(filename, "rb");
	if (!f) {
		js_error(J, "cannot open file: '%s'", filename);
	}

	if (fseek(f, 0, SEEK_END) < 0) {
		fclose(f);
		js_error(J, "cannot seek in file: '%s'", filename);
	}

	n = ftell(f);
	if (n < 0) {
		fclose(f);
		js_error(J, "cannot tell in file: '%s'", filename);
	}

	if (fseek(f, 0, SEEK_SET) < 0) {
		fclose(f);
		js_error(J, "cannot seek in file: '%s'", filename);
	}

	s = malloc(n + 1);
	if (!s) {
		fclose(f);
		js_error(J, "cannot allocate storage for file contents: '%s'", filename);
	}

	t = fread(s, 1, n, f);
	if (t != n) {
		free(s);
		fclose(f);
		js_error(J, "cannot read data from file: '%s'", filename);
	}
	s[n] = 0;

	js_pushstring(J, s);
	free(s);
	fclose(f);
}

static void jsB_readline(js_State *J)
{
	char line[256];
	int n;
	if (!fgets(line, sizeof line, stdin))
		js_error(J, "cannot read line from stdin");
	n = strlen(line);
	if (n > 0 && line[n-1] == '\n')
		line[n-1] = 0;
	js_pushstring(J, line);
}

static void jsB_quit(js_State *J)
{
	exit(js_tonumber(J, 1));
}

static const char *require_js =
	"function require(name) {\n"
	"var cache = require.cache;\n"
	"if (name in cache) return cache[name];\n"
	"var exports = {};\n"
	"cache[name] = exports;\n"
	"Function('exports', read(name+'.js'))(exports);\n"
	"return exports;\n"
	"}\n"
	"require.cache = Object.create(null);\n"
;

static const char *stacktrace_js =
	"Error.prototype.toString = function() {\n"
	"if (this.stackTrace) return this.name + ': ' + this.message + this.stackTrace;\n"
	"return this.name + ': ' + this.message;\n"
	"};\n"
;

static int eval_print(js_State *J, const char *source)
{
	if (js_ploadstring(J, "[string]", source)) {
		fprintf(stderr, "%s\n", js_trystring(J, -1, "Error"));
		js_pop(J, 1);
		return 1;
	}
	js_pushundefined(J);
	if (js_pcall(J, 0)) {
		fprintf(stderr, "%s\n", js_trystring(J, -1, "Error"));
		js_pop(J, 1);
		return 1;
	}
	if (js_isdefined(J, -1))
		printf("%s\n", js_trystring(J, -1, "can't convert to string"));
	js_pop(J, 1);
	return 0;
}

static char *read_stdin(void)
{
	int n = 0;
	int t = 512;
	char *s = NULL;

	for (;;) {
		char *ss = realloc(s, t);
		if (!ss) {
			free(s);
			fprintf(stderr, "cannot allocate storage for stdin contents\n");
			return NULL;
		}
		s = ss;
		n += fread(s + n, 1, t - n - 1, stdin);
		if (n < t - 1)
			break;
		t *= 2;
	}

	if (ferror(stdin)) {
		free(s);
		fprintf(stderr, "error reading stdin\n");
		return NULL;
	}

	s[n] = 0;
	return s;
}

int
main(int argc, char **argv)
{
	char line[256];
	js_State *J;
	int i, status = 0;

	J = js_newstate(NULL, NULL, JS_STRICT);

	js_newcfunction(J, jsB_gc, "gc", 0);
	js_setglobal(J, "gc");

	js_newcfunction(J, jsB_load, "load", 1);
	js_setglobal(J, "load");

	js_newcfunction(J, jsB_print, "print", 0);
	js_setglobal(J, "print");

	js_newcfunction(J, jsB_write, "write", 0);
	js_setglobal(J, "write");

	js_newcfunction(J, jsB_read, "read", 1);
	js_setglobal(J, "read");

	js_newcfunction(J, jsB_readline, "readline", 0);
	js_setglobal(J, "readline");

	js_newcfunction(J, jsB_quit, "quit", 1);
	js_setglobal(J, "quit");

	js_dostring(J, require_js);
	js_dostring(J, stacktrace_js);

	if (argc > 1) {
		for (i = 1; i < argc; ++i)
			if (js_dofile(J, argv[i]))
				status = 1;
	} else {
		if (isatty(0)) {
			fputs(PS1, stdout);
			while (fgets(line, sizeof line, stdin)) {
				eval_print(J, line);
				fputs(PS1, stdout);
			}
			putchar('\n');
		} else {
			char *input = read_stdin();
			if (!input || !js_dostring(J, input))
				status = 1;
			free(input);
		}
	}

	js_gc(J, 0);
	js_freestate(J);

	return status;
}
