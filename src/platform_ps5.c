#include "platform.h"
#include "appmeta.h"

/* Must come first: the FreeBSD headers below do not include it themselves.
 * Kept in its own block so include-sorting formatters cannot move it. */
// clang-format off
#include <sys/types.h>
// clang-format on

#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* What Payload Manager's Active Processes tab shows, and what a newer copy
 * looks for when it takes over. */
#define PROCESS_NAME "ps-floppy.elf"

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

/* Undocumented system service calls. Their behaviour was established by
 * running them on a console at firmware 13.60 (spike runs 0.2 and 0.4):
 *   - the first returns the app ID of the running game, or a negative value;
 *   - the second fills in that app's title ID and returns 0;
 *   - the third fills a status block whose first 32 bits are the ID of the
 *     app that has focus. It equals the game's ID while the game is on
 *     screen and becomes the home screen's ID while the game is minimized. */
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
int sceSystemServiceGetAppFocusedAppStatus(void *status);

/* Find another process with our name. The process list layout is the
 * console's, not the FreeBSD header's, so the fields are read at the offsets
 * other PS5 daemons (ftpsrv, websrv) use. */
static pid_t
find_other_instance(void) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  pid_t self = getpid();
  pid_t found = -1;
  size_t size = 0;
  uint8_t *buf;

  if(sysctl(mib, 4, NULL, &size, NULL, 0)) {
    return -1;
  }
  size += size / 4;
  if(!(buf = malloc(size))) {
    return -1;
  }
  if(sysctl(mib, 4, buf, &size, NULL, 0)) {
    free(buf);
    return -1;
  }

  for(uint8_t *ptr = buf; ptr + 448 < buf + size;) {
    int structsize = *(int*)ptr;
    pid_t pid = *(pid_t*)&ptr[72];
    const char *tdname = (const char*)&ptr[447];

    if(structsize <= 0) {
      break;
    }
    if(pid != self && !strncmp(tdname, PROCESS_NAME, sizeof PROCESS_NAME)) {
      found = pid;
    }
    ptr += structsize;
  }

  free(buf);
  return found;
}

void
plat_init(void) {
  pid_t pid;

  syscall(SYS_thr_set_name, -1, PROCESS_NAME);

  /* Bounded, so a copy that cannot be killed never hangs the new one. */
  for(int tries = 0; tries < 5 && (pid = find_other_instance()) > 0; tries++) {
    if(kill(pid, SIGKILL)) {
      break;
    }
    sleep(1);
  }

  /* A browser closing mid-reply must not take the tracker down. */
  signal(SIGPIPE, SIG_IGN);
}

void
plat_notify(const char *fmt, ...) {
  notify_request_t req;
  va_list ap;

  memset(&req, 0, sizeof req);
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);

  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

/* Game title IDs are 4 capitals + 5 digits (PPSA12345, CUSA12345). */
static int
is_title_id(const char *s) {
  if(strlen(s) != 9) {
    return 0;
  }
  for(int i = 0; i < 4; i++) {
    if(s[i] < 'A' || s[i] > 'Z') return 0;
  }
  for(int i = 4; i < 9; i++) {
    if(s[i] < '0' || s[i] > '9') return 0;
  }
  return 1;
}

int
plat_current_game(char title_id[10], int *focused) {
  static uint8_t status[4096]; /* far larger than the block the call fills */
  char tid[64];
  uint32_t focus_id;
  int app_id;

  title_id[0] = 0;
  *focused = 0;

  app_id = sceSystemServiceGetAppIdOfRunningBigApp();
  if(app_id <= 0) {
    return 0;
  }
  memset(tid, 0, sizeof tid);
  if(sceSystemServiceGetAppTitleId(app_id, tid)) {
    return 0;
  }
  tid[sizeof tid - 1] = 0;
  if(!is_title_id(tid)) {
    return 0;
  }
  memcpy(title_id, tid, 10);

  memset(status, 0, sizeof status);
  if(sceSystemServiceGetAppFocusedAppStatus(status)) {
    /* If focus cannot be read, count the game as on screen rather than
     * silently recording nothing. */
    *focused = 1;
    return 1;
  }
  memcpy(&focus_id, status, sizeof focus_id);
  *focused = focus_id == (uint32_t)app_id;
  return 1;
}

static long
read_file(const char *path, char *buf, size_t size) {
  FILE *f = fopen(path, "rb");
  long n;

  if(!f) {
    return -1;
  }
  n = (long)fread(buf, 1, size - 1, f);
  fclose(f);
  buf[n < 0 ? 0 : n] = 0;
  return n;
}

/* PS4 titles carry a binary param.sfo: find the TITLE key. Written from the
 * file format, not yet exercised on a console. */
static int
name_from_param_sfo(const unsigned char *sfo, long len, char *name,
                    size_t size) {
  uint32_t key_table, data_table, count;

  if(len < 0x14 || memcmp(sfo, "\0PSF", 4)) {
    return -1;
  }
  memcpy(&key_table, sfo + 0x08, 4);
  memcpy(&data_table, sfo + 0x0c, 4);
  memcpy(&count, sfo + 0x10, 4);

  for(uint32_t i = 0; i < count; i++) {
    const unsigned char *e = sfo + 0x14 + i * 16;
    uint16_t key_off;
    uint32_t data_len, data_off;

    if(e + 16 > sfo + len) {
      break;
    }
    memcpy(&key_off, e, 2);
    memcpy(&data_len, e + 4, 4);
    memcpy(&data_off, e + 12, 4);

    if((long)(key_table + key_off + 6) > len ||
       (long)(data_table + data_off + data_len) > len) {
      continue;
    }
    if(!strcmp((const char*)sfo + key_table + key_off, "TITLE")) {
      snprintf(name, size, "%.*s", (int)data_len,
               (const char*)sfo + data_table + data_off);
      return name[0] ? 0 : -1;
    }
  }
  return -1;
}

int
plat_game_name(const char *title_id, char *out, size_t size) {
  static const char *dirs[] = {
    "/user/appmeta/%s",
    "/system_data/priv/appmeta/%s",
    "/user/appmeta/external/%s",
  };
  static char buf[256 * 1024];
  char path[256];
  long n;

  for(size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
    char dir[200];

    snprintf(dir, sizeof dir, dirs[i], title_id);

    snprintf(path, sizeof path, "%s/param.json", dir);
    if((n = read_file(path, buf, sizeof buf)) > 0 &&
       !appmeta_title(buf, out, size)) {
      return 0;
    }
    snprintf(path, sizeof path, "%s/param.sfo", dir);
    if((n = read_file(path, buf, sizeof buf)) > 0 &&
       !name_from_param_sfo((unsigned char*)buf, n, out, size)) {
      return 0;
    }
  }
  return -1;
}

const char*
plat_data_dir(void) {
  return "/data/ps-floppy";
}
