#include "fitz.h"
#include "mupdf.h"

enum
{
	PDF_CRYPT_NONE,
	PDF_CRYPT_RC4,
	PDF_CRYPT_AESV2,
	PDF_CRYPT_AESV3,
	PDF_CRYPT_UNKNOWN,
};

typedef struct pdf_crypt_filter_s pdf_crypt_filter;

struct pdf_crypt_filter_s
{
	int method;
	int length;
};

struct pdf_crypt_s
{
	fz_obj *id;

	int v;
	int length;
	fz_obj *cf;
	pdf_crypt_filter stmf;
	pdf_crypt_filter strf;

	int r;
	unsigned char o[48];
	unsigned char u[48];
	unsigned char oe[32];
	unsigned char ue[32];
	int p;
	int encrypt_metadata;

	unsigned char key[32]; /* decryption key generated from password */
};

static fz_error pdf_parse_crypt_filter(pdf_crypt_filter *cf, fz_obj *dict, char *name, int defaultlength);

/*
 * Create crypt object for decrypting strings and streams
 * given the Encryption and ID objects.
 */

fz_error
pdf_new_crypt(pdf_crypt **cryptp, fz_obj *dict, fz_obj *id)
{
	pdf_crypt *crypt;
	fz_error error;
	fz_obj *obj;

	crypt = fz_malloc(sizeof(pdf_crypt));
	memset(crypt, 0x00, sizeof(pdf_crypt));

	/* Common to all security handlers (PDF 1.7 table 3.18) */

	obj = fz_dict_gets(dict, "Filter");
	if (!fz_is_name(obj))
	{
		pdf_free_crypt(crypt);
		return fz_throw("unspecified encryption handler");
	}
	if (strcmp(fz_to_name(obj), "Standard") != 0)
	{
		pdf_free_crypt(crypt);
		return fz_throw("unknown encryption handler: '%s'", fz_to_name(obj));
	}

	crypt->v = 0;
	obj = fz_dict_gets(dict, "V");
	if (fz_is_int(obj))
		crypt->v = fz_to_int(obj);
	if (crypt->v != 1 && crypt->v != 2 && crypt->v != 4 && crypt->v != 5)
	{
		pdf_free_crypt(crypt);
		return fz_throw("unknown encryption version");
	}

	crypt->length = 40;
	if (crypt->v == 2 || crypt->v == 4)
	{
		obj = fz_dict_gets(dict, "Length");
		if (fz_is_int(obj))
			crypt->length = fz_to_int(obj);

		/* work-around for pdf generators that assume length is in bytes */
		if (crypt->length < 40)
			crypt->length = crypt->length * 8;

		if (crypt->length % 8 != 0)
		{
			pdf_free_crypt(crypt);
			return fz_throw("invalid encryption key length");
		}
		if (crypt->length > 256)
		{
			pdf_free_crypt(crypt);
			return fz_throw("invalid encryption key length");
		}
	}

	if (crypt->v == 5)
		crypt->length = 256;

	if (crypt->v == 1 || crypt->v == 2)
	{
		crypt->stmf.method = PDF_CRYPT_RC4;
		crypt->stmf.length = crypt->length;

		crypt->strf.method = PDF_CRYPT_RC4;
		crypt->strf.length = crypt->length;
	}

	if (crypt->v == 4 || crypt->v == 5)
	{
		crypt->stmf.method = PDF_CRYPT_NONE;
		crypt->stmf.length = crypt->length;

		crypt->strf.method = PDF_CRYPT_NONE;
		crypt->strf.length = crypt->length;

		obj = fz_dict_gets(dict, "CF");
		if (fz_is_dict(obj))
		{
			crypt->cf = fz_keep_obj(obj);
		}
		else
		{
			crypt->cf = NULL;
		}

		obj = fz_dict_gets(dict, "StmF");
		if (fz_is_name(obj))
		{
			error = pdf_parse_crypt_filter(&crypt->stmf, crypt->cf, fz_to_name(obj), crypt->length);
			if (error)
			{
				pdf_free_crypt(crypt);
				return fz_rethrow(error, "cannot parse stream crypt filter (%d %d R)", fz_to_num(obj), fz_to_gen(obj));
			}
		}

		obj = fz_dict_gets(dict, "StrF");
		if (fz_is_name(obj))
		{
			error = pdf_parse_crypt_filter(&crypt->strf, crypt->cf, fz_to_name(obj), crypt->length);
			if (error)
			{
				pdf_free_crypt(crypt);
				return fz_rethrow(error, "cannot parse string crypt filter (%d %d R)", fz_to_num(obj), fz_to_gen(obj));
			}
		}

		/* in crypt revision 4, the crypt filter determines the key length */
		if (crypt->strf.method != PDF_CRYPT_NONE)
			crypt->length = crypt->stmf.length;
	}

	/* Standard security handler (PDF 1.7 table 3.19) */

	obj = fz_dict_gets(dict, "R");
	if (fz_is_int(obj))
		crypt->r = fz_to_int(obj);
	else
	{
		pdf_free_crypt(crypt);
		return fz_throw("encryption dictionary missing revision value");
	}

	obj = fz_dict_gets(dict, "O");
	if (fz_is_string(obj) && fz_to_str_len(obj) == 32)
		memcpy(crypt->o, fz_to_str_buf(obj), 32);
	/* /O and /U are supposed to be 48 bytes long for revision 5, they're often longer, though */
	else if (crypt->r == 5 && fz_is_string(obj) && fz_to_str_len(obj) >= 48)
		memcpy(crypt->o, fz_to_str_buf(obj), 48);
	else
	{
		pdf_free_crypt(crypt);
		return fz_throw("encryption dictionary missing owner password");
	}

	obj = fz_dict_gets(dict, "U");
	if (fz_is_string(obj) && fz_to_str_len(obj) == 32)
		memcpy(crypt->u, fz_to_str_buf(obj), 32);
	else if (fz_is_string(obj) && fz_to_str_len(obj) >= 48 && crypt->r == 5)
		memcpy(crypt->u, fz_to_str_buf(obj), 48);
	else if (fz_is_string(obj) && fz_to_str_len(obj) < 32)
	{
		fz_warn("encryption password key too short (%d)", fz_to_str_len(obj));
		memcpy(crypt->u, fz_to_str_buf(obj), fz_to_str_len(obj));
	}
	else
	{
		pdf_free_crypt(crypt);
		return fz_throw("encryption dictionary missing user password");
	}

	obj = fz_dict_gets(dict, "P");
	if (fz_is_int(obj))
		crypt->p = fz_to_int(obj);
	else
	{
		pdf_free_crypt(crypt);
		return fz_throw("encryption dictionary missing permissions value");
	}

	if (crypt->r == 5)
	{
		obj = fz_dict_gets(dict, "OE");
		if (!fz_is_string(obj) || fz_to_str_len(obj) != 32)
		{
			pdf_free_crypt(crypt);
			return fz_throw("encryption dictionary missing owner encryption key");
		}
		memcpy(crypt->oe, fz_to_str_buf(obj), 32);

		obj = fz_dict_gets(dict, "UE");
		if (!fz_is_string(obj) || fz_to_str_len(obj) != 32)
		{
			pdf_free_crypt(crypt);
			return fz_throw("encryption dictionary missing user encryption key");
		}
		memcpy(crypt->ue, fz_to_str_buf(obj), 32);
	}

	crypt->encrypt_metadata = 1;
	obj = fz_dict_gets(dict, "EncryptMetadata");
	if (fz_is_bool(obj))
		crypt->encrypt_metadata = fz_to_bool(obj);

	/* Extract file identifier string */

	if (fz_is_array(id) && fz_array_len(id) == 2)
	{
		obj = fz_array_get(id, 0);
		if (fz_is_string(obj))
			crypt->id = fz_keep_obj(obj);
	}
	else
		fz_warn("missing file identifier, may not be able to do decryption");

	*cryptp = crypt;
	return fz_okay;
}

