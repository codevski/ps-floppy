#include "match.h"
#include "json.h"

#include <stdio.h>
#include <string.h>

/* Longest title compared. Anything longer is treated as a bad reply rather
 * than compared cut short. */
#define MATCH_TITLE_MAX 256

/* UTF-8 sequences dropped by match_normalise. Trade marks are common in
 * console names, curly apostrophes in IGDB titles. */
static const char *const dropped[] = {
  "\xe2\x84\xa2",  /* trade mark */
  "\xc2\xae",      /* registered */
  "\xc2\xa9",      /* copyright */
  "\xe2\x80\x99",  /* right single quote */
  "\xe2\x80\x98",  /* left single quote */
};

int
match_normalise(const char *in, char *out, size_t size) {
  size_t n = 0;
  int gap = 0;

  if(!size) {
    return -1;
  }
  while(*in) {
    unsigned char c = (unsigned char)*in;
    size_t skip = 0;

    for(size_t i = 0; i < sizeof dropped / sizeof dropped[0]; i++) {
      size_t len = strlen(dropped[i]);
      if(!strncmp(in, dropped[i], len)) {
        skip = len;
        break;
      }
    }
    if(skip) {
      in += skip;
      continue;
    }
    in++;
    if(c == '\'') {
      continue;
    }
    if(c < 0x80 && !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z'))) {
      gap = n > 0;
      continue;
    }
    if(gap) {
      if(n + 1 >= size) return -1;
      out[n++] = ' ';
      gap = 0;
    }
    if(c >= 'A' && c <= 'Z') {
      c = (unsigned char)(c - 'A' + 'a');
    }
    if(n + 1 >= size) return -1;
    out[n++] = (char)c;
  }
  out[n] = 0;
  return 0;
}

enum match_kind
match_title(const char *console_name, const char *title) {
  char want[MATCH_TITLE_MAX], have[MATCH_TITLE_MAX];
  const char *colon;

  if(match_normalise(console_name, want, sizeof want) || !want[0] ||
     match_normalise(title, have, sizeof have)) {
    return MATCH_NO;
  }
  if(!strcmp(want, have)) {
    return MATCH_EXACT;
  }

  /* Try the title cut at each colon, so a console name that has a colon of
   * its own ("Ratchet & Clank: Rift Apart") still finds its editions. */
  for(colon = strchr(title, ':'); colon; colon = strchr(colon + 1, ':')) {
    char head[MATCH_TITLE_MAX];
    size_t len = (size_t)(colon - title);

    if(len >= sizeof head) break;
    memcpy(head, title, len);
    head[len] = 0;
    if(!match_normalise(head, have, sizeof have) && !strcmp(want, have)) {
      return MATCH_EDITION;
    }
  }
  return MATCH_NO;
}

const char*
match_platform(const char *title_id) {
  if(!strncmp(title_id, "PPSA", 4)) {
    return "PlayStation 5";
  }
  if(!strncmp(title_id, "CUSA", 4)) {
    /* Assumed. Not yet seen in a live reply. */
    return "PlayStation 4";
  }
  return NULL;
}

static void
copy_title(char *out, size_t size, const char *in) {
  size_t len = strlen(in);

  if(len >= size) len = size - 1;
  memcpy(out, in, len);
  out[len] = 0;
}

/* IGDB IDs arrive as a number from the search and as a string from the
 * library. Either way, as text. */
static int
read_id(const char *v, char *out, size_t size) {
  long id;

  if(!v) {
    return -1;
  }
  if(!json_string(v, out, size)) {
    return out[0] ? 0 : -1;
  }
  if(json_long(v, &id) || id <= 0) {
    return -1;
  }
  return snprintf(out, size, "%ld", id) < (int)size ? 0 : -1;
}

/* Results array of a valid paged reply, whether every result is in it, and
 * how many it holds. */
static const char*
results_of(const char *reply, int *complete, long *count) {
  const char *results, *r;
  long total, seen = 0;

  if(json_valid(reply) || !(results = json_member(reply, "results"))) {
    return NULL;
  }
  if(*results != '[') {
    return NULL;
  }
  for(r = json_first(results); r; r = json_next(r)) {
    seen++;
  }
  *complete = !json_long(json_member(json_member(reply, "pagination"),
                                     "total"), &total) && total <= seen;
  *count = seen;
  return results;
}

/* Fill `out` from a library entry already known to be the one. Returns
 * MATCH_FOUND, or MATCH_AMBIGUOUS if it cannot be written through IGDB. */
