/* ps-floppy: tracks PS5 playtime into a self-hosted Floppy instance.
 *
 * The background process. It names itself, replaces any older copy, serves
 * the config page, counts the time each game spends on screen, and sends it
 * to Floppy once the user turns sending on.
 */

/* Must come first: the FreeBSD headers below do not include it themselves.
 * Kept in its own block so include-sorting formatters cannot move it. */
// clang-format off
#include <sys/types.h>
// clang-format on

#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "http_server.h"
#include "platform.h"
#include "sync.h"
#include "tracker.h"

#define LOG_MAX_BYTES (512 * 1024)

void
app_log(struct app *app, const char *fmt, ...) {
  char when[32];
  time_t now = time(NULL);
  struct tm tm;
  va_list ap;
  FILE *f;

  if(!(f = fopen(app->log_path, "a"))) {
    return;
  }
  localtime_r(&now, &tm);
  strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
  fprintf(f, "%s  ", when);
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

/* Keep the log from growing without bound: start a fresh one when it is
 * large, keeping the previous file once. */
static void
rotate_log(const char *path) {
  struct stat st;
  char old[300];

  if(!stat(path, &st) && st.st_size > LOG_MAX_BYTES) {
    snprintf(old, sizeof old, "%s.old", path);
    rename(path, old);
  }
}

/* The console's own LAN address, for telling the user where the page is.
 * Connecting a UDP socket sends nothing; it only makes the system pick the
 * local address it would use. Returns 0 on success. */
static int
local_ip(const char *toward, char *out, size_t size) {
  struct sockaddr_in addr;
  socklen_t len = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  int rc = -1;

  if(fd < 0) {
    return -1;
  }
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  if(inet_pton(AF_INET, toward, &addr.sin_addr) == 1 &&
     !connect(fd, (struct sockaddr*)&addr, sizeof addr) &&
     !getsockname(fd, (struct sockaddr*)&addr, &len) &&
     inet_ntop(AF_INET, &addr.sin_addr, out, (socklen_t)size)) {
    rc = 0;
  }
  close(fd);
  return rc;
}

int
main(void) {
  static struct app app;
  pthread_t server, syncer;
  char ip[64];

  plat_init();

  app.started = time(NULL);
  pthread_mutex_init(&app.lock, NULL);

  mkdir(plat_data_dir(), 0700);
  snprintf(app.config_path, sizeof app.config_path, "%s/config.json",
           plat_data_dir());
  snprintf(app.store_path, sizeof app.store_path, "%s/games.tsv",
           plat_data_dir());
  snprintf(app.log_path, sizeof app.log_path, "%s/ps-floppy.log",
           plat_data_dir());
  rotate_log(app.log_path);
  app_log(&app, "==== ps-floppy " PSF_VERSION " started ====");

  store_load(&app.store, app.store_path);

  /* Names are read when a game starts, so a stored name can be stale: before
   * 0.1.4 it could be the wrong language, and a game update can rename it.
   * Read them all again. Only files, no network. */
  for(int i = 0; i < app.store.count; i++) {
    struct game *g = &app.store.games[i];
    char name[sizeof g->name];

    if(!plat_game_name(g->title_id, name, sizeof name) &&
       strcmp(name, g->name)) {
      app_log(&app, "%s is now named \"%s\" (was \"%s\")", g->title_id, name,
              g->name);
      store_game(&app.store, g->title_id, name);
    }
  }
  if(config_load(&app.cfg, app.config_path)) {
    plat_notify("ps-floppy: the saved settings could not be read and were "
                "ignored. Open the config page to enter them again.");
  }

  if(pthread_create(&server, NULL, http_server_run, &app)) {
    plat_notify("ps-floppy " PSF_VERSION " could not start its config page.");
    return 1;
  }
  if(pthread_create(&syncer, NULL, sync_run, &app)) {
    /* Counting still works without it, and the time stays waiting. */
    app_log(&app, "could not start the sync thread");
  }

  /* Prefer the route toward Floppy; fall back to any public address, which
   * works whenever the console has a default route. */
  if((app.cfg.host[0] && !local_ip(app.cfg.host, ip, sizeof ip)) ||
     !local_ip("1.1.1.1", ip, sizeof ip)) {
    plat_notify("ps-floppy " PSF_VERSION " running. %s http://%s:%d",
                config_is_complete(&app.cfg) ? "Config page:" :
                "Set it up from a phone or computer at", ip, PSF_HTTP_PORT);
  } else {
    plat_notify("ps-floppy " PSF_VERSION " running. %s port %d of this "
                "console's IP address.",
                config_is_complete(&app.cfg) ? "Config page is on" :
                "Set it up from a phone or computer on", PSF_HTTP_PORT);
  }

  tracker_run(&app);
  return 0;
}
