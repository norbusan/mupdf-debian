#ifndef js_utf_h
#define js_utf_h

typedef unsigned short Rune;	/* 16 bits */

#define chartorune	jsU_chartorune
#define runetochar	jsU_runetochar
#define runelen		jsU_runelen
#define utflen		jsU_utflen

#define isalpharune	jsU_isalpharune
#define islowerrune	jsU_islowerrune
#define isspacerune	jsU_isspacerune
#define istitlerune	jsU_istitlerune
#define isupperrune	jsU_isupperrune
#define tolowerrune	jsU_tolowerrune
#define totitlerune	jsU_totitlerune
#define toupperrune	jsU_toupperrune

enum
{
	UTFmax		= 3,		/* maximum bytes per rune */
	Runesync	= 0x80,		/* cannot represent part of a UTF sequence (<) */
	Runeself	= 0x80,		/* rune and UTF sequences are the same (<) */
	Runeerror	= 0xFFFD,	/* decoding error in UTF */
};

unsigned int	chartorune(Rune *rune, const char *str);
unsigned int	runetochar(char *str, const Rune *rune);
unsigned int	runelen(int c);
unsigned int	utflen(const char *s);

int		isalpharune(Rune c);
int		islowerrune(Rune c);
int		isspacerune(Rune c);
int		istitlerune(Rune c);
int		isupperrune(Rune c);
Rune		tolowerrune(Rune c);
Rune		totitlerune(Rune c);
Rune		toupperrune(Rune c);

#endif