static enum match_result
use_entry(const char *entry, struct match *out) {
  const char *item = json_member(entry, "item");
  char title[MATCH_TITLE_MAX], source[16];

  if(json_string(json_member(item, "source"), source, sizeof source) ||
     strcmp(source, "igdb") ||
     read_id(json_member(item, "media_id"), out->igdb_id,
             sizeof out->igdb_id) ||
     json_long(json_member(entry, "progress"), &out->progress) ||
     out->progress < 0 ||
     json_string(json_member(item, "title"), title, sizeof title)) {
    return MATCH_AMBIGUOUS;
  }
  copy_title(out->title, sizeof out->title, title);
  return MATCH_FOUND;
}

enum match_result
match_library(const char *reply, const char *console_name,
              struct match *out) {
  const char *results, *entry, *found;
  const char *exact = NULL, *edition = NULL;
  int complete, exacts = 0, editions = 0;
  char title[MATCH_TITLE_MAX];

  if(!(results = results_of(reply, &complete, &out->results))) {
    return MATCH_BAD_REPLY;
  }
  for(entry = json_first(results); entry; entry = json_next(entry)) {
    const char *item = json_member(entry, "item");

    if(!item || json_string(json_member(item, "title"), title, sizeof title)) {
      return MATCH_BAD_REPLY;
    }
    switch(match_title(console_name, title)) {
    case MATCH_EXACT:
      exacts++;
      exact = entry;
      break;
    case MATCH_EDITION:
      editions++;
      edition = entry;
      break;
    case MATCH_NO:
      break;
    }
  }

  /* Results we were not sent could hold another candidate. Retrying would
   * get the same page, so this waits for the user like any unclear match. */
  if(!complete) {
    return MATCH_AMBIGUOUS;
  }

  /* An exact title wins over editions. Two of the same kind is a tie. */
  if(exacts) {
    if(exacts > 1) return MATCH_AMBIGUOUS;
    found = exact;
  } else if(editions) {
    if(editions > 1) return MATCH_AMBIGUOUS;
    found = edition;
  } else {
    return MATCH_NONE;
  }

  /* One candidate. It is only usable if it can be written through the IGDB
   * route. Anything else waits for the user, and is never tracked again. */
  return use_entry(found, out);
}

enum match_result
match_saved(const char *reply, const char *igdb_id, struct match *out) {
  const char *results, *entry, *found = NULL;
  int complete, count = 0;
  char id[24], source[16];

  if(!igdb_id[0] || !(results = results_of(reply, &complete, &out->results))) {
    return MATCH_BAD_REPLY;
  }
  for(entry = json_first(results); entry; entry = json_next(entry)) {
    const char *item = json_member(entry, "item");

    if(!item) {
      return MATCH_BAD_REPLY;
    }
    if(!json_string(json_member(item, "source"), source, sizeof source) &&
       !strcmp(source, "igdb") &&
       !read_id(json_member(item, "media_id"), id, sizeof id) &&
       !strcmp(id, igdb_id)) {
      count++;
      found = entry;
    }
  }

  /* Two entries for one game are separate plays in Floppy. Which one a write
   * lands on is not known, so the user decides. */
  if(count > 1) {
    return MATCH_AMBIGUOUS;
  }
  if(!count) {
    return complete ? MATCH_NONE : MATCH_AMBIGUOUS;
  }
  return use_entry(found, out);
}

enum match_result
match_igdb(const char *reply, const char *console_name, const char *title_id,
           struct match *out) {
  const char *results, *r, *found = NULL;
  const char *platform = match_platform(title_id);
  int complete, candidates = 0;
  char title[MATCH_TITLE_MAX];

  /* `complete` is not checked here. A further exact match beyond the page
   * is unlikely, and the library check has already ruled out a duplicate. */
  if(!(results = results_of(reply, &complete, &out->results))) {
    return MATCH_BAD_REPLY;
  }
  if(!platform) {
    return MATCH_NONE;
  }
  for(r = json_first(results); r; r = json_next(r)) {
    const char *p;
    int on_platform = 0;

    if(json_string(json_member(r, "title"), title, sizeof title)) {
      return MATCH_BAD_REPLY;
    }
    if(match_title(console_name, title) != MATCH_EXACT) {
      continue;
    }
    for(p = json_first(json_member(r, "platforms")); p; p = json_next(p)) {
      char name[64];
      if(!json_string(p, name, sizeof name) && !strcmp(name, platform)) {
        on_platform = 1;
      }
    }
    if(on_platform) {
      candidates++;
      found = r;
    }
  }
  if(candidates > 1) {
    return MATCH_AMBIGUOUS;
  }
  if(!candidates) {
    return MATCH_NONE;
  }
  if(read_id(json_member(found, "media_id"), out->igdb_id,
             sizeof out->igdb_id)) {
    return MATCH_BAD_REPLY;
  }
  json_string(json_member(found, "title"), title, sizeof title);
  copy_title(out->title, sizeof out->title, title);
  out->progress = 0;
  return MATCH_FOUND;
}
