/* Host tests for src/floppy.c, through the real HTTP client, against
 * tests/mock_floppy.py. Run with `make check`, which starts the mock. */
#include "../src/config.h"
#include "../src/floppy.h"
#include "../src/http_client.h"
#include "../src/json.h"
#include "../src/writes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if(!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while(0)

/* Tell the mock how to treat the next write, or start it clean. */
static void
mock(const struct config *cfg, const char *path, const char *body) {
  static char buf[4096];
  struct http_reply reply;

  if(http_request(cfg, "POST", path, 0, body, buf, sizeof buf, &reply) ||
     reply.status != 200) {
    printf("mock control %s failed\n", path);
    exit(2);
  }
}

/* The current total of a tracked game, through the saved lookup. */
static enum match_result
total_of(const struct config *cfg, const char *id, const char *title,
         long *total) {
  struct match m;
  char why[400];
  enum match_result r;

  memset(&m, 0, sizeof m);
  r = floppy_find_saved(cfg, id, title, &m, why, sizeof why);
  *total = m.progress;
  return r;
}

/* The status Floppy holds for a game, read straight from a library search. */
static int
status_is(const struct config *cfg, const char *search, const char *want) {
  static char buf[64 * 1024];
  struct http_reply reply;
  char path[256], status[32];

  snprintf(path, sizeof path, "/api/v1/media/game?search=%s&limit=10", search);
  return !http_request(cfg, "GET", path, 1, NULL, buf, sizeof buf, &reply) &&
         !json_valid(reply.body) &&
         !json_string(json_member(json_first(json_member(reply.body,
                                                         "results")),
                                  "status"), status, sizeof status) &&
         !strcmp(status, want);
}

static void
test_writes(const struct config *cfg) {
  const char *deluxe = "Silent Hill f: Deluxe Edition";
  enum match_result r;
  char why[400];
  long t;

  mock(cfg, "/mock/reset", "{}");

  /* A plain update, confirmed by the reply. */
  CHECK(floppy_update(cfg, "347180", 50, why, sizeof why) == 1);
  CHECK(total_of(cfg, "347180", deluxe, &t) == MATCH_FOUND && t == 50);

  /* Applied, then the connection dropped: not confirmed. Reading first
   * shows it landed, so it is not sent again. */
  mock(cfg, "/mock/mode", "{\"mode\":\"drop\"}");
  CHECK(floppy_update(cfg, "347180", 60, why, sizeof why) == 0);
  CHECK(strstr(why, "may or may not") != NULL);
  r = total_of(cfg, "347180", deluxe, &t);
  CHECK(settle_update(r, t, 50, 60) == SETTLE_LANDED);

  /* Applied, then HTTP 500 from the metadata fetch. Same outcome. */
  mock(cfg, "/mock/mode", "{\"mode\":\"fail_after\"}");
  CHECK(floppy_update(cfg, "347180", 70, why, sizeof why) == 0);
  CHECK(strstr(why, "HTTP 500") != NULL);
  r = total_of(cfg, "347180", deluxe, &t);
  CHECK(settle_update(r, t, 60, 70) == SETTLE_LANDED);

  /* Refused before applying: reading shows the old total, send again. */
  mock(cfg, "/mock/mode", "{\"mode\":\"refuse\"}");
  CHECK(floppy_update(cfg, "347180", 80, why, sizeof why) == 0);
  r = total_of(cfg, "347180", deluxe, &t);
  CHECK(settle_update(r, t, 70, 80) == SETTLE_NOT_LANDED);
  CHECK(floppy_update(cfg, "347180", 80, why, sizeof why) == 1);

  /* Not tracked: 404, never created. */
  CHECK(floppy_update(cfg, "555", 5, why, sizeof why) == 0);
  CHECK(total_of(cfg, "555", "Fresh Game", &t) == MATCH_NONE);

  /* Nothing odd gets into the URL or body. */
  CHECK(floppy_update(cfg, "347180/../1", 5, why, sizeof why) == 0);
  CHECK(floppy_update(cfg, "347180", 0, why, sizeof why) == 0);

  /* Track: minutes arrive as minutes, not hours. */
  CHECK(floppy_track(cfg, "555", 10, why, sizeof why) == 1);
  CHECK(total_of(cfg, "555", "Fresh Game", &t) == MATCH_FOUND && t == 10);
  CHECK(status_is(cfg, "Fresh%20Game", "In progress"));

  /* Track applied, then dropped. Reading shows it is there: not sent
   * again. */
  mock(cfg, "/mock/mode", "{\"mode\":\"drop\"}");
  CHECK(floppy_track(cfg, "556", 25, why, sizeof why) == 0);
  r = total_of(cfg, "556", "Second Game", &t);
  CHECK(settle_track(r, t, 25) == SETTLE_LANDED);

  /* Why that matters: sending it blindly makes a second entry, which the
   * mock allows just like Floppy. Settling then asks for the user. */
  CHECK(floppy_track(cfg, "556", 25, why, sizeof why) == 1);
  r = total_of(cfg, "556", "Second Game", &t);
  CHECK(r == MATCH_AMBIGUOUS);
  CHECK(settle_track(r, t, 25) == SETTLE_ATTENTION);
}

