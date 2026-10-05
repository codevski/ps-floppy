#include "tracker.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"

static double
monotonic_seconds(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Close the session for the game in app->now. Caller holds the lock. */
static void
end_session(struct app *app) {
  struct now_playing *now = &app->now;
  struct game *g;

  if(!now->title_id[0]) {
    return;
  }
  g = store_game(&app->store, now->title_id, now->name);

  if(now->session < PSF_MIN_SESSION_SECONDS) {
    /* Too short to count. Take back what this session added. */
    if(g) {
      g->pending -= now->session;
      if(g->pending < 0) {
        g->pending = 0;
      }
      app->store.dirty = 1;
    }
    app_log(app, "closed %s \"%s\" after %lds: under %ds, not counted",
            now->title_id, now->name, now->session, PSF_MIN_SESSION_SECONDS);
  } else {
    app_log(app, "closed %s \"%s\": session %lds, %lds waiting to send",
            now->title_id, now->name, now->session, g ? g->pending : 0);
    app->sync_wanted = 1;
  }
  memset(now, 0, sizeof *now);
}

void
tracker_run(struct app *app) {
  double last_poll = monotonic_seconds();
  double last_save = last_poll;
  double carry = 0;         /* fraction of a second not yet counted */
  int was_counting = 0;     /* previous poll saw the same game on screen */

  for(;;) {
    char title_id[10];
    char name[128];
    int focused = 0;
    int running;
    int is_new;
    double t, delta;

    sleep(PSF_POLL_SECONDS);

    running = plat_current_game(title_id, &focused);
    t = monotonic_seconds();
    delta = t - last_poll;
    last_poll = t;

    /* A gap far longer than the poll interval means the console slept (rest
     * mode) or the process was stalled. That time was not play. */
    if(delta > 3.0 * PSF_POLL_SECONDS || delta < 0) {
      pthread_mutex_lock(&app->lock);
      app_log(app, "gap of %.0fs between polls: not counted", delta);
      pthread_mutex_unlock(&app->lock);
      delta = 0;
      was_counting = 0;
    }

    /* The name comes from a file read: fetch it before taking the lock, and
     * only when the game changes. */
    pthread_mutex_lock(&app->lock);
    is_new = running && strcmp(title_id, app->now.title_id);
    pthread_mutex_unlock(&app->lock);
    name[0] = 0;
    if(is_new && plat_game_name(title_id, name, sizeof name)) {
      snprintf(name, sizeof name, "%s", title_id);
    }

    pthread_mutex_lock(&app->lock);
    app->polled = 1;

    /* A different game, or none: the previous session is over. This also
     * covers switching straight from one game to another. */
    if(app->now.title_id[0] && (!running || is_new)) {
      end_session(app);
      was_counting = 0;
      carry = 0;
    }

    if(running) {
      struct game *g;

      if(is_new) {
        memcpy(app->now.title_id, title_id, sizeof app->now.title_id);
        snprintf(app->now.name, sizeof app->now.name, "%s", name);
        app->now.session = 0;
        app_log(app, "started %s \"%s\"%s", title_id, name,
                focused ? "" : " (minimized)");
      }
      if(focused != app->now.focused && !is_new) {
        app_log(app, "%s %s", app->now.title_id,
                focused ? "back on screen" : "minimized, clock paused");
      }
      app->now.focused = focused;

      /* Count the interval only if the game was on screen at both ends. */
      if(focused && was_counting &&
         (g = store_game(&app->store, app->now.title_id, app->now.name))) {
        long whole;

        carry += delta;
        whole = (long)carry;
        carry -= (double)whole;
        app->now.session += whole;
        g->pending += whole;
        app->store.dirty = 1;
      }
      was_counting = focused;
    }

    /* Checkpoint while playing, and save at once when a session ends. */
    if(app->store.dirty &&
       (!app->now.title_id[0] || t - last_save >= PSF_CHECKPOINT_SECONDS)) {
      if(store_save(&app->store, app->store_path, 0)) {
        app_log(app, "could not write %s", app->store_path);
      }
      last_save = t;
    }

    pthread_mutex_unlock(&app->lock);
  }
}
