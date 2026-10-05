#include "http_server.h"

/* Must come first: the FreeBSD headers below do not include it themselves.
 * Kept in its own block so include-sorting formatters cannot move it. */
// clang-format off
#include <sys/types.h>
// clang-format on

#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "http_client.h"
#include "json.h"
#include "page.h"

#define REQUEST_MAX  (16 * 1024)
#define CLIENT_BUF   (64 * 1024)

struct request {
  char  method[8];
  char  path[128];
  char *headers;      /* first header line */
  char *headers_end;  /* the blank line */
  char *body;
  size_t body_len;
};

/* ---- low level ---------------------------------------------------------- */

static int
send_all(int fd, const char *data, size_t len) {
  while(len) {
    ssize_t n = send(fd, data, len, 0);
    if(n <= 0) {
      return -1;
    }
    data += n;
    len -= (size_t)n;
  }
  return 0;
}

static void
respond(int fd, int status, const char *type, const char *body, size_t len) {
  const char *reason = status == 200 ? "OK" :
                       status == 400 ? "Bad Request" :
                       status == 403 ? "Forbidden" :
                       status == 404 ? "Not Found" :
                       status == 405 ? "Method Not Allowed" :
                       status == 413 ? "Payload Too Large" :
                       status == 415 ? "Unsupported Media Type" : "Error";
  char head[320];
  int n;

  n = snprintf(head, sizeof head,
               "HTTP/1.1 %d %s\r\n"
               "Content-Type: %s\r\n"
               "Content-Length: %zu\r\n"
               "Cache-Control: no-store\r\n"
               "X-Content-Type-Options: nosniff\r\n"
               "Connection: close\r\n\r\n",
               status, reason, type, len);
  if(n > 0 && !send_all(fd, head, (size_t)n)) {
    send_all(fd, body, len);
  }
}

static void
respond_json(int fd, int status, const char *json) {
  respond(fd, status, "application/json", json, strlen(json));
}

static void
respond_error(int fd, int status, const char *message) {
  char esc[400];
  char out[480];

  if(json_escape(message, esc, sizeof esc)) {
    esc[0] = 0;
  }
  snprintf(out, sizeof out, "{\"error\":\"%s\"}", esc);
  respond_json(fd, status, out);
}

static const char*
header_value(const struct request *req, const char *name, size_t *len) {
  size_t name_len = strlen(name);

  for(const char *p = req->headers; p && p < req->headers_end;) {
    const char *eol = strstr(p, "\r\n");

    if(!eol) {
      break;
    }
    if(!strncasecmp(p, name, name_len) && p[name_len] == ':') {
      p += name_len + 1;
      while(*p == ' ' || *p == '\t') {
        p++;
      }
      *len = (size_t)(eol - p);
      return p;
    }
    p = eol + 2;
  }
  return NULL;
}

/* Read one request into `buf`. Returns 0, or an HTTP status to reply with,
 * or -1 if the client went away. */
static int
read_request(int fd, char *buf, size_t size, struct request *req) {
  size_t used = 0;
  size_t want = 0;
  char *sep = NULL;
  const char *cl;
  size_t cl_len;
  ssize_t n;

  memset(req, 0, sizeof *req);

  while(!sep) {
    if(used >= size - 1) {
      return 413;
    }
    if((n = recv(fd, buf + used, size - 1 - used, 0)) <= 0) {
      return -1;
    }
    used += (size_t)n;
    buf[used] = 0;
    sep = strstr(buf, "\r\n\r\n");
  }

  if(sscanf(buf, "%7s %127s", req->method, req->path) != 2) {
    return 400;
  }
  /* The page never uses query strings: drop one if present. */
  req->path[strcspn(req->path, "?")] = 0;

  if(!(req->headers = strstr(buf, "\r\n"))) {
    return 400;
  }
  req->headers += 2;
  req->headers_end = sep + 2;
  req->body = sep + 4;

  if((cl = header_value(req, "Content-Length", &cl_len))) {
    long v = strtol(cl, NULL, 10);
    if(v < 0 || (size_t)v > size - 1 - (size_t)(req->body - buf)) {
      return 413;
    }
    want = (size_t)v;
  }
  while(used - (size_t)(req->body - buf) < want) {
    if((n = recv(fd, buf + used, size - 1 - used, 0)) <= 0) {
      return -1;
    }
    used += (size_t)n;
    buf[used] = 0;
  }
  req->body_len = want;
  req->body[want] = 0;
  return 0;
}

