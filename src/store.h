/* The tracker's own state on disk: one row per game the console has seen,
 * with the playtime that has not been sent to Floppy yet.
 *
 * Because Floppy keeps a single running total per game, "what is still owed"
 * is all that has to survive a restart. Unsent time is simply seconds per
 * game, so sessions never need to be queued one by one. */
#pragma once

#include <stddef.h>

#define STORE_MAX_GAMES 256

/* Where sync stands with a game. Kept in memory only: after a restart every
 * game is looked up again, which gives the same answer. */
enum sync_state {
  SYNC_NOT_YET,      /* no attempt since start */
  SYNC_READY,        /* matched, sending off: the write it would make */
  SYNC_NEEDS_MATCH,  /* no match, or more than one: waits for the user */
  SYNC_RETRY,        /* Floppy could not be read: tried again later */
  SYNC_SENT,         /* the last write was confirmed */
  SYNC_ATTENTION,    /* a write's outcome conflicts with Floppy: stopped */
};

struct game {
  char title_id[10];   /* console ID, e.g. PPSA21159 */
  char name[128];      /* name read from the console */
  long pending;        /* seconds played and not yet sent to Floppy */
  char igdb_id[24];    /* saved library match, empty until one is found */
  char igdb_title[128]; /* Floppy's title for it, searched on later syncs */

  /* A write to Floppy, saved before it is sent so that a crash or a timeout
   * can never send the same minutes twice (see writes.h). An empty `intent`
   * means none. */
  char intent[8];       /* "update" or "track" */
  long intent_from;     /* Floppy's total before the write, in minutes */
  long intent_to;       /* the total the write sets */
  long last_total;      /* last total the tracker wrote, 0 if none yet. A
                           write always sets at least 1 minute */

  /* Not saved to disk. */
  enum sync_state sync;
  char sync_note[512]; /* the last outcome, for the page and the log */
};

struct store {
  struct game games[STORE_MAX_GAMES];
  int         count;
  int         dirty;   /* changed since the last save */
};

/* Missing file is not an error. */
int store_load(struct store *st, const char *path);

/* Written to a temporary file and renamed. Clears `dirty` on success.
 * `durable` also forces it to disk first: required before a write to
 * Floppy is sent. */
int store_save(struct store *st, const char *path, int durable);

/* Find a game, adding it if new. Returns NULL only when the table is full. */
struct game *store_game(struct store *st, const char *title_id,
                        const char *name);
