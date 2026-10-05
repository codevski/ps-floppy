/* Decisions around writing playtime to Floppy, kept apart from the network
 * so every case runs in tests.
 *
 * Every write is saved as an intent before it is sent. A write that Floppy
 * applies but the console never records (a crash, a timeout, or an error
 * reply after Floppy already saved it) would otherwise be sent twice. So an
 * unconfirmed intent is settled by reading Floppy's total first: the new
 * total means it landed, the old total means send it again, anything else
 * stops. */
#pragma once

#include "match.h"

enum settle {
  SETTLE_LANDED,      /* Floppy shows the new total: count it as sent */
  SETTLE_NOT_LANDED,  /* Floppy shows the old total: send the same write */
  SETTLE_ATTENTION,   /* something else changed it: write nothing */
  SETTLE_UNKNOWN,     /* could not read Floppy: decide later */
};

/* A saved "update" intent, from -> to, against what the saved lookup found
 * now. `found` is the result of match_saved for the entry. */
enum settle settle_update(enum match_result found, long now_total,
                          long from, long to);

/* A saved "track" intent of `to` minutes, against a library search for the
 * tracked game's ID. MATCH_NONE from a reply read in full means it was
 * never created. */
enum settle settle_track(enum match_result found, long now_total, long to);

/* Whether a reply to PATCH /api/v1/media/game/igdb/<id> confirms the entry
 * now holds `total`. It must have exactly one entry in `consumptions`. */
int write_update_confirmed(const char *reply, long total);

/* Whether a reply to POST /api/v1/media/game confirms a new entry for
 * `igdb_id` holding `total`. */
int write_track_confirmed(const char *reply, const char *igdb_id, long total);