void
pdf_free_crypt(pdf_crypt *crypt)
{
	if (crypt->id) fz_drop_obj(crypt->id);
	if (crypt->cf) fz_drop_obj(crypt->cf);
	fz_free(crypt);
}

/*
 * Parse a CF dictionary entry (PDF 1.7 table 3.22)
 */

static fz_error
pdf_parse_crypt_filter(pdf_crypt_filter *cf, fz_obj *cf_obj, char *name, int defaultlength)
{
	fz_obj *obj;
	fz_obj *dict;
	int is_identity = (strcmp(name, "Identity") == 0);
	int is_stdcf = (!is_identity && (strcmp(name, "StdCF") == 0));

	if (!is_identity && !is_stdcf)
	{
		return fz_throw("Crypt Filter not Identity or StdCF (%d %d R)", fz_to_num(cf_obj), fz_to_gen(cf_obj));
	}
	cf->method = PDF_CRYPT_NONE;
	cf->length = defaultlength;

	if (cf_obj == NULL)
	{
		cf->method = (is_identity ? PDF_CRYPT_NONE : PDF_CRYPT_RC4);
		return fz_okay;
	}

	dict = fz_dict_gets(cf_obj, name);
	if (!fz_is_dict(dict))
	{
		return fz_throw("cannot parse crypt filter (%d %d R)", fz_to_num(cf_obj), fz_to_gen(cf_obj));
	}
	obj = fz_dict_gets(dict, "CFM");
	if (fz_is_name(obj))
	{
		if (!strcmp(fz_to_name(obj), "None"))
			cf->method = PDF_CRYPT_NONE;
		else if (!strcmp(fz_to_name(obj), "V2"))
			cf->method = PDF_CRYPT_RC4;
		else if (!strcmp(fz_to_name(obj), "AESV2"))
			cf->method = PDF_CRYPT_AESV2;
		else if (!strcmp(fz_to_name(obj), "AESV3"))
			cf->method = PDF_CRYPT_AESV3;
		else
			fz_throw("unknown encryption method: %s", fz_to_name(obj));
	}

	obj = fz_dict_gets(dict, "Length");
	if (fz_is_int(obj))
		cf->length = fz_to_int(obj);

	/* the length for crypt filters is supposed to be in bytes not bits */
	if (cf->length < 40)
		cf->length = cf->length * 8;

	if ((cf->length % 8) != 0)
		return fz_throw("invalid key length: %d", cf->length);

	return fz_okay;
}

