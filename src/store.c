#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Fields are tab separated, one game per line, so names must not carry
 * either character. */
static void
copy_field(char *dst, size_t size, const char *src) {
  size_t n = 0;

  for(; *src && n + 1 < size; src++) {
    dst[n++] = (*src == '\t' || *src == '\n' || *src == '\r') ? ' ' : *src;
  }
  dst[n] = 0;
}

/* Split `line` at the next tab. Returns the field and advances `*line`. */
static char*
next_field(char **line) {
  char *field = *line;
  char *tab;

  if(!field) {
    return "";
  }
  if((tab = strchr(field, '\t'))) {
    *tab = 0;
    *line = tab + 1;
  } else {
    *line = NULL;
  }
  return field;
}

int
store_load(struct store *st, const char *path) {
  char line[512];
  FILE *f;

  memset(st, 0, sizeof *st);
  if(!(f = fopen(path, "rb"))) {
    return 0;
  }
  while(st->count < STORE_MAX_GAMES && fgets(line, sizeof line, f)) {
    struct game *g = &st->games[st->count];
    char *rest = line;
    char *title_id;

    line[strcspn(line, "\r\n")] = 0;
    title_id = next_field(&rest);
    if(strlen(title_id) != 9) {
      continue; /* blank or damaged line */
    }
    copy_field(g->title_id, sizeof g->title_id, title_id);
    g->pending = strtol(next_field(&rest), NULL, 10);
    if(g->pending < 0) {
      g->pending = 0;
    }
    copy_field(g->name, sizeof g->name, next_field(&rest));
    copy_field(g->igdb_id, sizeof g->igdb_id, next_field(&rest));
    copy_field(g->igdb_title, sizeof g->igdb_title, next_field(&rest));

    /* Added in 0.1.3. Older files end here and load with no intent. */
    copy_field(g->intent, sizeof g->intent, next_field(&rest));
    g->intent_from = strtol(next_field(&rest), NULL, 10);
    g->intent_to = strtol(next_field(&rest), NULL, 10);
    g->last_total = strtol(next_field(&rest), NULL, 10);
    if(strcmp(g->intent, "update") && strcmp(g->intent, "track")) {
      g->intent[0] = 0;
    }
    if(!g->intent[0] || g->intent_from < 0 || g->intent_to <= g->intent_from) {
      g->intent[0] = 0;
      g->intent_from = g->intent_to = 0;
    }
    if(g->last_total < 0) {
      g->last_total = 0;
    }
    st->count++;
  }
  fclose(f);
  return 0;
}

int
store_save(struct store *st, const char *path, int durable) {
  char tmp[512];
  int failed;
  FILE *f;

  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  if(!(f = fopen(tmp, "wb"))) {
    return -1;
  }
  for(int i = 0; i < st->count; i++) {
    const struct game *g = &st->games[i];
    fprintf(f, "%s\t%ld\t%s\t%s\t%s\t%s\t", g->title_id, g->pending,
            g->name, g->igdb_id, g->igdb_title, g->intent);
    if(g->intent[0]) {
      fprintf(f, "%ld\t%ld\t", g->intent_from, g->intent_to);
    } else {
      fputs("\t\t", f);
    }
    if(g->last_total > 0) {
      fprintf(f, "%ld", g->last_total);
    }
    fputc('\n', f);
  }
  /* An intent must be on disk before its write is sent, not just in the
   * page cache, or a crash could lose it after Floppy applied the write.
   * fsync is not proven on the console yet, so ordinary checkpoints skip it
   * and only a failed write-ahead save is affected if it does not work. */
  failed = fflush(f) || (durable && fsync(fileno(f)));
  if(fclose(f) || failed || rename(tmp, path)) {
    unlink(tmp);
    return -1;
  }
  st->dirty = 0;
  return 0;
}

struct game*
store_game(struct store *st, const char *title_id, const char *name) {
  struct game *g;

  for(int i = 0; i < st->count; i++) {
    if(!strcmp(st->games[i].title_id, title_id)) {
      g = &st->games[i];
      /* Keep the name fresh: a game update can change it. */
      if(name && name[0] && strcmp(g->name, name)) {
        copy_field(g->name, sizeof g->name, name);
        st->dirty = 1;
      }
      return g;
    }
  }
  if(st->count >= STORE_MAX_GAMES) {
    return NULL;
  }
  g = &st->games[st->count++];
  memset(g, 0, sizeof *g);
  copy_field(g->title_id, sizeof g->title_id, title_id);
  copy_field(g->name, sizeof g->name, name && name[0] ? name : title_id);
  st->dirty = 1;
  return g;
}
