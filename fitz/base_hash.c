/* Linear probe hash table.
 * 2004 (C) Tor Andersson.
 * BSD license.
 *
 * Simple hashtable with open adressing linear probe.
 * Unlike text book examples, removing entries works
 * correctly in this implementation so it wont start
 * exhibiting bad behaviour if entries are inserted
 * and removed frequently.
 */

#include "fitz_base.h"

enum { MAXKEYLEN = 16 };

typedef struct fz_hashentry_s fz_hashentry;

struct fz_hashentry_s
{
	unsigned char key[MAXKEYLEN];
	void *val;
};

struct fz_hashtable_s
{
	int keylen;
	int size;
	int load;
	fz_hashentry *ents;
};

static unsigned hash(unsigned char *s, int len)
{
	unsigned hash = 0;
	int i;
	for (i = 0; i < len; i++)
	{
		hash += s[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}

fz_error
fz_newhash(fz_hashtable **tablep, int initialsize, int keylen)
{
	fz_hashtable *table;

	assert(keylen <= MAXKEYLEN);

	table = *tablep = fz_malloc(sizeof(fz_hashtable));
	if (!table)
		return fz_rethrow(-1, "out of memory: hash table struct");

	table->keylen = keylen;
	table->size = initialsize;
	table->load = 0;

	table->ents = fz_malloc(sizeof(fz_hashentry) * table->size);
	if (!table->ents)
	{
		fz_free(table);
		*tablep = nil;
		return fz_rethrow(-1, "out of memory: hash table entries (size=%d)", initialsize);
	}

	memset(table->ents, 0, sizeof(fz_hashentry) * table->size);

	return fz_okay;
}

void
fz_emptyhash(fz_hashtable *table)
{
	table->load = 0;
	memset(table->ents, 0, sizeof(fz_hashentry) * table->size);
}

int
fz_hashlen(fz_hashtable *table)
{
	return table->size;
}

void *
fz_hashgetkey(fz_hashtable *table, int idx)
{
	return table->ents[idx].key;
}

void *
fz_hashgetval(fz_hashtable *table, int idx)
{
	return table->ents[idx].val;
}

void
fz_drophash(fz_hashtable *table)
{
	fz_free(table->ents);
	fz_free(table);
}

fz_error
fz_resizehash(fz_hashtable *table, int newsize)
{
	fz_error error;
	fz_hashentry *newents;
	fz_hashentry *oldents;
	int oldload;
	int oldsize;
	int i;

	oldsize = table->size;
	oldload = table->load;
	oldents = table->ents;

	if (newsize < oldload * 8 / 10)
		return fz_throw("assert: resize hash too small");

	newents = fz_malloc(sizeof(fz_hashentry) * newsize);
	if (!newents)
		return fz_rethrow(-1, "out of memory: hash table (size=%d)", newsize);

	table->size = newsize;
	table->load = 0;
	table->ents = newents;
	memset(table->ents, 0, sizeof(fz_hashentry) * table->size);

	for (i = 0; i < oldsize; i++)
	{
		if (oldents[i].val)
		{
			error = fz_hashinsert(table, oldents[i].key, oldents[i].val);
			if (error)
			{
				table->size = oldsize;
				table->load = oldload;
				table->ents = oldents;
				fz_free(newents);
				return fz_rethrow(error, "cannot re-insert old entries");
			}
		}
	}

	fz_free(oldents);

	return fz_okay;
}

void *
fz_hashfind(fz_hashtable *table, void *key)
{
	fz_hashentry *ents = table->ents;
	unsigned size = table->size;
	unsigned pos = hash(key, table->keylen) % size;

	while (1)
	{
		if (!ents[pos].val)
			return nil;

		if (memcmp(key, &ents[pos].key, table->keylen) == 0)
			return ents[pos].val;

		pos = (pos + 1) % size;
	}
}

fz_error
fz_hashinsert(fz_hashtable *table, void *key, void *val)
{
	fz_error error;
	fz_hashentry *ents;
	unsigned size;
	unsigned pos;

	if (table->load > table->size * 8 / 10)
	{
		error = fz_resizehash(table, table->size * 2);
		if (error)
			return fz_rethrow(error, "cannot resize hash table");
	}

	ents = table->ents;
	size = table->size;
	pos = hash(key, table->keylen) % size;

	while (1)
	{
		if (!ents[pos].val)
		{
			memcpy(ents[pos].key, key, table->keylen);
			ents[pos].val = val;
			table->load ++;
			return fz_okay;
		}

		if (memcmp(key, &ents[pos].key, table->keylen) == 0)
			return fz_throw("assert: overwrite hash slot");

		pos = (pos + 1) % size;
	}

	return fz_okay;
}

fz_error
fz_hashremove(fz_hashtable *table, void *key)
{
	fz_hashentry *ents = table->ents;
	unsigned size = table->size;
	unsigned pos = hash(key, table->keylen) % size;
	unsigned hole, look, code;

	while (1)
	{
		if (!ents[pos].val)
			return fz_throw("assert: remove inexistant hash entry");

		if (memcmp(key, &ents[pos].key, table->keylen) == 0)
		{
			ents[pos].val = nil;

			hole = pos;
			look = (hole + 1) % size;

			while (ents[look].val)
			{
				code = hash(ents[look].key, table->keylen) % size;
				if ((code <= hole && hole < look) ||
					(look < code && code <= hole) ||
					(hole < look && look < code))
				{
					ents[hole] = ents[look];
					ents[look].val = nil;
					hole = look;
				}

				look = (look + 1) % size;
			}

			table->load --;

			return fz_okay;
		}

		pos = (pos + 1) % size;
	}
}

void
fz_debughash(fz_hashtable *table)
{
	int i, k;

	printf("cache load %d / %d\n", table->load, table->size);

	for (i = 0; i < table->size; i++)
	{
		if (!table->ents[i].val)
			printf("table % 4d: empty\n", i);
		else
		{
			printf("table % 4d: key=", i);
			for (k = 0; k < MAXKEYLEN; k++)
				printf("%02x", ((char*)table->ents[i].key)[k]);
			printf(" val=$%p\n", table->ents[i].val);
		}
	}
}