/*
 * Compute an encryption key (PDF 1.7 algorithm 3.2)
 */

static const unsigned char padding[32] =
{
	0x28, 0xbf, 0x4e, 0x5e, 0x4e, 0x75, 0x8a, 0x41,
	0x64, 0x00, 0x4e, 0x56, 0xff, 0xfa, 0x01, 0x08,
	0x2e, 0x2e, 0x00, 0xb6, 0xd0, 0x68, 0x3e, 0x80,
	0x2f, 0x0c, 0xa9, 0xfe, 0x64, 0x53, 0x69, 0x7a
};

static void
pdf_compute_encryption_key(pdf_crypt *crypt, unsigned char *password, int pwlen, unsigned char *key)
{
	unsigned char buf[32];
	unsigned int p;
	int i, n;
	fz_md5 md5;

	n = crypt->length / 8;

	/* Step 1 - copy and pad password string */
	if (pwlen > 32)
		pwlen = 32;
	memcpy(buf, password, pwlen);
	memcpy(buf + pwlen, padding, 32 - pwlen);

	/* Step 2 - init md5 and pass value of step 1 */
	fz_md5_init(&md5);
	fz_md5_update(&md5, buf, 32);

	/* Step 3 - pass O value */
	fz_md5_update(&md5, crypt->o, 32);

	/* Step 4 - pass P value as unsigned int, low-order byte first */
	p = (unsigned int) crypt->p;
	buf[0] = (p) & 0xFF;
	buf[1] = (p >> 8) & 0xFF;
	buf[2] = (p >> 16) & 0xFF;
	buf[3] = (p >> 24) & 0xFF;
	fz_md5_update(&md5, buf, 4);

	/* Step 5 - pass first element of ID array */
	fz_md5_update(&md5, (unsigned char *)fz_to_str_buf(crypt->id), fz_to_str_len(crypt->id));

	/* Step 6 (revision 4 or greater) - if metadata is not encrypted pass 0xFFFFFFFF */
	if (crypt->r >= 4)
	{
		if (!crypt->encrypt_metadata)
		{
			buf[0] = 0xFF;
			buf[1] = 0xFF;
			buf[2] = 0xFF;
			buf[3] = 0xFF;
			fz_md5_update(&md5, buf, 4);
		}
	}

	/* Step 7 - finish the hash */
	fz_md5_final(&md5, buf);

	/* Step 8 (revision 3 or greater) - do some voodoo 50 times */
	if (crypt->r >= 3)
	{
		for (i = 0; i < 50; i++)
		{
			fz_md5_init(&md5);
			fz_md5_update(&md5, buf, n);
			fz_md5_final(&md5, buf);
		}
	}

	/* Step 9 - the key is the first 'n' bytes of the result */
	memcpy(key, buf, n);
}

