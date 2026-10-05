/* Desktop stand-in, so the config page can be developed in a normal browser
 * without a console. Built by `make host`. */
#include "platform.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void
plat_init(void) {
  signal(SIGPIPE, SIG_IGN);
}

void
plat_notify(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  printf("[notify] ");
  vprintf(fmt, ap);
  printf("\n");
  va_end(ap);
  fflush(stdout);
}

/* Pretend a game is running by writing one line to .host-data/fake-game:
 *
 *     PPSA21159 1 SILENT HILL f
 *
 * title ID, 1 for on screen or 0 for minimized, then the name. Delete the
 * file, or empty it, to close the game. */
static int
read_fake(char title_id[10], int *focused, char *name, size_t size) {
  char line[256];
  char id[16] = "";
  int focus = 0;
  int offset = 0;
  FILE *f = fopen(".host-data/fake-game", "r");

  if(!f) {
    return 0;
  }
  line[0] = 0;
  if(!fgets(line, sizeof line, f)) {
    line[0] = 0;
  }
  fclose(f);
  line[strcspn(line, "\r\n")] = 0;

  if(sscanf(line, "%15s %d %n", id, &focus, &offset) < 2 || strlen(id) != 9) {
    return 0;
  }
  memcpy(title_id, id, 10);
  *focused = focus != 0;
  if(name) {
    snprintf(name, size, "%s", offset ? line + offset : id);
  }
  return 1;
}

int
plat_current_game(char title_id[10], int *focused) {
  title_id[0] = 0;
  *focused = 0;
  return read_fake(title_id, focused, NULL, 0);
}

int
plat_game_name(const char *title_id, char *out, size_t size) {
  char id[10];
  int focused;

  if(read_fake(id, &focused, out, size) && !strcmp(id, title_id)) {
    return 0;
  }
  return -1;
}

const char*
plat_data_dir(void) {
  return ".host-data";
}
