#include "writes.h"
#include "json.h"

#include <stdio.h>
#include <string.h>

enum settle
settle_update(enum match_result found, long now_total, long from, long to) {
  switch(found) {
  case MATCH_FOUND:
    if(now_total == to) return SETTLE_LANDED;
    if(now_total == from) return SETTLE_NOT_LANDED;
    return SETTLE_ATTENTION;
  case MATCH_BAD_REPLY:
    return SETTLE_UNKNOWN;
  case MATCH_NONE:
  case MATCH_AMBIGUOUS:
    break;
  }
  /* The entry is gone, or there are now two. */
  return SETTLE_ATTENTION;
}

enum settle
settle_track(enum match_result found, long now_total, long to) {
  switch(found) {
  case MATCH_FOUND:
    return now_total == to ? SETTLE_LANDED : SETTLE_ATTENTION;
  case MATCH_NONE:
    return SETTLE_NOT_LANDED;
  case MATCH_BAD_REPLY:
    return SETTLE_UNKNOWN;
  case MATCH_AMBIGUOUS:
    break;
  }
  return SETTLE_ATTENTION;
}

int
write_update_confirmed(const char *reply, long total) {
  const char *entry;
  long progress;

  if(json_valid(reply)) {
    return 0;
  }
  entry = json_first(json_member(reply, "consumptions"));
  return entry && !json_next(entry) &&
         !json_long(json_member(entry, "progress"), &progress) &&
         progress == total;
}

int
write_track_confirmed(const char *reply, const char *igdb_id, long total) {
  const char *item, *v;
  char id[24];
  long n, progress;

  if(json_valid(reply) ||
     json_long(json_member(reply, "progress"), &progress) ||
     progress != total ||
     !(item = json_member(reply, "item"))) {
    return 0;
  }
  /* The ID comes back as a string in this shape. Accept a number too. */
  v = json_member(item, "media_id");
  if(json_string(v, id, sizeof id)) {
    if(json_long(v, &n) ||
       snprintf(id, sizeof id, "%ld", n) >= (int)sizeof id) {
      return 0;
    }
  }
  return !strcmp(id, igdb_id);
}