/* Reject cross-site requests. Any web page open in a browser on the LAN can
 * fire a request at this address, and one that changed the Floppy address
 * would have the saved token sent to a server of its choosing. Two checks:
 *   - writes must be JSON, which browsers will not send cross-site without a
 *     preflight this server never approves;
 *   - if the browser names an Origin, it must be this server. */
static int
request_is_same_site(const struct request *req) {
  size_t origin_len, host_len, type_len;
  const char *origin = header_value(req, "Origin", &origin_len);
  const char *host = header_value(req, "Host", &host_len);
  const char *type = header_value(req, "Content-Type", &type_len);

  if(!type || type_len < 16 || strncasecmp(type, "application/json", 16)) {
    return 0;
  }
  if(origin) {
    if(origin_len < 7 || strncasecmp(origin, "http://", 7) || !host) {
      return 0;
    }
    origin += 7;
    origin_len -= 7;
    if(origin_len != host_len || strncasecmp(origin, host, host_len)) {
      return 0;
    }
  }
  return 1;
}

/* ---- API ---------------------------------------------------------------- */

/* The token is write-only: only its last four characters ever leave. */
static void
config_to_json(const struct config *cfg, char *out, size_t size) {
  char url[2 * sizeof cfg->floppy_url];
  size_t token_len = strlen(cfg->token);
  const char *hint = token_len >= 8 ? cfg->token + token_len - 4 : "";
  char hint_esc[32];

  if(json_escape(cfg->floppy_url, url, sizeof url)) {
    url[0] = 0;
  }
  if(json_escape(hint, hint_esc, sizeof hint_esc)) {
    hint_esc[0] = 0;
  }
  snprintf(out, size,
           "{\"floppy_url\":\"%s\",\"token_set\":%s,\"token_hint\":\"%s\","
           "\"send_to_floppy\":%s}",
           url, token_len ? "true" : "false", hint_esc,
           cfg->send ? "true" : "false");
}

static void
handle_get_status(struct app *app, int fd) {
  char out[256];
  int configured;

  pthread_mutex_lock(&app->lock);
  configured = config_is_complete(&app->cfg);
  pthread_mutex_unlock(&app->lock);

  snprintf(out, sizeof out,
           "{\"name\":\"ps-floppy\",\"version\":\"" PSF_VERSION "\","
           "\"uptime_seconds\":%ld,\"configured\":%s}",
           (long)(time(NULL) - app->started), configured ? "true" : "false");
  respond_json(fd, 200, out);
}

/* What the tracker sees and what it is holding, for the page. */
static void
handle_get_tracker(struct app *app, int fd) {
  size_t size = 64 * 1024;
  char *out = malloc(size);
  char name[400];
  char note[1100];
  size_t used = 0;
  int first = 1;

  if(!out) {
    respond_error(fd, 500, "Out of memory.");
    return;
  }

  pthread_mutex_lock(&app->lock);

  if(app->now.title_id[0]) {
    json_escape(app->now.name, name, sizeof name);
    used += (size_t)snprintf(out + used, size - used,
                             "{\"playing\":{\"title_id\":\"%s\",\"name\":\"%s\","
                             "\"focused\":%s,\"session_seconds\":%ld},",
                             app->now.title_id, name,
                             app->now.focused ? "true" : "false",
                             app->now.session);
  } else {
    used += (size_t)snprintf(out + used, size - used, "{\"playing\":null,");
  }

  used += (size_t)snprintf(out + used, size - used, "\"waiting\":[");
  for(int i = 0; i < app->store.count && used + 2000 < size; i++) {
    static const char *const states[] = {
      [SYNC_NOT_YET] = "not_yet", [SYNC_READY] = "ready",
      [SYNC_NEEDS_MATCH] = "needs_match", [SYNC_RETRY] = "retry",
      [SYNC_SENT] = "sent", [SYNC_ATTENTION] = "attention",
    };
    const struct game *g = &app->store.games[i];

    if(g->pending <= 0) {
      continue;
    }
    json_escape(g->name, name, sizeof name);
    if(json_escape(g->sync_note, note, sizeof note)) {
      note[0] = 0;
    }
    used += (size_t)snprintf(out + used, size - used,
                             "%s{\"title_id\":\"%s\",\"name\":\"%s\","
                             "\"seconds\":%ld,\"sync\":\"%s\","
                             "\"note\":\"%s\"}",
                             first ? "" : ",", g->title_id, name, g->pending,
                             states[g->sync], note);
    first = 0;
  }
  snprintf(out + used, size - used, "]}");

  pthread_mutex_unlock(&app->lock);

  respond_json(fd, 200, out);
  free(out);
}

