/* Host tests for src/match.c. Run with `make check`. */
#include "../src/match.h"
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

/* A library reply holding the given entries, each written as
 * {title, source, media_id, progress}. */
#define ENTRY(title, source, id, progress) \
  "{\"id\":1,\"item\":{\"media_id\":" id ",\"source\":\"" source "\"," \
  "\"title\":\"" title "\"},\"progress\":" progress "}"
#define LIBRARY(total, entries) \
  "{\"pagination\":{\"total\":" total "},\"results\":[" entries "]}"

static int
norm_is(const char *in, const char *want) {
  char out[128];

  return !match_normalise(in, out, sizeof out) && !strcmp(out, want);
}

static void
test_normalise(void) {
  char tiny[4];

  CHECK(norm_is("SILENT HILL f", "silent hill f"));
  CHECK(norm_is("  Ratchet & Clank:  Rift Apart ", "ratchet clank rift apart"));
  CHECK(norm_is("Marvel's Spider-Man 2", "marvels spider man 2"));
  CHECK(norm_is("Marvel\xe2\x80\x99s Spider-Man 2", "marvels spider man 2"));
  CHECK(norm_is("Call of Duty\xc2\xae: Black Ops", "call of duty black ops"));
  CHECK(norm_is("DEATH STRANDING\xe2\x84\xa2", "death stranding"));
  /* Other non-ASCII stays as it is, so it only matches itself. */
  CHECK(norm_is("\xc5\x8ckami HD", "\xc5\x8ckami hd"));
  CHECK(norm_is("---", ""));
  CHECK(match_normalise("SILENT", tiny, sizeof tiny) == -1);
}

static void
test_title(void) {
  const char *sh = "SILENT HILL f";

  CHECK(match_title(sh, "Silent Hill f") == MATCH_EXACT);
  CHECK(match_title(sh, "Silent Hill f: Deluxe Edition") == MATCH_EDITION);
  CHECK(match_title(sh, "Silent Hill f x Urban Myth Dissolution Center")
        == MATCH_NO);
  CHECK(match_title(sh, "Silent Hill") == MATCH_NO);
  CHECK(match_title("SILENT HILL", "Silent Hill f") == MATCH_NO);

  /* A colon in the console's own name. */
  CHECK(match_title("Ratchet & Clank: Rift Apart",
                    "Ratchet & Clank: Rift Apart") == MATCH_EXACT);
  CHECK(match_title("Ratchet & Clank: Rift Apart",
                    "Ratchet & Clank: Rift Apart: Digital Deluxe")
        == MATCH_EDITION);

  /* A known weakness: a name that is a prefix of another game matches it as
   * an edition. */
  CHECK(match_title("SILENT HILL", "Silent Hill: Townfall") == MATCH_EDITION);

  /* An empty or all-punctuation name matches nothing. */
  CHECK(match_title("", "Silent Hill f") == MATCH_NO);
  CHECK(match_title("::", ": Something") == MATCH_NO);
}

static void
test_platform(void) {
  CHECK(!strcmp(match_platform("PPSA21159"), "PlayStation 5"));
  CHECK(!strcmp(match_platform("CUSA00001"), "PlayStation 4"));
  CHECK(match_platform("NPXS40087") == NULL);
}

static void
test_library(void) {
  struct match m;

  /* A real library: the Deluxe Edition is the one entry, and it wins. */
  memset(&m, 0, sizeof m);
  CHECK(match_library(library_reply, "SILENT HILL f", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "347180"));
  CHECK(!strcmp(m.title, "Silent Hill f: Deluxe Edition"));
  CHECK(m.progress == 0);

  /* The ID as a number works too, and progress comes through. */
  CHECK(match_library(LIBRARY("1", ENTRY("Silent Hill f", "igdb", "222343",
                                         "95")),
                      "SILENT HILL f", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "222343") && m.progress == 95);

  /* An exact title wins over an edition, in either order. */
  CHECK(match_library(LIBRARY("2",
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "\"347180\"", "0") ","
          ENTRY("Silent Hill f", "igdb", "222343", "0")),
        "SILENT HILL f", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "222343"));
  CHECK(match_library(LIBRARY("2",
          ENTRY("Silent Hill f", "igdb", "222343", "0") ","
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "\"347180\"", "0")),
        "SILENT HILL f", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "222343"));

  /* Two of the same kind: wait for the user. */
  CHECK(match_library(LIBRARY("2",
          ENTRY("Silent Hill f", "igdb", "222343", "0") ","
          ENTRY("SILENT HILL F", "igdb", "1", "0")),
        "SILENT HILL f", &m) == MATCH_AMBIGUOUS);
  CHECK(match_library(LIBRARY("2",
          ENTRY("Silent Hill f: Day One Edition", "igdb", "370229", "0") ","
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "\"347180\"", "0")),
        "SILENT HILL f", &m) == MATCH_AMBIGUOUS);

  /* A non-candidate beside the candidate does not get in the way. */
  CHECK(match_library(LIBRARY("2",
          ENTRY("Silent Hill f x Urban Myth Dissolution Center", "igdb",
                "417146", "0") ","
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "\"347180\"", "7")),
        "SILENT HILL f", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "347180") && m.progress == 7);

  /* Nothing matching, read in full: the only result that allows tracking,
   * and only when there were no results at all. */
  CHECK(match_library(LIBRARY("0", ""), "SILENT HILL f", &m) == MATCH_NONE);
  CHECK(m.results == 0);
  CHECK(match_library(LIBRARY("1", ENTRY("Silent Hill 2", "igdb", "1", "0")),
                      "SILENT HILL f", &m) == MATCH_NONE);
  CHECK(m.results == 1);

  /* More results than were sent: another candidate could be among them. */
  CHECK(match_library(LIBRARY("30", ENTRY("Silent Hill 2", "igdb", "1", "0")),
                      "SILENT HILL f", &m) == MATCH_AMBIGUOUS);
  CHECK(match_library("{\"results\":[]}", "SILENT HILL f", &m)
        == MATCH_AMBIGUOUS);

  /* A candidate that cannot be written through the IGDB route. */
  CHECK(match_library(LIBRARY("1", ENTRY("Silent Hill f", "manual", "\"9\"",
                                         "0")),
                      "SILENT HILL f", &m) == MATCH_AMBIGUOUS);
  CHECK(match_library(LIBRARY("1", ENTRY("Silent Hill f", "igdb", "\"\"",
                                         "0")),
                      "SILENT HILL f", &m) == MATCH_AMBIGUOUS);
  CHECK(match_library(LIBRARY("1", ENTRY("Silent Hill f", "igdb", "1",
                                         "null")),
                      "SILENT HILL f", &m) == MATCH_AMBIGUOUS);

  /* Broken replies are never read as "not in the library". */
  {
    char cut[sizeof library_reply];

    memcpy(cut, library_reply, sizeof library_reply);
    cut[sizeof library_reply - 10] = 0;
    CHECK(match_library(cut, "SILENT HILL f", &m) == MATCH_BAD_REPLY);
  }
  CHECK(match_library("", "SILENT HILL f", &m) == MATCH_BAD_REPLY);
  CHECK(match_library("{\"detail\":\"Invalid token.\"}", "SILENT HILL f", &m)
        == MATCH_BAD_REPLY);
  CHECK(match_library("{\"results\":{}}", "SILENT HILL f", &m)
        == MATCH_BAD_REPLY);
  CHECK(match_library(LIBRARY("1", "{\"id\":1}"), "SILENT HILL f", &m)
        == MATCH_BAD_REPLY);
}

