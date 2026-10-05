/* Minimal HTTP/1.1 client for talking to Floppy over plain HTTP. */
#pragma once

#include "config.h"

#include <stddef.h>

struct http_reply {
  int    status;      /* HTTP status code, 0 if the request never completed */
  char  *body;        /* points into the caller's buffer, NUL terminated */
  size_t body_len;
  char   error[400];  /* set when the request failed before a status arrived */
  int    truncated;   /* the reply filled the buffer, so its end is missing */
};

/* Send one request to the configured Floppy and wait for the reply.
 * `path` is appended to the configured base path, e.g. "/api/v1/info".
 * `json_body` may be NULL. `buf` receives the raw reply and backs `reply`.
 * Returns 0 if a status line was received, -1 otherwise. */
int http_request(const struct config *cfg, const char *method,
                 const char *path, int send_token, const char *json_body,
                 char *buf, size_t buf_size, struct http_reply *reply);
