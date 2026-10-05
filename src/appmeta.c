#include "appmeta.h"
#include "json.h"

#include <string.h>

/* The `titleName` of one language block, if it is there and not empty. */
static int
title_in(const char *localized, const char *lang, char *out, size_t size) {
  return json_string(json_member(json_member(localized, lang), "titleName"),
                     out, size) || !out[0] ? -1 : 0;
}

/* The first "titleName" anywhere in the text. What 0.1.3 and earlier did,
 * kept for a file the JSON reader rejects. */
static int
first_title(const char *json, char *out, size_t size) {
  const char *p = strstr(json, "\"titleName\"");
  size_t i = 0;

  if(!p || !(p = strchr(p + 11, ':')) || !(p = strchr(p, '"'))) {
    return -1;
  }
  for(p++; *p && *p != '"' && i + 1 < size; p++) {
    if(*p == '\\' && p[1]) {
      p++;
    }
    out[i++] = *p;
  }
  out[i] = 0;
  return i ? 0 : -1;
}

int
appmeta_title(const char *param_json, char *out, size_t size) {
  const char *localized;
  char lang[16];

  if(!json_valid(param_json) &&
     (localized = json_member(param_json, "localizedParameters"))) {
    if(!title_in(localized, "en-US", out, size)) {
      return 0;
    }
    if(!json_string(json_member(localized, "defaultLanguage"), lang,
                    sizeof lang) &&
       !title_in(localized, lang, out, size)) {
      return 0;
    }
    if(!title_in(localized, "en-GB", out, size)) {
      return 0;
    }
  }
  return first_title(param_json, out, size);
}
