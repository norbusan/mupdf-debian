/* hexdump.c -- an "xxd -i" workalike for dumping binary files as source code */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int zero, string;

static int
hexdump(FILE *fo, FILE *fi)
{
	int c, n;

	if (string)
		fprintf(fo, "\"");

	n = 0;
	c = fgetc(fi);
	while (c != -1)
	{
		n += fprintf(fo, string ? "\\x%02x" : "%d,", c);
		if (n > 72) {
			fprintf(fo, string ? "\"\n\"" : "\n");
			n = 0;
		}
		c = fgetc(fi);
	}

	if (string)
		fprintf(fo, "\"\n");

	return n;
}

int
main(int argc, char **argv)
{
	FILE *fo;
	FILE *fi;
	char filename[256];
	char *basename;
	char *p;
	int i, optind, size;

	if (argc < 3)
	{
		fprintf(stderr, "usage: hexdump [-0] [-s] output.c input.dat\n");
		return 1;
	}

	zero = 0;
	string = 0;
	optind = 1;

	if (!strcmp(argv[optind], "-0")) {
		++optind;
		zero = 1;
	}

	if (!strcmp(argv[optind], "-s")) {
		++optind;
		string = 1;
	}

	fo = fopen(argv[optind], "wb");
	if (!fo)
	{
		fprintf(stderr, "hexdump: could not open output file '%s'\n", argv[optind]);
		return 1;
	}

	for (i = optind+1; i < argc; i++)
	{
		fi = fopen(argv[i], "rb");
		if (!fi)
		{
			fclose(fo);
			fprintf(stderr, "hexdump: could not open input file '%s'\n", argv[i]);
			return 1;
		}

		basename = strrchr(argv[i], '/');
		if (!basename)
			basename = strrchr(argv[i], '\\');
		if (basename)
			basename++;
		else
			basename = argv[i];

		if (strlen(basename) >= sizeof(filename))
		{
			fclose(fi);
			fclose(fo);
			fprintf(stderr, "hexdump: filename '%s' too long\n", basename);
			return 1;
		}

		strcpy(filename, argv[i]);
		for (p = filename; *p; ++p)
		{
			if (*p == '/' || *p == '.' || *p == '\\' || *p == '-')
				*p = '_';
		}

		fseek(fi, 0, SEEK_END);
		size = ftell(fi);
		fseek(fi, 0, SEEK_SET);

		fprintf(fo, "const int fz_%s_size = %d;\n", filename, size + zero);
		fprintf(fo, "const unsigned char fz_%s[] =", filename);
		fprintf(fo, string ? "\n" : " {\n");
		hexdump(fo, fi);
		if (!zero)
		{
			fprintf(fo, string ? ";\n" : "};\n");
		}
		else
		{
			/* zero-terminate so we can hexdump text files into C strings */
			fprintf(fo, string ? ";\n" : "0};\n");
		}

		fclose(fi);
	}

	if (fclose(fo))
	{
		fprintf(stderr, "hexdump: could not close output file '%s'\n", argv[1]);
		return 1;
	}

	return 0;
}
