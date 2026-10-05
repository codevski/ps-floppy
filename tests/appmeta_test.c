/* Host tests for src/appmeta.c. Run with `make check`. */
#include "../src/appmeta.h"
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

static int
title_is(const char *json, const char *want) {
  char out[128];

  return !appmeta_title(json, out, sizeof out) && !strcmp(out, want);
}

int
main(void) {
  /* The fixture reproduces the bug: its first name is the Arabic one,
   * which is what 0.1.3 sent to Floppy's search. */
  CHECK(strstr(wolverine_param, "\"titleName\": \"\xd9\x88") ==
        strstr(wolverine_param, "\"titleName\""));

  /* The real files. */
  CHECK(title_is(wolverine_param, "Marvel's Wolverine"));
  CHECK(title_is(silenthill_param, "SILENT HILL f"));

  /* No en-US: the game's default language wins over the first name. */
  CHECK(title_is("{\"localizedParameters\":{\"de-DE\":{\"titleName\":\"A\"},"
                 "\"defaultLanguage\":\"ja-JP\","
                 "\"ja-JP\":{\"titleName\":\"B\"}}}", "B"));

  /* No en-US and no usable default: en-GB. */
  CHECK(title_is("{\"localizedParameters\":{\"ar-AE\":{\"titleName\":\"A\"},"
                 "\"defaultLanguage\":\"fr-FR\","
                 "\"en-GB\":{\"titleName\":\"C\"}}}", "C"));

  /* An empty en-US name does not count. */
  CHECK(title_is("{\"localizedParameters\":{\"defaultLanguage\":\"de-DE\","
                 "\"de-DE\":{\"titleName\":\"D\"},"
                 "\"en-US\":{\"titleName\":\"\"}}}", "D"));

  /* Nothing better: the first name, as before. */
  CHECK(title_is("{\"localizedParameters\":{\"ar-AE\":{\"titleName\":\"E\"}}}",
                 "E"));

  /* A file the JSON reader rejects still gives the first name. */
  CHECK(title_is("{\"localizedParameters\":{\"en-US\":{\"titleName\":\"F\"}",
                 "F"));

  /* No name at all. */
  {
    char out[16];
    CHECK(appmeta_title("{\"titleId\":\"PPSA00001\"}", out, sizeof out) == -1);
    CHECK(appmeta_title("", out, sizeof out) == -1);
  }

  if(failures) {
    printf("%d failed\n", failures);
    return 1;
  }
  printf("appmeta: all passed\n");
  return 0;
}