static void
handle_get_config(struct app *app, int fd) {
  struct config cfg;
  char out[1024];

  pthread_mutex_lock(&app->lock);
  cfg = app->cfg;
  pthread_mutex_unlock(&app->lock);

  config_to_json(&cfg, out, sizeof out);
  respond_json(fd, 200, out);
}

static void
handle_post_config(struct app *app, int fd, const struct request *req) {
  struct config next;
  const char *send;
  char value[512];
  char err[256];
  char out[1024];
  int saved;

  /* The page always sends well-formed JSON. Anything else is refused before
   * any of it is read, so a broken body cannot flip a setting. */
  if(json_valid(req->body)) {
    respond_error(fd, 400, "The settings were not valid JSON.");
    return;
  }

  pthread_mutex_lock(&app->lock);
  next = app->cfg;
  pthread_mutex_unlock(&app->lock);

  if(json_get_string(req->body, "floppy_url", value, sizeof value)) {
    respond_error(fd, 400, "Enter the address of your Floppy server.");
    return;
  }
  if(config_set_url(&next, value, err, sizeof err)) {
    respond_error(fd, 400, err);
    return;
  }
  /* A blank token field means "keep the one already saved". */
  if(!json_get_string(req->body, "token", value, sizeof value) && value[0]) {
    if(config_set_token(&next, value, err, sizeof err)) {
      respond_error(fd, 400, err);
      return;
    }
  }
  /* Absent means "keep the current setting". */
  if((send = json_member(req->body, "send_to_floppy"))) {
    if(!strncmp(send, "true", 4)) {
      next.send = 1;
    } else if(!strncmp(send, "false", 5)) {
      next.send = 0;
    } else {
      respond_error(fd, 400, "send_to_floppy must be true or false.");
      return;
    }
  }

  pthread_mutex_lock(&app->lock);
  saved = !config_save(&next, app->config_path);
  if(saved) {
    if(next.send != app->cfg.send) {
      app_log(app, "sending to Floppy turned %s from the config page",
              next.send ? "on" : "off");
    }
    app->cfg = next;
    app->sync_wanted = 1; /* new settings: look again now, not in 15 min */
  }
  pthread_mutex_unlock(&app->lock);

  if(!saved) {
    respond_error(fd, 500, "Could not write the settings file on the "
                  "console.");
    return;
  }
  config_to_json(&next, out, sizeof out);
  respond_json(fd, 200, out);
}

/* Check the saved settings against the real server: is Floppy there, and
 * does it accept the token? */
