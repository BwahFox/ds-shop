#include "catalog.h"
#include "http.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


int catalog_parse(const char *data, Catalog *catalog) {
    catalog->count = 0;
    const char *p = data;

    while (*p && catalog->count < PAGE_SIZE) {
        const char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);

        if (eol == p) { p++; continue; }

        Title *t = &catalog->titles[catalog->count];

#define NEXT_FIELD(dst, max_len)                                \
        {                                                       \
            const char *pipe = (const char *)memchr(p, '|',    \
                                    (size_t)(eol - p));         \
            if (!pipe) goto skip_line;                          \
            int flen = (int)(pipe - p);                         \
            if (flen >= (max_len)) flen = (max_len) - 1;       \
            memcpy(dst, p, (size_t)flen);                       \
            (dst)[flen] = '\0';                                 \
            p = pipe + 1;                                       \
        }

        NEXT_FIELD(t->name, MAX_NAME_LEN)
        NEXT_FIELD(t->file, MAX_FILE_LEN)

        /* size field */
        {
            const char *pipe = (const char *)memchr(p, '|', (size_t)(eol - p));
            if (!pipe) goto skip_line;
            t->size = (size_t)strtoul(p, NULL, 10);
            p = pipe + 1;
        }

        /* desc: rest of line, strip \r */
        {
            int dlen = (int)(eol - p);
            while (dlen > 0 && (p[dlen - 1] == '\r')) dlen--;
            if (dlen >= MAX_DESC_LEN) dlen = MAX_DESC_LEN - 1;
            memcpy(t->desc, p, (size_t)dlen);
            t->desc[dlen] = '\0';
        }

        catalog->count++;

skip_line:
        p = (*eol) ? eol + 1 : eol;
    }

#undef NEXT_FIELD

    return catalog->count;
}

#ifdef TEST_MODE

/* Offline fixture for the --test build: a fake multi-page library. */
static const struct { const char *name, *file; size_t size; } k_test[] = {
    { "Mario Kart DS",        "mario_kart_ds.nds",   67108864  },
    { "New Super Mario Bros", "nsmb.nds",            134217728 },
    { "Pokemon Diamond",      "pokemon_diamond.nds", 268435456 },
    { "Zelda: Phantom Hour.", "zelda_ph.nds",        134217728 },
    { "Metroid Prime Hunt.",  "mph.nds",             67108864  },
    { "Castlevania: PoR",     "cvpor.nds",           67108864  },
    { "Advance Wars: DC",     "awdc.nds",            33554432  },
    { "Elite Beat Agents",    "eba.nds",             67108864  },
    { "Kirby: Canvas Curse",  "kirby_cc.nds",        33554432  },
    { "WarioWare: Touched!",  "warioware.nds",       33554432  },
    { "Trauma Center: UaK",   "trauma.nds",          67108864  },
    { "Contra 4",             "contra4.nds",         67108864  },
    { "Sonic Rush",           "sonic_rush.nds",      67108864  },
    { "Mario & Luigi: PiT",   "mlpit.nds",           67108864  },
    { "Animal Crossing WW",   "acww.nds",            134217728 },
    { "Tetris DS",            "tetris_ds.nds",       33554432  },
    { "Nintendogs",           "nintendogs.nds",      67108864  },
    { "Brain Age",            "brain_age.nds",       16777216  },
    { "Picross DS",           "picross_ds.nds",      33554432  },
    { "Meteos",               "meteos.nds",          33554432  },
    { "Lost Magic",           "lost_magic.nds",      67108864  },
};
#define K_TEST_COUNT ((int)(sizeof(k_test) / sizeof(k_test[0])))

int catalog_total(const char *server, int port,
                  const char *category, const char *query) {
    (void)server; (void)port; (void)category; (void)query;
    return K_TEST_COUNT;
}

int catalog_fetch_page(Catalog *cat, const char *server, int port,
                       const char *category, const char *query, int page) {
    (void)server; (void)port; (void)category; (void)query;
    cat->count = 0;
    int start = page * PAGE_SIZE;
    for (int i = 0; i < PAGE_SIZE && start + i < K_TEST_COUNT; i++) {
        Title *t = &cat->titles[cat->count++];
        strncpy(t->name, k_test[start + i].name, MAX_NAME_LEN - 1);
        t->name[MAX_NAME_LEN - 1] = '\0';
        strncpy(t->file, k_test[start + i].file, MAX_FILE_LEN - 1);
        t->file[MAX_FILE_LEN - 1] = '\0';
        t->size    = k_test[start + i].size;
        t->desc[0] = '\0';
    }
    return cat->count;
}

#else /* normal build: fetch pages over HTTP */

/* A page of text is tiny (~18 lines); plenty of headroom here. */
#define CATALOG_BUF_SIZE 8192

static char catalog_buf[CATALOG_BUF_SIZE];

/* Build "&q=<encoded>" into qarg, or "" when there's no query. */
static void query_arg(const char *query, char *qarg, int qarg_len) {
    if (query && query[0]) {
        char enc[96];
        url_encode(query, enc, sizeof(enc));
        snprintf(qarg, (size_t)qarg_len, "&q=%s", enc);
    } else {
        qarg[0] = '\0';
    }
}

int catalog_total(const char *server, int port,
                  const char *category, const char *query) {
    char qarg[128];
    query_arg(query, qarg, sizeof(qarg));
    char path[192];
    snprintf(path, sizeof(path), "/count?cat=%s%s", category, qarg);

    HttpResponse resp;
    /* http_get buffers headers+body together, so this must fit the HTTP
       headers (~150 bytes) plus the small numeric body. */
    char buf[256];
    int len = http_get(server, port, path, buf, sizeof(buf), &resp);
    if (len < 0 || resp.status != 200) return -1;
    return atoi(buf);
}

int catalog_fetch_page(Catalog *cat, const char *server, int port,
                       const char *category, const char *query, int page) {
    char qarg[128];
    query_arg(query, qarg, sizeof(qarg));
    char path[224];
    snprintf(path, sizeof(path), "/catalog.txt?cat=%s%s&page=%d&size=%d",
             category, qarg, page, PAGE_SIZE);

    HttpResponse resp;
    int len = http_get(server, port, path, catalog_buf, CATALOG_BUF_SIZE, &resp);
    if (len < 0 || resp.status != 200) return -1;
    return catalog_parse(catalog_buf, cat);
}

#endif
