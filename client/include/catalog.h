#pragma once
#include <stddef.h>

/* The client only ever holds ONE page in RAM; the server is asked for pages
   on demand, so the total library size is unbounded by DS memory. */
#define PAGE_SIZE       18
#define MAX_NAME_LEN    48
#define MAX_FILE_LEN    128
#define MAX_DESC_LEN    96

typedef struct {
    char   name[MAX_NAME_LEN];
    char   file[MAX_FILE_LEN];
    size_t size;
    char   desc[MAX_DESC_LEN];
} Title;

/* Holds a single page of up to PAGE_SIZE titles. */
typedef struct {
    Title titles[PAGE_SIZE];
    int   count;
} Catalog;

/* Total titles in a category, optionally filtered by search `query`
   (NULL/"" = no filter). GET /count?cat=&q=. -1 on error. */
int catalog_total(const char *server, int port,
                  const char *category, const char *query);

/* Fetch page `page` (0-based) of `category` (optionally filtered by `query`)
   into `out`. Returns the page count, or -1 on error. */
int catalog_fetch_page(Catalog *out, const char *server, int port,
                       const char *category, const char *query, int page);

/* Parse pipe-delimited catalog text into `cat` (up to PAGE_SIZE entries). */
int catalog_parse(const char *data, Catalog *catalog);
