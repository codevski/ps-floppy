/* Settings the user enters on the config page, stored as one JSON file. */
#pragma once

#include <stddef.h>

struct config {
  char floppy_url[320]; /* as the user typed it, normalised */
  char token[256];      /* Floppy API token, empty if not set */
  int  send;            /* 1: write playtime to Floppy. 0: dry-run only */

  /* Derived from floppy_url by config_set_url. */
  char host[128];
  char port[8];
  char base[128];       /* path prefix without trailing slash, may be empty */
};

/* Validate and store a Floppy address. Returns 0, or -1 with a message the
 * user can act on in `err`. */
int config_set_url(struct config *cfg, const char *url, char *err, size_t n);

/* Validate and store a token. Returns 0, or -1 with a message in `err`. */
int config_set_token(struct config *cfg, const char *token, char *err,
                     size_t n);

int config_is_complete(const struct config *cfg);

/* Missing file is not an error: the config just stays empty. */
int config_load(struct config *cfg, const char *path);

/* Written to a temporary file and renamed, so a crash never leaves half a
 * config behind. */
int config_save(const struct config *cfg, const char *path);
