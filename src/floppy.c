#include "floppy.h"
#include "http_client.h"
#include "writes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same size as the config page's client buffer. A reply that does not fit
 * arrives cut short and fails json_valid, so it is retried, never misread. */
#define FLOPPY_BUF (64 * 1024)

/* Percent-encode `in` for a query string. Every byte that is not unreserved
 * is encoded, so names with spaces, "&", ":" or non-ASCII text are safe.
 * Returns 0, or -1 if it does not fit. */
static int
url_encode(const char *in, char *out, size_t size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t n = 0;

  for(; *in; in++) {
    unsigned char c = (unsigned char)*in;

    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
       c == '~') {
      if(n + 1 >= size) return -1;
      out[n++] = (char)c;
    } else {
      if(n + 3 >= size) return -1;
      out[n++] = '%';
      out[n++] = hex[c >> 4];
      out[n++] = hex[c & 15];
    }
  }
  out[n] = 0;
  return 0;
}

/* GET `what` (already encoded) from `route`, where `route` ends in
 * "search=". On success returns the body, which lives in *buf and must be
 * freed by the caller. On failure returns NULL with the reason in `why`. */
static const char*
search(const struct config *cfg, const char *route, const char *what,
       const char *suffix, char **buf, char *why, size_t why_size) {
  struct http_reply reply;
  char path[1024];
  int len;

  *buf = NULL;
  len = snprintf(path, sizeof path, "%s%s%s", route, what, suffix);
  if(len < 0 || (size_t)len >= sizeof path) {
    snprintf(why, why_size, "The game name is too long to search for.");
    return NULL;
  }
  if(!(*buf = malloc(FLOPPY_BUF))) {
    snprintf(why, why_size, "Out of memory.");
    return NULL;
  }
  if(http_request(cfg, "GET", path, 1, NULL, *buf, FLOPPY_BUF, &reply)) {
    snprintf(why, why_size, "%s", reply.error);
    return NULL;
  }
  if(reply.status == 401 || reply.status == 403) {
    snprintf(why, why_size, "Floppy rejected the API token (HTTP %d). Check "
             "it on the config page.", reply.status);
    return NULL;
  }
  if(reply.status != 200) {
    snprintf(why, why_size, "Floppy answered the search with HTTP %d.",
             reply.status);
    return NULL;
  }
  if(reply.truncated) {
    snprintf(why, why_size, "Floppy's reply was too big for the %d KB "
             "buffer and was cut short. This search will keep failing until "
             "the limit is lowered.", FLOPPY_BUF / 1024);
    return NULL;
  }
  return reply.body;
}

/* A sentence for each result other than MATCH_FOUND. */
static void
explain(enum match_result r, const char *bad, const char *none,
        const char *ambiguous, char *why, size_t why_size) {
  switch(r) {
  case MATCH_FOUND:
    break;
  case MATCH_NONE:
    snprintf(why, why_size, "%s", none);
    break;
  case MATCH_AMBIGUOUS:
    snprintf(why, why_size, "%s", ambiguous);
    break;
  case MATCH_BAD_REPLY:
    snprintf(why, why_size, "%s", bad);
    break;
  }
}

#define STR2(x) #x
#define STR(x) STR2(x)

enum match_result
floppy_find_in_library(const struct config *cfg, const char *console_name,
                       struct match *out, char *why, size_t why_size) {
  char name[512];
  const char *body;
  char *buf;
  enum match_result r;

  if(url_encode(console_name, name, sizeof name)) {
    snprintf(why, why_size, "The game name is too long to search for.");
    return MATCH_BAD_REPLY;
  }
  if(!(body = search(cfg, "/api/v1/media/game?search=", name,
                     "&limit=" STR(FLOPPY_LIBRARY_LIMIT), &buf, why,
                     why_size))) {
    free(buf);
    return MATCH_BAD_REPLY;
  }
  r = match_library(body, console_name, out);
  free(buf);
  explain(r,
          "Floppy's reply to the library search could not be read. It may "
          "have been cut short.",
          "Not in the library.",
          "More than one library entry could be this game. It needs a match "
          "on the config page.",
          why, why_size);
  return r;
}

enum match_result
floppy_find_saved(const struct config *cfg, const char *igdb_id,
                  const char *igdb_title, struct match *out, char *why,
                  size_t why_size) {
  char name[512];
  const char *body;
  char *buf;
  enum match_result r;

  if(url_encode(igdb_title, name, sizeof name)) {
    snprintf(why, why_size, "The saved title is too long to search for.");
    return MATCH_BAD_REPLY;
  }
  if(!(body = search(cfg, "/api/v1/media/game?search=", name,
                     "&limit=" STR(FLOPPY_LIBRARY_LIMIT), &buf, why,
                     why_size))) {
    free(buf);
    return MATCH_BAD_REPLY;
  }
  r = match_saved(body, igdb_id, out);
  free(buf);
  explain(r,
          "Floppy's reply to the library search could not be read. It may "
          "have been cut short.",
          "The matched entry is no longer in the library. It needs a match "
          "on the config page.",
          "The library has more than one entry for this game, or too many "
          "results to check. It needs a match on the config page.",
          why, why_size);
  return r;
}

