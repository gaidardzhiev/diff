#include <arm_acle.h>
#include <arm_neon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <stdbool.h>

#define CL 64
#define BS 4	//neon 128bit reg can hold 4x32bit hashes, sh*t yeah

//aligned malloc, cuz cache lines dont f*ck around
static void *amalloc(size_t sz) {
	void *p = NULL;
	if (posix_memalign(&p, CL, sz)) return NULL;
	return p;
}

//each f*cking line cached and hashed
typedef struct {
	char *t;
	size_t l;
	uint32_t h;
} ln_t;

//we grow this bad boy as needed
typedef struct {
	ln_t *ls;
	size_t cap, sz;
} arr_t;

static void arr_init(arr_t *a) {
	a->cap = 64;
	a->sz = 0;
	a->ls = malloc(a->cap * sizeof(ln_t));
	if (!a->ls) {
		perror("malloc failed, shit");
		exit(1);
	}
}

//trim that sh*t
static void trim(char *s) {
	size_t st = 0, en = strlen(s);
	while (st < en && isspace((unsigned char)s[st])) st++;
	while (en > st && isspace((unsigned char)s[en - 1])) en--;
	if (st > 0 || en < strlen(s)) memmove(s, s + st, en - st);
	s[en - st] = 0;
}

//normalize windows CRLF sh*t
static void normnl(char *s) {
	size_t ln = strlen(s);
	if (ln >= 2 && s[ln - 2] == '\r' && s[ln - 1] == '\n') {
		s[ln - 2] = '\n';
		s[ln - 1] = 0;
	}
}

//add lines to array, smartly resizing, trimming the damn thing
static bool a_add(arr_t *a, const char *s, size_t len) {
	if (a->sz == a->cap) {
		size_t nc = a->cap * 2;
		ln_t *nl = realloc(a->ls, nc * sizeof(ln_t));
		if (!nl) return false;
		a->ls = nl;
		a->cap = nc;
	}
	char *cp = malloc(len + 1);
	if (!cp) return false;
	memcpy(cp, s, len);
	cp[len] = 0;
	trim(cp);
	normnl(cp);
	a->ls[a->sz].t = cp;
	a->ls[a->sz].l = strlen(cp);
	a->ls[a->sz].h = 0;
	a->sz++;
	return true;
}

static void a_free(arr_t *a) {
	for (size_t i = 0; i < a->sz; i++) free(a->ls[i].t);
	free(a->ls);
	a->ls = NULL;
	a->sz = 0;
	a->cap = 0;
}

//file load with line parsing
static bool loadf(const char *fn, arr_t *a) {
	FILE *f = fopen(fn, "r");
	if (!f) {
		perror(fn);
		return false;
	}
	char *buf = NULL;
	size_t cap = 0;
	ssize_t r;
	while ((r = getline(&buf, &cap, f)) != -1) {
		normnl(buf);
		trim(buf);
		if (!a_add(a, buf, strlen(buf))) {
			free(buf);
			fclose(f);
			return false;
		}
	}
	free(buf);
	fclose(f);
	return true;
}

//hardware CRC32 hashing f*ck yeah
static uint32_t crc32h(const char *d, size_t l) {
	uint32_t c = 0xFFFFFFFFu;
	size_t i = 0;
	for (; i + 4 <= l; i += 4) {
		uint32_t v;
		memcpy(&v, d + i, 4);
		c = __crc32w(c, v);
	}
	for (; i < l; i++) c = __crc32b(c, d[i]);
	return ~c;
}

//hash all lines, fast...
static void hashls(arr_t *a) {
	for (size_t i = 0; i < a->sz; i++)
		a->ls[i].h = crc32h(a->ls[i].t, a->ls[i].l);
}

//SIMD compare hashes fast batch equality check, mask of matches
static uint32_t simdcmp(uint32_t *a, uint32_t *b, size_t c) {
	uint32x4_t va, vb, eq;
	uint32_t m = 0;
	for (size_t i = 0; i + BS <= c; i += BS) {
		va = vld1q_u32(a + i);
		vb = vld1q_u32(b + i);
		eq = vceqq_u32(va, vb);
		m |= ((vgetq_lane_u32(eq, 0) != 0) << i)
		     | ((vgetq_lane_u32(eq, 1) != 0) << (i + 1))
		     | ((vgetq_lane_u32(eq, 2) != 0) << (i + 2))
		     | ((vgetq_lane_u32(eq, 3) != 0) << (i + 3));
	}
	for (size_t i = (c/BS)*BS; i < c; i++) m |= (a[i] == b[i]) << i;
	return m;
}

