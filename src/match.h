/* Deciding which Floppy entry a console game belongs to.
 *
 * Pure functions over the text of Floppy's replies, so every rule here runs
 * and is tested on the desktop.
 *
 * The user's own library wins, in any edition: an IGDB search alone would
 * pick the base game and create a second entry beside the edition the user
 * already has. Anything unclear waits for the user instead of guessing. */
#pragma once

#include <stddef.h>

enum match_kind {
  MATCH_NO,       /* a different game */
  MATCH_EXACT,    /* same title */
  MATCH_EDITION,  /* same title followed by ": subtitle" */
};

enum match_result {
  MATCH_FOUND,      /* exactly one usable entry, in `struct match` */
  MATCH_NONE,       /* the reply was read in full and nothing matched */
  MATCH_AMBIGUOUS,  /* more than one candidate, or one that cannot be used */
  MATCH_BAD_REPLY,  /* malformed or cut short: not evidence of anything */
};

struct match {
  char igdb_id[24];
  char title[128];  /* Floppy's title, for the log and the page */
  long progress;    /* minutes already in Floppy. Library matches only */
  long results;     /* how many results the reply held, matching or not.
                       Set for every outcome except MATCH_BAD_REPLY */
};

/* Lower-case ASCII letters and digits separated by single spaces. Apostrophes
 * and the marks (TM) (R) (C) are dropped, other punctuation separates words,
 * and other non-ASCII bytes are kept as they are. Returns 0, or -1 if it does
 * not fit. */
int match_normalise(const char *in, char *out, size_t size);

/* How Floppy's `title` relates to the console's game name. */
enum match_kind match_title(const char *console_name, const char *title);

/* IGDB's platform name for a console title ID, or NULL if it is not a PS5
 * (PPSA) or PS4 (CUSA) game. */
const char *match_platform(const char *title_id);

/* Pick the entry from a reply to GET /api/v1/media/game?search=<name>. A
 * single exact title wins. Failing that, a single edition. Two of the same
 * kind wait for the user. */
enum match_result match_library(const char *reply, const char *console_name,
                                struct match *out);

/* Find a game matched before, by its saved IGDB ID, in a reply to
 * GET /api/v1/media/game?search=<saved Floppy title>. No title rules run
 * again, so a later edition or a renamed entry cannot move the time.
 * MATCH_NONE means the entry is gone. It waits for the user and must never
 * lead to tracking the game again. */
enum match_result match_saved(const char *reply, const char *igdb_id,
                              struct match *out);

/* Pick the game to track from a reply to GET /api/v1/search/game. Only an
 * exact title on the console's platform counts. Use only after
 * match_library returned MATCH_NONE. */
enum match_result match_igdb(const char *reply, const char *console_name,
                             const char *title_id, struct match *out);
