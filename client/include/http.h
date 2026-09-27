#pragma once
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    int    status;
    size_t content_length;
} HttpResponse;

/* Fetch body into buf. Returns bytes read, -1 on error. */
int http_get(const char *host, int port, const char *path,
             char *buf, size_t buf_size, HttpResponse *resp);

/* Download to a file. progress(received, total) called each chunk; total=0 if unknown. */
int http_download(const char *host, int port, const char *path,
                  const char *dest_file, void (*progress)(size_t, size_t));

/* Optional UI hooks: busy(true/false) around every request, and waiting(n)
 * on each frame that passes with no data (n = frames since data last came). */
void http_set_hooks(void (*busy)(bool on), void (*waiting)(int frames));

/* Percent-encode `src` into `dst` for use in a URL (keeps unreserved + '/'). */
void url_encode(const char *src, char *dst, int dst_len);