/*
 * Compute an encryption key (PDF 1.7 ExtensionLevel 3 algorithm 3.2a)
 */

static void
pdf_compute_encryption_key_r5(pdf_crypt *crypt, unsigned char *password, int pwlen, int ownerkey, unsigned char *validationkey)
{
	unsigned char buffer[128 + 8 + 48];
	fz_sha256 sha256;
	fz_aes aes;

	/* Step 2 - truncate UTF-8 password to 127 characters */

	if (pwlen > 127)
		pwlen = 127;

	/* Step 3/4 - test password against owner/user key and compute encryption key */

	memcpy(buffer, password, pwlen);
	if (ownerkey)
	{
		memcpy(buffer + pwlen, crypt->o + 32, 8);
		memcpy(buffer + pwlen + 8, crypt->u, 48);
	}
	else
		memcpy(buffer + pwlen, crypt->u + 32, 8);

	fz_sha256_init(&sha256);
	fz_sha256_update(&sha256, buffer, pwlen + 8 + (ownerkey ? 48 : 0));
	fz_sha256_final(&sha256, validationkey);

	/* Step 3.5/4.5 - compute file encryption key from OE/UE */

	memcpy(buffer + pwlen, crypt->u + 40, 8);

	fz_sha256_init(&sha256);
	fz_sha256_update(&sha256, buffer, pwlen + 8);
	fz_sha256_final(&sha256, buffer);

	// clear password buffer and use it as iv
	memset(buffer + 32, 0, sizeof(buffer) - 32);
	aes_setkey_dec(&aes, buffer, crypt->length);
	aes_crypt_cbc(&aes, AES_DECRYPT, 32, buffer + 32, ownerkey ? crypt->oe : crypt->ue, crypt->key);
}

/*
 * Computing the user password (PDF 1.7 algorithm 3.4 and 3.5)
 * Also save the generated key for decrypting objects and streams in crypt->key.
 */

