#ifndef MUPDF_FITZ_XML_H
#define MUPDF_FITZ_XML_H

#include "mupdf/fitz/system.h"
#include "mupdf/fitz/context.h"

/*
	XML document model
*/

typedef struct fz_xml_s fz_xml;

/*
	fz_parse_xml: Parse a zero-terminated string into a tree of xml nodes.

	preserve_white: whether to keep or delete all-whitespace nodes.
*/
fz_xml *fz_parse_xml(fz_context *ctx, unsigned char *buf, int len, int preserve_white);

/*
	fz_xml_prev: Return previous sibling of XML node.
*/
fz_xml *fz_xml_prev(fz_xml *item);

/*
	fz_xml_next: Return next sibling of XML node.
*/
fz_xml *fz_xml_next(fz_xml *item);

/*
	fz_xml_up: Return parent of XML node.
*/
fz_xml *fz_xml_up(fz_xml *item);

/*
	fz_xml_down: Return first child of XML node.
*/
fz_xml *fz_xml_down(fz_xml *item);

/*
	fz_xml_is_tag: Return true if the tag name matches.
*/
int fz_xml_is_tag(fz_xml *item, const char *name);

/*
	fz_xml_tag: Return tag of XML node. Return NULL for text nodes.
*/
char *fz_xml_tag(fz_xml *item);

/*
	fz_xml_att: Return the value of an attribute of an XML node.
	NULL if the attribute doesn't exist.
*/
char *fz_xml_att(fz_xml *item, const char *att);

/*
	fz_xml_text: Return the text content of an XML node.
	Return NULL if the node is a tag.
*/
char *fz_xml_text(fz_xml *item);

/*
	fz_drop_xml: Free the XML node and all its children and siblings.
*/
void fz_drop_xml(fz_context *doc, fz_xml *item);

/*
	fz_detach_xml: Detach a node from the tree, unlinking it from its parent.
*/
void fz_detach_xml(fz_xml *node);

/*
	fz_debug_xml: Pretty-print an XML tree to stdout.
*/
void fz_debug_xml(fz_xml *item, int level);

fz_xml *fz_xml_find(fz_xml *item, const char *tag);
fz_xml *fz_xml_find_next(fz_xml *item, const char *tag);
fz_xml *fz_xml_find_down(fz_xml *item, const char *tag);

#endif
