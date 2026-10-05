#include "sync.h"
#include "floppy.h"
#include "writes.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Long enough for any note below with a full `why` inside. */
#define NOTE_MAX 512

/* One game to look at, copied out so the network calls run unlocked. */
struct job {
  char title_id[10];
  char name[128];
  char igdb_id[24];
  char igdb_title[128];
  long pending;
  char intent[8];
  long intent_from;
  long intent_to;
  long last_total;
};

static double
monotonic_seconds(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* A game on screen means play: no network work then. Caller holds the
 * lock. */
static int
playing(const struct app *app) {
  return app->now.title_id[0] && app->now.focused;
}

/* Copy Floppy's text into a games.tsv field, which cannot hold tabs or line
 * breaks. */
static void
copy_tsv(char *dst, size_t size, const char *src) {
  size_t n = 0;

  for(; *src && n + 1 < size; src++) {
    dst[n++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
  }
  dst[n] = 0;
}

/* ---- changes to the game table. Each takes the lock itself. ------------ */

/* Record where sync stands with a game. Logs only when that changes, so a
 * pass repeating every 15 minutes stays quiet. */
static void
set_state(struct app *app, const struct job *job, enum sync_state state,
          const char *note) {
  struct game *g;

  pthread_mutex_lock(&app->lock);
  if((g = store_game(&app->store, job->title_id, NULL))) {
    if(g->sync != state || strcmp(g->sync_note, note)) {
      app_log(app, "%s%s \"%s\": %s", state == SYNC_READY ? "dry-run: " : "",
              job->title_id, job->name, note);
    }
    g->sync = state;
    snprintf(g->sync_note, sizeof g->sync_note, "%s", note);
  }
  pthread_mutex_unlock(&app->lock);
}

/* Remember a library match, so later syncs find the game by its ID. */
static void
save_match(struct app *app, const struct job *job, const struct match *m) {
  struct game *g;

  pthread_mutex_lock(&app->lock);
  if((g = store_game(&app->store, job->title_id, NULL)) && !g->igdb_id[0]) {
    copy_tsv(g->igdb_id, sizeof g->igdb_id, m->igdb_id);
    copy_tsv(g->igdb_title, sizeof g->igdb_title, m->title);
    app->store.dirty = 1;
    app_log(app, "matched %s \"%s\" to %s (IGDB %s) in the library",
            job->title_id, job->name, m->title, m->igdb_id);
  }
  pthread_mutex_unlock(&app->lock);
}

/* Save a write before sending it, forced to disk. A track also saves the
 * game it is about to create, which settling needs to find it again.
 * Returns -1, with nothing changed, if it could not be saved: then the
 * write must not be sent. */
static int
save_intent(struct app *app, const struct job *job, const char *kind,
            long from, long to, const struct match *track) {
  struct game *g, before;
  int rc = -1;

  pthread_mutex_lock(&app->lock);
  if((g = store_game(&app->store, job->title_id, NULL))) {
    before = *g;
    snprintf(g->intent, sizeof g->intent, "%s", kind);
    g->intent_from = from;
    g->intent_to = to;
    if(track) {
      copy_tsv(g->igdb_id, sizeof g->igdb_id, track->igdb_id);
      copy_tsv(g->igdb_title, sizeof g->igdb_title, track->title);
    }
    if(!(rc = store_save(&app->store, app->store_path, 1))) {
      app_log(app, "about to %s %s \"%s\" (IGDB %s): %ld to %ld min", kind,
              job->title_id, job->name, g->igdb_id, from, to);
    } else {
      *g = before;
      app_log(app, "could not save before writing %s, nothing sent",
              job->title_id);
    }
  }
  pthread_mutex_unlock(&app->lock);
  return rc;
}

/* A write is confirmed: take its minutes off the waiting time and clear the
 * intent. Both change in the same save, so after a crash either both or
 * neither are on disk, and settling gets the same answer again. */
static void
apply_landed(struct app *app, const struct job *job, long from, long to) {
  struct game *g;

  pthread_mutex_lock(&app->lock);
  if((g = store_game(&app->store, job->title_id, NULL))) {
    g->pending -= (to - from) * 60;
    if(g->pending < 0) {
      g->pending = 0;
    }
    g->last_total = to;
    g->intent[0] = 0;
    g->intent_from = g->intent_to = 0;
    app->store.dirty = 1;
    if(store_save(&app->store, app->store_path, 0)) {
      app_log(app, "could not write %s", app->store_path);
    }
  }
  pthread_mutex_unlock(&app->lock);
}

/* ---- one game ----------------------------------------------------------- */

static void
report_failure(struct app *app, const struct job *job, enum match_result r,
               const char *why) {
  char note[NOTE_MAX];

  if(r == MATCH_BAD_REPLY) {
    snprintf(note, sizeof note, "Could not check Floppy, will try again. %s",
             why);
    set_state(app, job, SYNC_RETRY, note);
  } else {
    /* Each of these reasons already ends by saying it needs a match. */
    set_state(app, job, SYNC_NEEDS_MATCH, why);
  }
}

/* Send a write and settle it at once if the reply confirms it. */
static void
send_and_record(struct app *app, const struct config *cfg,
                const struct job *job, int track, const char *igdb_id,
                const char *title, long from, long to, const char *extra) {
  char note[NOTE_MAX], why[400];
  int ok;

  ok = track ? floppy_track(cfg, igdb_id, to, why, sizeof why)
             : floppy_update(cfg, igdb_id, to, why, sizeof why);
  if(ok) {
    apply_landed(app, job, from, to);
    if(track) {
      snprintf(note, sizeof note, "Added %s (IGDB %s) to your library with "
               "%ld min.%s", title, igdb_id, to, extra);
    } else {
      snprintf(note, sizeof note, "Sent %ld min to %s: %ld to %ld min.%s",
               to - from, title, from, to, extra);
    }
    set_state(app, job, SYNC_SENT, note);
  } else {
    snprintf(note, sizeof note, "Sending %ld min to %s was not confirmed. %s",
             to - from, title, why);
    set_state(app, job, SYNC_RETRY, note);
  }
}

/* A saved intent is settled before anything else is done for the game. */
static void
settle(struct app *app, const struct config *cfg, const struct job *job) {
  int track = !strcmp(job->intent, "track");
  char note[NOTE_MAX], why[400];
  enum match_result r;
  struct match m;

  memset(&m, 0, sizeof m);
  r = floppy_find_saved(cfg, job->igdb_id, job->igdb_title, &m, why,
                        sizeof why);
  switch(track ? settle_track(r, m.progress, job->intent_to)
               : settle_update(r, m.progress, job->intent_from,
                               job->intent_to)) {
  case SETTLE_LANDED:
    apply_landed(app, job, job->intent_from, job->intent_to);
    snprintf(note, sizeof note, "Sent %ld min to %s: %ld to %ld min, "
             "confirmed by reading it back.",
             job->intent_to - job->intent_from, job->igdb_title,
             job->intent_from, job->intent_to);
    set_state(app, job, SYNC_SENT, note);
    return;
  case SETTLE_UNKNOWN:
    snprintf(note, sizeof note, "A write is waiting to be checked, but "
             "Floppy could not be read. Will try again. %s", why);
    set_state(app, job, SYNC_RETRY, note);
    return;
  case SETTLE_ATTENTION:
    if(r == MATCH_FOUND) {
      snprintf(note, sizeof note, "Needs attention. The tracker wrote %ld to "
               "%ld min, but Floppy now shows %ld. Nothing more is sent for "
               "this game until the intent is cleared: stop the tracker, "
               "empty the intent columns in games.tsv, start it again.",
               job->intent_from, job->intent_to, m.progress);
    } else {
      snprintf(note, sizeof note, "Needs attention. A write of %ld to %ld min "
               "cannot be checked: the entry is gone or there are two. "
               "Nothing more is sent for this game until the intent is "
               "cleared: stop the tracker, empty the intent columns in "
               "games.tsv, start it again.", job->intent_from,
               job->intent_to);
    }
    set_state(app, job, SYNC_ATTENTION, note);
    return;
  case SETTLE_NOT_LANDED:
    break;
  }

  if(!cfg->send) {
    snprintf(note, sizeof note, "A write of %ld to %ld min did not reach "
             "Floppy, and sending is now off. Turn it on to finish it.",
             job->intent_from, job->intent_to);
    set_state(app, job, SYNC_RETRY, note);
    return;
  }
  /* Same write again, with the totals saved before. */
  send_and_record(app, cfg, job, track, job->igdb_id, job->igdb_title,
                  job->intent_from, job->intent_to, "");
}

/* The game has an entry: add the waiting minutes to it. */
static void
update(struct app *app, const struct config *cfg, const struct job *job,
       const struct match *m) {
  long minutes = job->pending / 60;
  long to = m->progress + minutes;
  char note[NOTE_MAX], warn[200] = "";

  /* Something else changed the total since the tracker last wrote it,
   * typically a Steam import in overwrite mode. Warn, then carry on. */
  if(job->last_total > 0 && m->progress != job->last_total) {
    snprintf(warn, sizeof warn, " Floppy's total changed from %ld to %ld "
             "since the tracker last wrote it. A Steam import in overwrite "
             "mode replaces PS5 minutes.", job->last_total, m->progress);
  }
  if(!cfg->send) {
    snprintf(note, sizeof note, "Would add %ld min to %s (IGDB %s): %ld to "
             "%ld min.%s", minutes, m->title, m->igdb_id, m->progress, to,
             warn);
    set_state(app, job, SYNC_READY, note);
    return;
  }
  if(save_intent(app, job, "update", m->progress, to, NULL)) {
    set_state(app, job, SYNC_RETRY, "Could not save to the console before "
              "writing, so nothing was sent. Will try again.");
    return;
  }
  send_and_record(app, cfg, job, 0, m->igdb_id, m->title, m->progress, to,
                  warn);
}

/* Not found in the library by the console's name. Track it only when IGDB
 * has exactly one game of that name on this console, the library does not
 * have it under IGDB's title either, and the first search returned nothing at
 * all. */
static void
new_game(struct app *app, const struct config *cfg, const struct job *job,
         long library_results) {
  long minutes = job->pending / 60;
  struct match found, lib;
  char note[NOTE_MAX], why[400];
  enum match_result r;

  memset(&found, 0, sizeof found);
  r = floppy_find_on_igdb(cfg, job->name, job->title_id, &found, why,
                          sizeof why);
  if(r != MATCH_FOUND) {
    report_failure(app, job, r, why);
    return;
  }

  /* Guard: is the IGDB game in the library under IGDB's own title? Then it
   * is a library match after all, and gets updated, not tracked again. */
  memset(&lib, 0, sizeof lib);
  r = floppy_find_saved(cfg, found.igdb_id, found.title, &lib, why,
                        sizeof why);
  if(r == MATCH_FOUND) {
    save_match(app, job, &lib);
    update(app, cfg, job, &lib);
    return;
  }
  if(r != MATCH_NONE) {
    report_failure(app, job, r, why);
    return;
  }

  /* Guard: the search by console name returned nothing at all. Any result,
   * even an unrelated one, could be this game under another name. */
  if(library_results > 0) {
    snprintf(note, sizeof note, "Not added: IGDB has %s (IGDB %s), but your "
             "library has entries with similar names, so it may already be "
             "there. It needs a match on the config page.", found.title,
             found.igdb_id);
    set_state(app, job, SYNC_NEEDS_MATCH, note);
    return;
  }

  if(!cfg->send) {
    snprintf(note, sizeof note, "Not in your library. Would add %s (IGDB %s) "
             "as In progress, with %ld min.", found.title, found.igdb_id,
             minutes);
    set_state(app, job, SYNC_READY, note);
    return;
  }
  if(save_intent(app, job, "track", 0, minutes, &found)) {
    set_state(app, job, SYNC_RETRY, "Could not save to the console before "
              "writing, so nothing was sent. Will try again.");
    return;
  }
  send_and_record(app, cfg, job, 1, found.igdb_id, found.title, 0, minutes,
                  "");
}

static void
sync_game(struct app *app, const struct config *cfg, const struct job *job) {
  char why[400];
  enum match_result r;
  struct match m;

  if(job->intent[0]) {
    settle(app, cfg, job);
    return;
  }

  memset(&m, 0, sizeof m);
  if(job->igdb_id[0]) {
    r = floppy_find_saved(cfg, job->igdb_id, job->igdb_title, &m, why,
                          sizeof why);
  } else {
    r = floppy_find_in_library(cfg, job->name, &m, why, sizeof why);
    if(r == MATCH_FOUND) {
      save_match(app, job, &m);
    } else if(r == MATCH_NONE) {
      new_game(app, cfg, job, m.results);
      return;
    }
  }
  if(r != MATCH_FOUND) {
    report_failure(app, job, r, why);
    return;
  }
  update(app, cfg, job, &m);
}

/* ---- the thread --------------------------------------------------------- */

/* Returns -1 if a game on screen stopped the pass before it finished, so
 * the caller runs it again as soon as play stops. */
static int
sync_pass(struct app *app) {
  static struct job jobs[STORE_MAX_GAMES];
  struct config cfg;
  int count = 0;

  pthread_mutex_lock(&app->lock);
  /* Until the tracker's first look, a game on screen would go unseen: the
   * payload can be launched in the middle of play. */
  if(!app->polled || playing(app)) {
    pthread_mutex_unlock(&app->lock);
    return -1;
  }
  if(!config_is_complete(&app->cfg)) {
    pthread_mutex_unlock(&app->lock);
    return 0;
  }
  cfg = app->cfg;
  for(int i = 0; i < app->store.count; i++) {
    const struct game *g = &app->store.games[i];
    struct job *j = &jobs[count];

    /* Never the game that is open: its time is still changing. A saved
     * intent is settled whatever the waiting time. Otherwise whole minutes
     * only. A game needing attention is only read, never written. */
    if(!strcmp(g->title_id, app->now.title_id) ||
       (!g->intent[0] && g->pending < 60)) {
      continue;
    }
    memcpy(j->title_id, g->title_id, sizeof j->title_id);
    memcpy(j->name, g->name, sizeof j->name);
    memcpy(j->igdb_id, g->igdb_id, sizeof j->igdb_id);
    memcpy(j->igdb_title, g->igdb_title, sizeof j->igdb_title);
    memcpy(j->intent, g->intent, sizeof j->intent);
    j->pending = g->pending;
    j->intent_from = g->intent_from;
    j->intent_to = g->intent_to;
    j->last_total = g->last_total;
    count++;
  }
  pthread_mutex_unlock(&app->lock);

  for(int i = 0; i < count; i++) {
    int stop;

    /* Stop at once if a game came on screen. The rest waits. */
    pthread_mutex_lock(&app->lock);
    stop = playing(app);
    pthread_mutex_unlock(&app->lock);
    if(stop) {
      return -1;
    }
    sync_game(app, &cfg, &jobs[i]);
  }
  return 0;
}

void*
sync_run(void *arg) {
  struct app *app = arg;
  double next = 0; /* first pass straight away: time may be left from before */
  int due = 0;     /* a pass was asked for and has not run yet */

  pthread_mutex_lock(&app->lock);
  app_log(app, "sync: sending to Floppy is %s",
          app->cfg.send ? "on" : "off (dry-run, nothing is written)");
  pthread_mutex_unlock(&app->lock);

  for(;;) {
    double t = monotonic_seconds();

    pthread_mutex_lock(&app->lock);
    due |= app->sync_wanted || t >= next;
    app->sync_wanted = 0;
    pthread_mutex_unlock(&app->lock);

    if(due && !sync_pass(app)) {
      due = 0;
      next = t + PSF_SYNC_SECONDS;
    }
    sleep(PSF_POLL_SECONDS);
  }
  return NULL;
}