static void
pdf_compute_user_password(pdf_crypt *crypt, unsigned char *password, int pwlen, unsigned char *output)
{
	if (crypt->r == 2)
	{
		fz_arc4 arc4;

		pdf_compute_encryption_key(crypt, password, pwlen, crypt->key);
		fz_arc4_init(&arc4, crypt->key, crypt->length / 8);
		fz_arc4_encrypt(&arc4, output, padding, 32);
	}

	if (crypt->r == 3 || crypt->r == 4)
	{
		unsigned char xor[32];
		unsigned char digest[16];
		fz_md5 md5;
		fz_arc4 arc4;
		int i, x, n;

		n = crypt->length / 8;

		pdf_compute_encryption_key(crypt, password, pwlen, crypt->key);

		fz_md5_init(&md5);
		fz_md5_update(&md5, padding, 32);
		fz_md5_update(&md5, (unsigned char*)fz_to_str_buf(crypt->id), fz_to_str_len(crypt->id));
		fz_md5_final(&md5, digest);

		fz_arc4_init(&arc4, crypt->key, n);
		fz_arc4_encrypt(&arc4, output, digest, 16);

		for (x = 1; x <= 19; x++)
		{
			for (i = 0; i < n; i++)
				xor[i] = crypt->key[i] ^ x;
			fz_arc4_init(&arc4, xor, n);
			fz_arc4_encrypt(&arc4, output, output, 16);
		}

		memcpy(output + 16, padding, 16);
	}

	if (crypt->r == 5)
	{
		pdf_compute_encryption_key_r5(crypt, password, pwlen, 0, output);
	}
}

/*
 * Authenticating the user password (PDF 1.7 algorithm 3.6
 * and ExtensionLevel 3 algorithm 3.11)
 * This also has the side effect of saving a key generated
 * from the password for decrypting objects and streams.
 */

static int
pdf_authenticate_user_password(pdf_crypt *crypt, unsigned char *password, int pwlen)
{
	unsigned char output[32];
	pdf_compute_user_password(crypt, password, pwlen, output);
	if (crypt->r == 2 || crypt->r == 5)
		return memcmp(output, crypt->u, 32) == 0;
	if (crypt->r == 3 || crypt->r == 4)
		return memcmp(output, crypt->u, 16) == 0;
	return 0;
}

/*
 * Authenticating the owner password (PDF 1.7 algorithm 3.7
 * and ExtensionLevel 3 algorithm 3.12)
 * Generates the user password from the owner password
 * and calls pdf_authenticate_user_password.
 */

static int
pdf_authenticate_owner_password(pdf_crypt *crypt, unsigned char *ownerpass, int pwlen)
{
	unsigned char pwbuf[32];
	unsigned char key[32];
	unsigned char xor[32];
	unsigned char userpass[32];
	int i, n, x;
	fz_md5 md5;
	fz_arc4 arc4;

	if (crypt->r == 5)
	{
		/* PDF 1.7 ExtensionLevel 3 algorithm 3.12 */

		pdf_compute_encryption_key_r5(crypt, ownerpass, pwlen, 1, key);

		return !memcmp(key, crypt->o, 32);
	}

	n = crypt->length / 8;

	/* Step 1 -- steps 1 to 4 of PDF 1.7 algorithm 3.3 */

	/* copy and pad password string */
	if (pwlen > 32)
		pwlen = 32;
	memcpy(pwbuf, ownerpass, pwlen);
	memcpy(pwbuf + pwlen, padding, 32 - pwlen);

	/* take md5 hash of padded password */
	fz_md5_init(&md5);
	fz_md5_update(&md5, pwbuf, 32);
	fz_md5_final(&md5, key);

	/* do some voodoo 50 times (Revision 3 or greater) */
	if (crypt->r >= 3)
	{
		for (i = 0; i < 50; i++)
		{
			fz_md5_init(&md5);
			fz_md5_update(&md5, key, 16);
			fz_md5_final(&md5, key);
		}
	}

	/* Step 2 (Revision 2) */
	if (crypt->r == 2)
	{
		fz_arc4_init(&arc4, key, n);
		fz_arc4_encrypt(&arc4, userpass, crypt->o, 32);
	}

	/* Step 2 (Revision 3 or greater) */
	if (crypt->r >= 3)
	{
		memcpy(userpass, crypt->o, 32);
		for (x = 0; x < 20; x++)
		{
			for (i = 0; i < n; i++)
				xor[i] = key[i] ^ (19 - x);
			fz_arc4_init(&arc4, xor, n);
			fz_arc4_encrypt(&arc4, userpass, userpass, 32);
		}
	}

	return pdf_authenticate_user_password(crypt, userpass, 32);
}

