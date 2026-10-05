/* Host tests for src/writes.c and the new games.tsv columns in src/store.c.
 * Run with `make check`. */
#include "../src/config.h"
#include "../src/store.h"
#include "../src/writes.h"
#include "fixtures.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if(!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while(0)

static void
test_settle(void) {
  /* Update 0 -> 95, then Floppy shows: */
  CHECK(settle_update(MATCH_FOUND, 95, 0, 95) == SETTLE_LANDED);
  CHECK(settle_update(MATCH_FOUND, 0, 0, 95) == SETTLE_NOT_LANDED);
  CHECK(settle_update(MATCH_FOUND, 60, 0, 95) == SETTLE_ATTENTION);
  CHECK(settle_update(MATCH_FOUND, 190, 0, 95) == SETTLE_ATTENTION);
  CHECK(settle_update(MATCH_BAD_REPLY, 0, 0, 95) == SETTLE_UNKNOWN);
  CHECK(settle_update(MATCH_NONE, 0, 0, 95) == SETTLE_ATTENTION);
  CHECK(settle_update(MATCH_AMBIGUOUS, 95, 0, 95) == SETTLE_ATTENTION);

  /* Track with 10 minutes, then the library search by ID shows: */
  CHECK(settle_track(MATCH_FOUND, 10, 10) == SETTLE_LANDED);
  CHECK(settle_track(MATCH_NONE, 0, 10) == SETTLE_NOT_LANDED);
  CHECK(settle_track(MATCH_FOUND, 0, 10) == SETTLE_ATTENTION);
  CHECK(settle_track(MATCH_AMBIGUOUS, 10, 10) == SETTLE_ATTENTION);
  CHECK(settle_track(MATCH_BAD_REPLY, 0, 10) == SETTLE_UNKNOWN);
}

static void
test_confirm(void) {
  char two[] =
    "{\"consumptions\":[{\"progress\":95},{\"progress\":95}]}";

  /* The real no-op reply confirms 0 and nothing else. */
  CHECK(write_update_confirmed(patch_reply, 0));
  CHECK(!write_update_confirmed(patch_reply, 95));
  CHECK(write_update_confirmed("{\"consumptions\":[{\"progress\":95}]}", 95));

  /* Two entries: which one changed is not known, so not confirmed. */
  CHECK(!write_update_confirmed(two, 95));
  CHECK(!write_update_confirmed("{\"consumptions\":[]}", 95));
  CHECK(!write_update_confirmed("{\"detail\":\"Not found.\"}", 95));
  CHECK(!write_update_confirmed("{\"consumptions\":[{\"progress\":95}]", 95));
  CHECK(!write_update_confirmed("", 95));

  /* Track replies come in the library entry shape. */
  CHECK(write_track_confirmed("{\"id\":1,\"item\":{\"media_id\":\"555\"},"
                              "\"progress\":10}", "555", 10));
  CHECK(write_track_confirmed("{\"id\":1,\"item\":{\"media_id\":555},"
                              "\"progress\":10}", "555", 10));
  CHECK(!write_track_confirmed("{\"id\":1,\"item\":{\"media_id\":\"556\"},"
                               "\"progress\":10}", "555", 10));
  CHECK(!write_track_confirmed("{\"id\":1,\"item\":{\"media_id\":\"555\"},"
                               "\"progress\":0}", "555", 10));
  CHECK(!write_track_confirmed("{\"id\":1,\"progress\":10}", "555", 10));
  CHECK(!write_track_confirmed("{\"detail\":\"Invalid media data.\"}", "555",
                               10));
}

static void
test_store(void) {
  static struct store st;
  const char *path = "build/writes-test.tsv";
  struct game *g;
  FILE *f;

  /* A 0.1.2 file, five columns, loads with no intent. */
  f = fopen(path, "w");
  fputs("PPSA21159\t5707\tSILENT HILL f\t347180\tSilent Hill f: Deluxe "
        "Edition\n", f);
  fclose(f);
  CHECK(!store_load(&st, path));
  CHECK(st.count == 1);
  g = &st.games[0];
  CHECK(g->pending == 5707 && !strcmp(g->igdb_id, "347180"));
  CHECK(!g->intent[0] && g->last_total == 0);

  /* Round trip with an intent and a last total. */
  memcpy(g->intent, "update", 7);
  g->intent_from = 0;
  g->intent_to = 95;
  g->last_total = 40;
  CHECK(!store_save(&st, path, 1));
  CHECK(!store_load(&st, path));
  g = &st.games[0];
  CHECK(!strcmp(g->intent, "update"));
  CHECK(g->intent_from == 0 && g->intent_to == 95 && g->last_total == 40);
  CHECK(!strcmp(g->igdb_title, "Silent Hill f: Deluxe Edition"));

  /* Cleared again, saved without fsync. */
  g->intent[0] = 0;
  CHECK(!store_save(&st, path, 0));
  CHECK(!store_load(&st, path));
  CHECK(!st.games[0].intent[0] && st.games[0].last_total == 40);

  /* A damaged intent is dropped rather than acted on. */
  f = fopen(path, "w");
  fputs("PPSA21159\t60\tX\t1\tX\tupdate\t95\t10\t\n"
        "PPSA00001\t60\tY\t2\tY\tdelete\t0\t10\t\n"
        "PPSA00002\t60\tZ\t3\tZ\ttrack\t0\t10\t-5\n", f);
  fclose(f);
  CHECK(!store_load(&st, path) && st.count == 3);
  CHECK(!st.games[0].intent[0]);
  CHECK(!st.games[1].intent[0]);
  CHECK(!strcmp(st.games[2].intent, "track") && st.games[2].intent_to == 10);
  CHECK(st.games[2].last_total == 0);
  remove(path);
}

/* The send switch: on only when the file says exactly true. */
static int
send_after_loading(const char *text) {
  const char *path = "build/writes-test.json";
  struct config cfg;
  FILE *f = fopen(path, "w");

  fputs(text, f);
  fclose(f);
  config_load(&cfg, path);
  remove(path);
  return cfg.send;
}

static void
test_send_setting(void) {
  struct config cfg;

  CHECK(send_after_loading("{\"floppy_url\":\"http://1.2.3.4:8000\","
                           "\"token\":\"abcdefgh\","
                           "\"send_to_floppy\": true}") == 1);
  CHECK(send_after_loading("{\"floppy_url\":\"http://1.2.3.4:8000\","
                           "\"send_to_floppy\":false}") == 0);
  /* A 0.1.2 file has no switch: off. */
  CHECK(send_after_loading("{\"floppy_url\":\"http://1.2.3.4:8000\","
                           "\"token\":\"abcdefgh\"}") == 0);
  /* Damaged files never turn it on. */
  CHECK(send_after_loading("{\"send_to_floppy\":true") == 0);
  CHECK(send_after_loading("{\"send_to_floppy\":\"true\"}") == 0);
  CHECK(send_after_loading("{\"other\":{\"send_to_floppy\":true}}") == 0);

  /* Saved and loaded again. */
  memset(&cfg, 0, sizeof cfg);
  config_set_url(&cfg, "http://1.2.3.4:8000", NULL, 0);
  cfg.send = 1;
  CHECK(!config_save(&cfg, "build/writes-test.json"));
  memset(&cfg, 0, sizeof cfg);
  config_load(&cfg, "build/writes-test.json");
  CHECK(cfg.send == 1 && !strcmp(cfg.host, "1.2.3.4"));
  remove("build/writes-test.json");
}

int
main(void) {
  test_send_setting();
  test_settle();
  test_confirm();
  test_store();
  if(failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("writes: all passed\n");
  return 0;
}