static void
test_saved(void) {
  struct match m;

  /* Found by ID, whatever the title says now. */
  memset(&m, 0, sizeof m);
  CHECK(match_saved(library_reply, "347180", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "347180") && m.progress == 0);
  CHECK(!strcmp(m.title, "Silent Hill f: Deluxe Edition"));

  /* The base game added later does not take the time away from Deluxe. */
  CHECK(match_saved(LIBRARY("2",
          ENTRY("Silent Hill f", "igdb", "222343", "0") ","
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "\"347180\"", "12")),
        "347180", &m) == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "347180") && m.progress == 12);

  /* Gone from the library: reported, never re-matched. */
  CHECK(match_saved(LIBRARY("1", ENTRY("Silent Hill f", "igdb", "222343",
                                       "0")),
                    "347180", &m) == MATCH_NONE);
  CHECK(match_saved(LIBRARY("0", ""), "347180", &m) == MATCH_NONE);

  /* Not all results were sent, so "gone" cannot be told. */
  CHECK(match_saved(LIBRARY("30", ENTRY("Silent Hill f", "igdb", "222343",
                                        "0")),
                    "347180", &m) == MATCH_AMBIGUOUS);

  /* Two entries for the same game: which one to write is not known. */
  CHECK(match_saved(LIBRARY("2",
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "347180", "3") ","
          ENTRY("Silent Hill f: Deluxe Edition", "igdb", "\"347180\"", "9")),
        "347180", &m) == MATCH_AMBIGUOUS);

  /* Same number from another provider is not the same game. */
  CHECK(match_saved(LIBRARY("1", ENTRY("Something", "tmdb", "347180", "0")),
                    "347180", &m) == MATCH_NONE);

  CHECK(match_saved(library_reply, "", &m) == MATCH_BAD_REPLY);
  CHECK(match_saved("{\"results\":[", "347180", &m) == MATCH_BAD_REPLY);
}

static void
test_igdb(void) {
  struct match m;

  /* The real search: only the base game is an exact title on PS5. */
  memset(&m, 0, sizeof m);
  CHECK(match_igdb(search_reply, "SILENT HILL f", "PPSA21159", &m)
        == MATCH_FOUND);
  CHECK(!strcmp(m.igdb_id, "222343"));
  CHECK(!strcmp(m.title, "Silent Hill f"));

  /* Same name on PS4: no result lists PS4, so nothing is tracked. */
  CHECK(match_igdb(search_reply, "SILENT HILL f", "CUSA00001", &m)
        == MATCH_NONE);

  /* Not a game. */
  CHECK(match_igdb(search_reply, "SILENT HILL f", "NPXS40087", &m)
        == MATCH_NONE);

  /* Editions never count here, even alone. */
  CHECK(match_igdb("{\"pagination\":{\"total\":1},\"results\":["
                   "{\"media_id\":347180,\"title\":\"Silent Hill f: Deluxe "
                   "Edition\",\"platforms\":[\"PlayStation 5\"]}]}",
                   "SILENT HILL f", "PPSA21159", &m) == MATCH_NONE);

  /* Two exact titles on the platform: wait for the user. */
  CHECK(match_igdb("{\"pagination\":{\"total\":2},\"results\":["
                   "{\"media_id\":1,\"title\":\"Doom\","
                   "\"platforms\":[\"PlayStation 5\"]},"
                   "{\"media_id\":2,\"title\":\"DOOM\","
                   "\"platforms\":[\"PlayStation 5\"]}]}",
                   "DOOM", "PPSA00001", &m) == MATCH_AMBIGUOUS);

  CHECK(match_igdb("{\"results\":", "SILENT HILL f", "PPSA21159", &m)
        == MATCH_BAD_REPLY);
}

int
main(void) {
  test_normalise();
  test_title();
  test_platform();
  test_library();
  test_saved();
  test_igdb();
  if(failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("match: all passed\n");
  return 0;
}
