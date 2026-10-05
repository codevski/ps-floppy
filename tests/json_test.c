/* Host tests for src/json.c. Run with `make check`.
 *
 * Real reply shapes from fixtures.h, plus broken documents. */
#include "../src/json.h"
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
test_search_reply(void) {
  const char *results, *r, *v;
  char title[128];
  long id;
  int n = 0;

  CHECK(!json_valid(search_reply));
  results = json_member(search_reply, "results");
  CHECK(results != NULL);

  r = json_first(results);
  CHECK(r != NULL);
  CHECK(!json_long(json_member(r, "media_id"), &id) && id == 222343);
  CHECK(!json_string(json_member(r, "title"), title, sizeof title));
  CHECK(!strcmp(title, "Silent Hill f"));

  /* The last platform of the first result. */
  for(v = json_first(json_member(r, "platforms")); v; v = json_next(v)) {
    CHECK(!json_string(v, title, sizeof title));
  }
  CHECK(!strcmp(title, "PlayStation 5"));

  for(r = json_first(results); r; r = json_next(r)) {
    n++;
  }
  CHECK(n == 5);
}

static void
test_library_reply(void) {
  const char *entry, *item;
  char text[64];
  long n;

  CHECK(!json_valid(library_reply));
  entry = json_first(json_member(library_reply, "results"));
  CHECK(entry != NULL);
  CHECK(json_next(entry) == NULL);

  CHECK(!json_long(json_member(entry, "progress"), &n) && n == 0);
  CHECK(!json_long(json_member(entry, "id"), &n) && n == 35614);

  /* Only the entry's own `source`, not the one inside `item`. */
  CHECK(!json_string(json_member(entry, "source"), text, sizeof text));
  CHECK(!strcmp(text, "PlayStation"));

  item = json_member(entry, "item");
  CHECK(!json_string(json_member(item, "media_id"), text, sizeof text));
  CHECK(!strcmp(text, "347180"));
  CHECK(!json_string(json_member(item, "source"), text, sizeof text));
  CHECK(!strcmp(text, "igdb"));

  /* A string is not a number, and a fraction is not an integer. */
  CHECK(json_long(json_member(item, "media_id"), &n) == -1);
  CHECK(json_long(json_member(item, "provider_rating"), &n) == -1);

  CHECK(json_member(entry, "nope") == NULL);
  CHECK(json_member(json_member(entry, "nope"), "deeper") == NULL);
  CHECK(json_first(NULL) == NULL && json_next(NULL) == NULL);
  CHECK(json_skip(NULL) == NULL && json_valid(NULL) == -1);
  CHECK(json_string(NULL, text, sizeof text) == -1);
  CHECK(json_long(NULL, &n) == -1);
  CHECK(json_first(json_member(entry, "lists")) == NULL);
}

static void
test_broken(void) {
  char cut[sizeof library_reply];
  long n;

  /* A reply cut short, as by a full client buffer, must not look valid. */
  memcpy(cut, library_reply, sizeof library_reply);
  cut[sizeof library_reply / 2] = 0;
  CHECK(json_valid(cut) == -1);

  CHECK(json_valid("") == -1);
  CHECK(json_valid("{\"a\":1} x") == -1);
  CHECK(json_valid("{\"a\":1,}") == -1);
  CHECK(json_valid("[1 2]") == -1);
  CHECK(json_valid("{\"a\" 1}") == -1);
  CHECK(json_valid("tru") == -1);
  CHECK(json_valid("01x") == -1);
  CHECK(json_valid("1.") == -1);
  CHECK(json_valid(" {\"a\":[true,false,null,-1.5e+3,\"\\u00d5\"]} ") == 0);

  /* Nesting past the limit is refused, not followed. */
  {
    char deep[200];
    int i;

    for(i = 0; i < 99; i++) deep[i] = '[';
    for(; i < 198; i++) deep[i] = ']';
    deep[i] = 0;
    CHECK(json_valid(deep) == -1);
  }

  CHECK(!json_long("-9223372036854775808", &n) && n < 0);
  CHECK(json_long("9223372036854775808", &n) == -1);
  CHECK(!json_long("9223372036854775807", &n) && n > 0);
}

static void
test_strings(void) {
  char out[16];

  /* UTF-8 passes through, escapes are decoded. */
  CHECK(!json_string("\"\xc5\x8ckami HD\"", out, sizeof out));
  CHECK(!strcmp(out, "\xc5\x8ckami HD"));
  CHECK(!json_string("\"\\u014ckami\"", out, sizeof out));
  CHECK(!strcmp(out, "\xc5\x8ckami"));

  /* Too long for the buffer is an error, never a cut-off title. */
  CHECK(json_string("\"Silent Hill f: Deluxe Edition\"", out, sizeof out)
        == -1);
  CHECK(json_string("42", out, sizeof out) == -1);
}

int
main(void) {
  test_search_reply();
  test_library_reply();
  test_broken();
  test_strings();
  if(failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("json: all passed\n");
  return 0;
}
