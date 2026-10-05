/* The calls sync makes to Floppy. The reads hand each reply to the rules in
 * match.h. The two writes report only whether Floppy's reply confirmed them.
 *
 * Every call may block for the network timeout. Never call one while holding
 * app.lock. */
#pragma once

#include "config.h"
#include "match.h"

#include <stddef.h>

/* Results asked for per library search. An entry is about 3 KB and the
 * reply buffer is 64 KB. */
#define FLOPPY_LIBRARY_LIMIT 10

/* Results asked for per IGDB search. Each is a few hundred bytes. */
#define FLOPPY_IGDB_LIMIT 10

/* Each returns a match_result. Anything but MATCH_FOUND leaves a sentence for
 * the log in `why`. A failed request is MATCH_BAD_REPLY, so it is retried
 * and never read as "not in the library". */

/* First match: search the library by the console's name. */
enum match_result floppy_find_in_library(const struct config *cfg,
                                         const char *console_name,
                                         struct match *out,
                                         char *why, size_t why_size);

/* Later syncs: search the library by the saved Floppy title, then keep only
 * the entry with the saved IGDB ID. */
enum match_result floppy_find_saved(const struct config *cfg,
                                    const char *igdb_id,
                                    const char *igdb_title,
                                    struct match *out,
                                    char *why, size_t why_size);

/* Only after floppy_find_in_library returned MATCH_NONE: search IGDB for a
 * game to track. */
enum match_result floppy_find_on_igdb(const struct config *cfg,
                                      const char *console_name,
                                      const char *title_id,
                                      struct match *out,
                                      char *why, size_t why_size);

/* The writes. Each returns 1 only when Floppy's reply confirms the new
 * total. 0 means "not confirmed", never "not applied": Floppy can apply a
 * write and still time out or answer an error, so the caller must settle a
 * saved intent by reading before sending again. `why` says what happened. */

/* Set the total of the tracked game `igdb_id` to `total` minutes. */
int floppy_update(const struct config *cfg, const char *igdb_id, long total,
                  char *why, size_t why_size);

/* Track `igdb_id` as a new game, In progress, with `minutes` played. Floppy
 * does not check for an existing entry, so call this only after the library
 * has been searched by both names and holds nothing that could be it. */
int floppy_track(const struct config *cfg, const char *igdb_id, long minutes,
                 char *why, size_t why_size);