//collision verify with neon and fallback byte check
static bool neonv(const char *a, const char *b, size_t l) {
	size_t i = 0;
	for (; i + 16 <= l; i += 16) {
		uint8x16_t v1 = vld1q_u8((const uint8_t *)(a + i));
		uint8x16_t v2 = vld1q_u8((const uint8_t *)(b + i));
		uint8x16_t eq = vceqq_u8(v1, v2);
		uint64x2_t m = vreinterpretq_u64_u8(eq);
		if (vgetq_lane_u64(m, 0) != UINT64_MAX || vgetq_lane_u64(m, 1) != UINT64_MAX)
			return false;
	}
	for (; i < l; i++) if (a[i] != b[i]) return false;
	return true;
}

//diff ops and entries
typedef enum { EQ, INS, DEL } op_t;
typedef struct {
	op_t o;
	size_t a, b;
} ent_t;

//diff list
typedef struct {
	ent_t *e;
	size_t cap, cnt;
} dlst_t;

static void dl_init(dlst_t *d) {
	d->cap = 128;
	d->cnt = 0;
	d->e = malloc(d->cap * sizeof(ent_t));
	if (!d->e) {
		perror("malloc");
		exit(1);
	}
}

static void dl_add(dlst_t *d, op_t o, size_t a, size_t b) {
	if (d->cnt == d->cap) {
		d->cap *= 2;
		ent_t *ne = realloc(d->e, d->cap * sizeof(ent_t));
		if (!ne) {
			free(d->e);
			perror("realloc");
			exit(1);
		}
		d->e = ne;
	}
	d->e[d->cnt++] = (ent_t) {
		o,a,b
	};
}

static void dl_free(dlst_t *d) {
	free(d->e);
}

//hackerman diff algo on hashes verified
static void h_diff(arr_t *a, arr_t *b, dlst_t *d) {
	size_t i=0, j=0;
	while(i<a->sz && j<b->sz) {
		if(a->ls[i].h == b->ls[j].h && neonv(a->ls[i].t,b->ls[j].t,a->ls[i].l)) {
			dl_add(d, EQ, i, j);
			i++;
			j++;
		} else {
			bool next_i = (j +1 < b->sz) && (a->ls[i].h == b->ls[j+1].h)
				      && neonv(a->ls[i].t,b->ls[j+1].t,a->ls[i].l);
			bool next_j = (i +1 < a->sz) && (a->ls[i+1].h == b->ls[j].h)
				      && neonv(a->ls[i+1].t,b->ls[j].t,a->ls[i+1].l);
			if(next_i && !next_j) {
				dl_add(d, INS, -1u, j);
				j++;
			} else if(!next_i && next_j) {
				dl_add(d, DEL, i, -1u);
				i++;
			} else {
				dl_add(d, DEL, i, -1u);
				dl_add(d, INS, -1u, j);
				i++;
				j++;
			}
		}
	}
	while(i<a->sz) dl_add(d, DEL, i++, -1u);
	while(j<b->sz) dl_add(d, INS, -1u, j++);
}

//print unified diff, no bullsh*t...
static void pr_diff(arr_t *a, arr_t *b, dlst_t *d, const char *f1, const char*f2) {
	printf("--- %s\n+++ %s\n", f1, f2);
	size_t i=0;
	while(i < d->cnt) {
		while(i < d->cnt && d->e[i].o == EQ) i++;
		if(i == d->cnt) break;
		size_t st = (i>=3)?i-3:0;
		size_t en = i;
		while(en < d->cnt && d->e[en].o != EQ) en++;
		if(en + 3 < d->cnt) en += 3;
		else en = d->cnt;
		size_t osz = 0, nsz = 0;
		size_t ost = -1u, nst = -1u;
		for(size_t k=st; k<en; k++) {
			ent_t *x = &d->e[k];
			if(x->a != -1u) {
				if (ost == -1u) ost = x->a+1;
				osz++;
			}
			if(x->b != -1u) {
				if (nst == -1u) nst = x->b+1;
				nsz++;
			}
		}
		printf("@@ -%zu,%zu +%zu,%zu @@\n", ost, osz, nst, nsz);
		for(size_t k=st; k<en; k++) {
			ent_t *x = &d->e[k];
			switch (x->o) {
			case EQ:
				printf(" %s\n", a->ls[x->a].t);
				break;
			case INS:
				printf("+%s\n", b->ls[x->b].t);
				break;
			case DEL:
				printf("-%s\n", a->ls[x->a].t);
				break;
			}
		}
		i = en;
	}
}

int main(int c,char **v) {
	if(c!=3) {
		fprintf(stderr,"usage: %s <file> <file>\n",v[0]);
		return 1;
	}
	arr_t a,b;
	dlst_t d;
	arr_init(&a);
	arr_init(&b);
	if(!loadf(v[1], &a)) return 1;
	if(!loadf(v[2], &b)) return 1;
	hashls(&a);
	hashls(&b);
	dl_init(&d);
	h_diff(&a,&b,&d);
	pr_diff(&a,&b,&d,v[1],v[2]);
	dl_free(&d);
	a_free(&a);
	a_free(&b);
	return 0;
}

//you just witnessed hacker grade diffing magic...
