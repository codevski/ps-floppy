/* State shared between the config web server, the tracker and sync. */
#pragma once

#include "config.h"
#include "store.h"

#include <pthread.h>
#include <time.h>

#ifndef PSF_VERSION
#define PSF_VERSION "dev"
#endif

/* Port of the config page. Chosen to stay clear of the usual PS5 homebrew
 * ports (1337 and 2121 ftp, 8080 websrv, 8084 Payload Manager, 9021 elfldr). */
#define PSF_HTTP_PORT 8765

/* How often the tracker looks at what is running. Two tiny system calls. */
#ifndef PSF_POLL_SECONDS
#define PSF_POLL_SECONDS 5
#endif

/* Sessions shorter than this are discarded: a launch by mistake, a crash. */
#ifndef PSF_MIN_SESSION_SECONDS
#define PSF_MIN_SESSION_SECONDS 120
#endif

/* How often unsent playtime is written to disk while a game is running. The
 * most that is lost if the process is killed. */
#ifndef PSF_CHECKPOINT_SECONDS
#define PSF_CHECKPOINT_SECONDS 60
#endif

/* How often sync looks again at games with time waiting, on top of a look
 * each time a session ends. */
#ifndef PSF_SYNC_SECONDS
#define PSF_SYNC_SECONDS 900
#endif

/* What the tracker sees right now, for the config page. */
struct now_playing {
  char title_id[10];   /* empty when no game is running */
  char name[128];
  int  focused;        /* 0 while the game is minimized */
  long session;        /* seconds counted this session */
};

struct app {
  pthread_mutex_t    lock;    /* guards everything below */
  struct config      cfg;
  struct store       store;
  struct now_playing now;
  char               config_path[256];
  char               store_path[256];
  char               log_path[256];
  time_t             started;
  int                sync_wanted; /* set when a session ends */
  int                polled;      /* the tracker has looked at least once, so
                                     `now` can be trusted */
};

/* Append a timestamped line to the tracker's log file. */
void app_log(struct app *app, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