int
pdf_authenticate_password(pdf_xref *xref, char *password)
{
	if (xref->crypt)
	{
		if (pdf_authenticate_user_password(xref->crypt, (unsigned char *)password, strlen(password)))
			return 1;
		if (pdf_authenticate_owner_password(xref->crypt, (unsigned char *)password, strlen(password)))
			return 1;
		return 0;
	}
	return 1;
}

int
pdf_needs_password(pdf_xref *xref)
{
	if (!xref->crypt)
		return 0;
	if (pdf_authenticate_password(xref, ""))
		return 0;
	return 1;
}

int
pdf_has_permission(pdf_xref *xref, int p)
{
	if (!xref->crypt)
		return 1;
	return xref->crypt->p & p;
}

unsigned char *
pdf_get_crypt_key(pdf_xref *xref)
{
	if (xref->crypt)
		return xref->crypt->key;
	return NULL;
}

int
pdf_get_crypt_revision(pdf_xref *xref)
{
	if (xref->crypt)
		return xref->crypt->v;
	return 0;
}

char *
pdf_get_crypt_method(pdf_xref *xref)
{
	if (xref->crypt)
	{
		switch (xref->crypt->strf.method)
		{
		case PDF_CRYPT_NONE: return "None";
		case PDF_CRYPT_RC4: return "RC4";
		case PDF_CRYPT_AESV2: return "AES";
		case PDF_CRYPT_AESV3: return "AES";
		case PDF_CRYPT_UNKNOWN: return "Unknown";
		}
	}
	return "None";
}

int
pdf_get_crypt_length(pdf_xref *xref)
{
	if (xref->crypt)
		return xref->crypt->length;
	return 0;
}

/*
 * PDF 1.7 algorithm 3.1 and ExtensionLevel 3 algorithm 3.1a
 *
 * Using the global encryption key that was generated from the
 * password, create a new key that is used to decrypt indivual
 * objects and streams. This key is based on the object and
 * generation numbers.
 */

static int
pdf_compute_object_key(pdf_crypt *crypt, pdf_crypt_filter *cf, int num, int gen, unsigned char *key)
{
	fz_md5 md5;
	unsigned char message[5];

	if (cf->method == PDF_CRYPT_AESV3)
	{
		memcpy(key, crypt->key, crypt->length / 8);
		return crypt->length / 8;
	}

	fz_md5_init(&md5);
	fz_md5_update(&md5, crypt->key, crypt->length / 8);
	message[0] = (num) & 0xFF;
	message[1] = (num >> 8) & 0xFF;
	message[2] = (num >> 16) & 0xFF;
	message[3] = (gen) & 0xFF;
	message[4] = (gen >> 8) & 0xFF;
	fz_md5_update(&md5, message, 5);

	if (cf->method == PDF_CRYPT_AESV2)
		fz_md5_update(&md5, (unsigned char *)"sAlT", 4);

	fz_md5_final(&md5, key);

	if (crypt->length / 8 + 5 > 16)
		return 16;
	return crypt->length / 8 + 5;
}

/*
 * PDF 1.7 algorithm 3.1 and ExtensionLevel 3 algorithm 3.1a
 *
 * Decrypt all strings in obj modifying the data in-place.
 * Recurse through arrays and dictionaries, but do not follow
 * indirect references.
 */