enum match_result
floppy_find_on_igdb(const struct config *cfg, const char *console_name,
                    const char *title_id, struct match *out, char *why,
                    size_t why_size) {
  char name[512];
  const char *body;
  char *buf;
  enum match_result r;

  if(url_encode(console_name, name, sizeof name)) {
    snprintf(why, why_size, "The game name is too long to search for.");
    return MATCH_BAD_REPLY;
  }
  if(!(body = search(cfg, "/api/v1/search/game?search=", name,
                     "&source=igdb&limit=" STR(FLOPPY_IGDB_LIMIT), &buf, why,
                     why_size))) {
    free(buf);
    return MATCH_BAD_REPLY;
  }
  r = match_igdb(body, console_name, title_id, out);
  free(buf);
  explain(r,
          "Floppy's reply to the IGDB search could not be read. It may have "
          "been cut short.",
          "No IGDB game has exactly this name on this console. It needs a "
          "match on the config page.",
          "More than one IGDB game has exactly this name on this console. It "
          "needs a match on the config page.",
          why, why_size);
  return r;
}

/* IGDB IDs come from Floppy's own replies, but they go into a JSON body and
 * a URL, so only digits are let through. */
static int
plain_id(const char *id) {
  if(!*id || strlen(id) > 20) {
    return 0;
  }
  for(; *id; id++) {
    if(*id < '0' || *id > '9') return 0;
  }
  return 1;
}

/* Send one write. Returns the reply body for the caller to check, or NULL
 * with `why` set when no reply arrived. *buf must be freed by the caller. */
static const char*
send_write(const struct config *cfg, const char *method, const char *path,
           const char *body, int *status, char **buf, char *why,
           size_t why_size) {
  struct http_reply reply;

  *status = 0;
  if(!(*buf = malloc(FLOPPY_BUF))) {
    snprintf(why, why_size, "Out of memory.");
    return NULL;
  }
  if(http_request(cfg, method, path, 1, body, *buf, FLOPPY_BUF, &reply)) {
    snprintf(why, why_size, "No reply to the write, so it may or may not "
             "have been applied. It will be checked before sending again. "
             "%s", reply.error);
    return NULL;
  }
  *status = reply.status;
  return reply.body;
}

int
floppy_update(const struct config *cfg, const char *igdb_id, long total,
              char *why, size_t why_size) {
  char path[64], body[64];
  const char *reply;
  char *buf;
  int status, ok;

  if(!plain_id(igdb_id) || total < 1) {
    snprintf(why, why_size, "Refused to write: bad ID or total.");
    return 0;
  }
  snprintf(path, sizeof path, "/api/v1/media/game/igdb/%s", igdb_id);
  /* Plain minutes. This route stores the number as it is. */
  snprintf(body, sizeof body, "{\"progress\":%ld}", total);

  reply = send_write(cfg, "PATCH", path, body, &status, &buf, why, why_size);
  ok = reply && status == 200 && write_update_confirmed(reply, total);
  if(reply && !ok) {
    snprintf(why, why_size, "Floppy answered HTTP %d without confirming the "
             "new total. It will be checked before sending again.", status);
  }
  free(buf);
  return ok;
}

int
floppy_track(const struct config *cfg, const char *igdb_id, long minutes,
             char *why, size_t why_size) {
  char body[160];
  const char *reply;
  char *buf;
  int status, ok;

  if(!plain_id(igdb_id) || minutes < 1) {
    snprintf(why, why_size, "Refused to track: bad ID or minutes.");
    return 0;
  }
  /* This route reads `progress` through Floppy's duration field, where a
   * bare number means hours: "95" would be 95 hours. "95 minutes" is 95
   * minutes. PATCH, above, takes plain minutes. */
  snprintf(body, sizeof body,
           "{\"source\":\"igdb\",\"media_id\":\"%s\","
           "\"status\":\"In progress\",\"progress\":\"%ld minutes\"}",
           igdb_id, minutes);

  reply = send_write(cfg, "POST", "/api/v1/media/game", body, &status, &buf,
                     why, why_size);
  ok = reply && status == 201 && write_track_confirmed(reply, igdb_id, minutes);
  if(reply && !ok) {
    snprintf(why, why_size, "Floppy answered HTTP %d without confirming the "
             "new game. It will be checked before sending again.", status);
  }
  free(buf);
  return ok;
}