static void
handle_post_test(struct app *app, int fd) {
  struct http_reply reply;
  struct config cfg;
  const char *token_state;
  char version[96] = "";
  char message[640] = "";
  char esc_version[200];
  char esc_message[1300];
  char out[1700];
  char *buf;

  pthread_mutex_lock(&app->lock);
  cfg = app->cfg;
  pthread_mutex_unlock(&app->lock);

  if(!cfg.host[0]) {
    respond_error(fd, 400, "Save a Floppy address first.");
    return;
  }
  if(!(buf = malloc(CLIENT_BUF))) {
    respond_error(fd, 500, "Out of memory.");
    return;
  }

  if(http_request(&cfg, "GET", "/api/v1/info", 0, NULL, buf, CLIENT_BUF,
                  &reply)) {
    json_escape(reply.error, esc_message, sizeof esc_message);
    snprintf(out, sizeof out,
             "{\"reachable\":false,\"token\":\"untested\",\"message\":\"%s\"}",
             esc_message);
    respond_json(fd, 200, out);
    free(buf);
    return;
  }
  if(reply.status != 200 ||
     json_get_string(reply.body, "version", version, sizeof version)) {
    snprintf(message, sizeof message,
             "Something answered at %s, but it does not look like Floppy "
             "(HTTP %d from /api/v1/info). Check the address and port.",
             cfg.floppy_url, reply.status);
    json_escape(message, esc_message, sizeof esc_message);
    snprintf(out, sizeof out,
             "{\"reachable\":false,\"token\":\"untested\",\"message\":\"%s\"}",
             esc_message);
    respond_json(fd, 200, out);
    free(buf);
    return;
  }

  if(!cfg.token[0]) {
    token_state = "missing";
    snprintf(message, sizeof message,
             "Floppy is reachable. Add your API token to finish.");
  } else if(http_request(&cfg, "GET", "/api/v1/media/game?limit=1", 1, NULL,
                         buf, CLIENT_BUF, &reply)) {
    token_state = "unknown";
    snprintf(message, sizeof message,
             "Floppy is reachable, but checking the token failed: %s",
             reply.error);
  } else if(reply.status == 200) {
    token_state = "ok";
    snprintf(message, sizeof message, "Connected. Floppy accepted the token.");
  } else if(reply.status == 401 || reply.status == 403) {
    token_state = "rejected";
    snprintf(message, sizeof message,
             "Floppy is reachable but rejected the token. Copy it again from "
             "Floppy: Settings, then Integrations.");
  } else {
    token_state = "unknown";
    snprintf(message, sizeof message,
             "Floppy is reachable, but the token check returned HTTP %d.",
             reply.status);
  }

  json_escape(version, esc_version, sizeof esc_version);
  json_escape(message, esc_message, sizeof esc_message);
  snprintf(out, sizeof out,
           "{\"reachable\":true,\"floppy_version\":\"%s\",\"token\":\"%s\","
           "\"message\":\"%s\"}",
           esc_version, token_state, esc_message);
  respond_json(fd, 200, out);
  free(buf);
}

/* ---- routing ------------------------------------------------------------ */

static void
handle_client(struct app *app, int fd) {
  struct timeval tv = {10, 0};
  struct request req;
  char *buf;
  int is_get, is_post;
  int rc;

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

  if(!(buf = malloc(REQUEST_MAX))) {
    return;
  }
  if((rc = read_request(fd, buf, REQUEST_MAX, &req))) {
    if(rc > 0) {
      respond_error(fd, rc, rc == 413 ? "Request too large." : "Bad request.");
    }
    free(buf);
    return;
  }

  is_get = !strcmp(req.method, "GET");
  is_post = !strcmp(req.method, "POST");

  if(is_post && !request_is_same_site(&req)) {
    respond_error(fd, 403, "Request refused. Open this page directly at the "
                  "console's address and try again.");
  } else if(!strcmp(req.path, "/") || !strcmp(req.path, "/index.html")) {
    size_t len;
    const char *html = page_html(&len);

    if(is_get) {
      respond(fd, 200, "text/html; charset=utf-8", html, len);
    } else {
      respond_error(fd, 405, "Method not allowed.");
    }
  } else if(!strcmp(req.path, "/api/status") && is_get) {
    handle_get_status(app, fd);
  } else if(!strcmp(req.path, "/api/tracker") && is_get) {
    handle_get_tracker(app, fd);
  } else if(!strcmp(req.path, "/api/config") && is_get) {
    handle_get_config(app, fd);
  } else if(!strcmp(req.path, "/api/config") && is_post) {
    handle_post_config(app, fd, &req);
  } else if(!strcmp(req.path, "/api/test") && is_post) {
    handle_post_test(app, fd);
  } else {
    respond_error(fd, 404, "Not found.");
  }

  free(buf);
}

static int
open_listener(int port) {
  struct sockaddr_in addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;

  if(fd < 0) {
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

  if(bind(fd, (struct sockaddr*)&addr, sizeof addr) || listen(fd, 8)) {
    close(fd);
    return -1;
  }
  return fd;
}

void*
http_server_run(void *arg) {
  struct app *app = arg;

  for(;;) {
    int srv = open_listener(PSF_HTTP_PORT);

    if(srv < 0) {
      sleep(3);
      continue;
    }
    for(;;) {
      int fd = accept(srv, NULL, NULL);

      if(fd < 0) {
        if(errno == EINTR || errno == ECONNABORTED) {
          continue;
        }
        /* The listener is dead, which happens after rest mode. Rebuild. */
        break;
      }
      handle_client(app, fd);
      shutdown(fd, SHUT_RDWR);
      close(fd);
    }
    close(srv);
    sleep(3);
  }
  return NULL;
}