static void
pdf_crypt_obj_imp(pdf_crypt *crypt, fz_obj *obj, unsigned char *key, int keylen)
{
	unsigned char *s;
	int i, n;

	if (fz_is_indirect(obj))
		return;

	if (fz_is_string(obj))
	{
		s = (unsigned char *) fz_to_str_buf(obj);
		n = fz_to_str_len(obj);

		if (crypt->strf.method == PDF_CRYPT_RC4)
		{
			fz_arc4 arc4;
			fz_arc4_init(&arc4, key, keylen);
			fz_arc4_encrypt(&arc4, s, s, n);
		}

		if (crypt->strf.method == PDF_CRYPT_AESV2 || crypt->strf.method == PDF_CRYPT_AESV3)
		{
			if (n & 15 || n < 32)
				fz_warn("invalid string length for aes encryption");
			else
			{
				unsigned char iv[16];
				fz_aes aes;
				memcpy(iv, s, 16);
				aes_setkey_dec(&aes, key, keylen * 8);
				aes_crypt_cbc(&aes, AES_DECRYPT, n - 16, iv, s + 16, s);
				/* delete space used for iv and padding bytes at end */
				if (s[n - 17] < 1 || s[n - 17] > 16)
					fz_warn("aes padding out of range");
				else
					fz_set_str_len(obj, n - 16 - s[n - 17]);
			}
		}
	}

	else if (fz_is_array(obj))
	{
		n = fz_array_len(obj);
		for (i = 0; i < n; i++)
		{
			pdf_crypt_obj_imp(crypt, fz_array_get(obj, i), key, keylen);
		}
	}

	else if (fz_is_dict(obj))
	{
		n = fz_dict_len(obj);
		for (i = 0; i < n; i++)
		{
			pdf_crypt_obj_imp(crypt, fz_dict_get_val(obj, i), key, keylen);
		}
	}
}

void
pdf_crypt_obj(pdf_crypt *crypt, fz_obj *obj, int num, int gen)
{
	unsigned char key[32];
	int len;

	len = pdf_compute_object_key(crypt, &crypt->strf, num, gen, key);

	pdf_crypt_obj_imp(crypt, obj, key, len);
}

/*
 * PDF 1.7 algorithm 3.1 and ExtensionLevel 3 algorithm 3.1a
 *
 * Create filter suitable for de/encrypting a stream.
 */
static fz_stream *
pdf_open_crypt_imp(fz_stream *chain, pdf_crypt *crypt, pdf_crypt_filter *stmf, int num, int gen)
{
	unsigned char key[32];
	int len;

	len = pdf_compute_object_key(crypt, stmf, num, gen, key);

	if (stmf->method == PDF_CRYPT_RC4)
		return fz_open_arc4(chain, key, len);

	if (stmf->method == PDF_CRYPT_AESV2 || stmf->method == PDF_CRYPT_AESV3)
		return fz_open_aesd(chain, key, len);

	return fz_open_copy(chain);
}

fz_stream *
pdf_open_crypt(fz_stream *chain, pdf_crypt *crypt, int num, int gen)
{
	return pdf_open_crypt_imp(chain, crypt, &crypt->stmf, num, gen);
}

fz_stream *
pdf_open_crypt_with_filter(fz_stream *chain, pdf_crypt *crypt, char *name, int num, int gen)
{
	fz_error error;
	pdf_crypt_filter cf;

	if (strcmp(name, "Identity"))
	{
		error = pdf_parse_crypt_filter(&cf, crypt->cf, name, crypt->length);
		if (error)
			fz_catch(error, "cannot parse crypt filter (%d %d R)", num, gen);
		else
			return pdf_open_crypt_imp(chain, crypt, &cf, num, gen);
	}
	return chain;
}

void pdf_debug_crypt(pdf_crypt *crypt)
{
	int i;

	printf("crypt {\n");

	printf("\tv=%d length=%d\n", crypt->v, crypt->length);
	printf("\tstmf method=%d length=%d\n", crypt->stmf.method, crypt->stmf.length);
	printf("\tstrf method=%d length=%d\n", crypt->strf.method, crypt->strf.length);
	printf("\tr=%d\n", crypt->r);

	printf("\to=<");
	for (i = 0; i < 32; i++)
		printf("%02X", crypt->o[i]);
	printf(">\n");

	printf("\tu=<");
	for (i = 0; i < 32; i++)
		printf("%02X", crypt->u[i]);
	printf(">\n");

	printf("}\n");
}
