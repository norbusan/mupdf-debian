/*
run-time cpu feature detection code
mm, alphabet soup...

Glenn Kennard <d98gk@efd.lth.se>
*/

#include <fitz.h>

typedef struct {
	void (*test)(void);
	const unsigned flag;
	const char *name;
} featuretest;


#if defined(ARCH_X86) || defined(ARCH_X86_64)

/* need emms?? */
static void mmx(void)
{ __asm__ ("pand %mm0, %mm0\n\t"); }

static void m3dnow(void)
{ __asm__ ("pavgusb %mm0, %mm0\n\t"); }

static void mmxext(void) /* aka  Extended 3DNow! */
{ __asm__ ("pmaxsw %mm0, %mm0\n\t"); }

static void sse(void)
{ __asm__ ("andps %xmm0, %xmm0\n\t"); }

static void sse2(void)
{ __asm__ ("andpd %xmm0, %xmm0\n\t"); }

static void sse3(void)
{ __asm__ ("haddps %%xmm0, %%xmm0\n\t" : : : "%xmm0"); }

#ifdef ARCH_X86_64
static void amd64(void)
{ __asm__ ("and %rax, %rax\n\t"); }
#endif


static const featuretest features[] = {
	{ mmx, HAVE_MMX, "mmx" },
	{ m3dnow, HAVE_3DNOW, "3dnow" },
	{ mmxext, HAVE_MMXEXT, "mmxext" },
	{ sse, HAVE_SSE, "sse" },
	{ sse2, HAVE_SSE2, "sse2" },
	{ sse3, HAVE_SSE3, "sse3" },
#ifdef ARCH_X86_64
	{ amd64, HAVE_AMD64, "amd64" }
#endif
};

#endif


#if defined(ARCH_SPARC)
/* assembler must have -xarch=v8plusa passed to it (v9a for 64 bit binaries) */
static void vis(void)
{ __asm__ ("fand %f8, %f8, %f8\n\t"); }

static const featuretest features[] = {
	{ vis, HAVE_VIS, "vis" }
};

#endif


#if defined(ARCH_PPC)

static void altivec(void)
{ __asm__ ("vand v0, v0, v0\n\t"); }


static const featuretest features[] = {
	{ altivec, HAVE_ALTIVEC, "altivec" },
};

#endif

#ifndef HAVE_CPUDEP
static const featuretest features[] = {};
#endif

#include <signal.h> /* signal/sigaction */
#include <setjmp.h> /* sigsetjmp/siglongjmp */

/* global run-time constant */
unsigned fz_cpuflags = 0;

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump;

static void
sigillhandler(int sig)
{
	if (!canjump) {
		signal(sig, SIG_DFL);
		raise(sig);
	}

	canjump = 0;
	siglongjmp(jmpbuf, 1);
}

static void
dumpflags(void)
{
	unsigned f = fz_cpuflags;
	int i, n;

	fputs("detected cpu features: ", stdout);
	n = 0;
	for (i = 0; i < sizeof(features) / sizeof(featuretest); i++)
	{
		if (f & features[i].flag)
		{
			fputs(features[i].name, stdout);
			n ++;
		}
	}
	if (!n)
		fputs("none", stdout);
	fputc('\n', stdout);
}

/* called by runtime before main()...
 * TODO:
 * CPUACCEL=0 ./x11pdf disables detection
 * CPUACCEL='mmx sse' enables only mmx and sse
 * not set enables everything
 */
void fz_cpudetect(void)
{
	static int hasrun = 0;

	unsigned flags = 0;
	int i;
	void (*oldhandler)(int) = NULL;
	void (*tmphandler)(int);

	if (hasrun)
		return;
	hasrun = 1;

	for (i = 0; i < sizeof(features) / sizeof(featuretest); i++)
	{
		canjump = 0;

		tmphandler = signal(SIGILL, sigillhandler);
		if (!oldhandler)
			oldhandler = tmphandler;

		if (sigsetjmp(jmpbuf, 1))
		{
			/* test failed - disable feature */
			flags &= ~features[i].flag;
			continue;
		}

		canjump = 1;

		features[i].test();

		/* if we got here the test succeeded */
		flags |= features[i].flag;
	}

	/* restore previous signal handler */
	signal(SIGILL, oldhandler);

	fz_cpuflags = flags;

	dumpflags();
}

static __attribute__((constructor)) void fzcpudetect(void)
{
	fz_cpudetect();
}

#ifdef TEST
#include <stdio.h>

/* compile: gcc -DARCH_SPARC -DTEST cpudep.c -o cpudep */
int
main(int n, char **a)
{
	dumpflags();
	return 0;
}
#endif