int
main(int argc, char **argv) {
  struct config cfg, bad;
  struct match m;
  char url[64], err[200], why[400];

  if(argc != 2) {
    printf("usage: floppy-test <mock port>\n");
    return 2;
  }
  memset(&cfg, 0, sizeof cfg);
  snprintf(url, sizeof url, "http://127.0.0.1:%s", argv[1]);
  if(config_set_url(&cfg, url, err, sizeof err) ||
     config_set_token(&cfg, "good-token", err, sizeof err)) {
    printf("config: %s\n", err);
    return 2;
  }
  bad = cfg;
  config_set_token(&bad, "wrong-token", err, sizeof err);
  mock(&cfg, "/mock/reset", "{}");

  /* The real case end to end: the console name finds the Deluxe entry. */
  memset(&m, 0, sizeof m);
  CHECK(floppy_find_in_library(&cfg, "SILENT HILL f", &m, why, sizeof why)
        == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "347180") && m.progress == 42);

  /* A later sync finds it again through the saved title and ID. */
  memset(&m, 0, sizeof m);
  CHECK(floppy_find_saved(&cfg, "347180", "Silent Hill f: Deluxe Edition",
                          &m, why, sizeof why) == MATCH_FOUND);
  CHECK(m.progress == 42);

  /* Encoding: space, "&", ":" and a UTF-8 trade mark arrive intact. The
   * mock only answers if they decode to exactly this name. */
  CHECK(floppy_find_in_library(&cfg, "Ratchet & Clank: Rift Apart\xe2\x84\xa2",
                               &m, why, sizeof why) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "284716"));

  /* A chunked reply is decoded. */
  CHECK(floppy_find_in_library(&cfg, "CHUNKED", &m, why, sizeof why)
        == MATCH_FOUND);

  /* Not in the library, so the IGDB search runs: the base game. */
  CHECK(floppy_find_in_library(&cfg, "NEW GAME", &m, why, sizeof why)
        == MATCH_NONE);
  memset(&m, 0, sizeof m);
  CHECK(floppy_find_on_igdb(&cfg, "SILENT HILL f", "PPSA21159", &m, why,
                            sizeof why) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "222343"));
  CHECK(floppy_find_on_igdb(&cfg, "NEW GAME", "PPSA00001", &m, why,
                            sizeof why) == MATCH_NONE);
  CHECK(strstr(why, "needs a match") != NULL);

  /* The saved entry is gone. */
  CHECK(floppy_find_saved(&cfg, "347180", "NEW GAME", &m, why, sizeof why)
        == MATCH_NONE);
  CHECK(strstr(why, "no longer in the library") != NULL);

  /* Failures are never "not in the library". */
  CHECK(floppy_find_in_library(&bad, "SILENT HILL f", &m, why, sizeof why)
        == MATCH_BAD_REPLY);
  CHECK(strstr(why, "rejected the API token") != NULL);
  CHECK(floppy_find_in_library(&cfg, "BOOM", &m, why, sizeof why)
        == MATCH_BAD_REPLY);
  CHECK(strstr(why, "HTTP 500") != NULL);
  CHECK(floppy_find_in_library(&cfg, "HUGE", &m, why, sizeof why)
        == MATCH_BAD_REPLY);
  CHECK(strstr(why, "too big") != NULL);
  CHECK(floppy_find_in_library(&cfg, "CUT", &m, why, sizeof why)
        == MATCH_BAD_REPLY);

  /* Nobody listening. */
  {
    struct config off = cfg;
    config_set_url(&off, "http://127.0.0.1:1", err, sizeof err);
    CHECK(floppy_find_in_library(&off, "SILENT HILL f", &m, why, sizeof why)
          == MATCH_BAD_REPLY);
    CHECK(why[0] != 0);
  }

  test_writes(&cfg);

  if(failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("floppy: all passed\n");
  return 0;
}
