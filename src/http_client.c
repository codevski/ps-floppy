#include "http_client.h"

/* Must come first: the FreeBSD headers below do not include it themselves.
 * Kept in its own block so include-sorting formatters cannot move it. */
// clang-format off
#include <sys/types.h>
// clang-format on

#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define NET_TIMEOUT_S 8

#ifndef PSF_VERSION
#define PSF_VERSION "dev"
#endif

static int
connect_with_timeout(const char *host, const char *port, char *err,
                     size_t err_size) {
  struct addrinfo hints, *res = NULL, *ai;
  int last_errno = 0;
  int fd = -1;
  int rc;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if((rc = getaddrinfo(host, port, &hints, &res))) {
    snprintf(err, err_size, "Could not look up %s: %s", host,
             gai_strerror(rc));
    return -1;
  }

  for(ai = res; ai; ai = ai->ai_next) {
    struct pollfd pfd;
    socklen_t len = sizeof rc;
    int flags;

    if((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) < 0) {
      last_errno = errno;
      continue;
    }
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    if(!connect(fd, ai->ai_addr, ai->ai_addrlen)) {
      fcntl(fd, F_SETFL, flags);
      break;
    }
    last_errno = errno;
    if(errno == EINPROGRESS) {
      int ready;

      pfd.fd = fd;
      pfd.events = POLLOUT;
      rc = 0;
      ready = poll(&pfd, 1, NET_TIMEOUT_S * 1000);
      if(ready == 1 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &rc, &len) &&
         !rc) {
        fcntl(fd, F_SETFL, flags);
        break;
      }
      last_errno = ready == 0 ? ETIMEDOUT : (rc ? rc : errno);
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);

  if(fd < 0) {
    switch(last_errno) {
    case ECONNREFUSED:
      snprintf(err, err_size, "%s refused the connection on port %s. Check "
               "that Floppy is running and the port is right.", host, port);
      break;
    case ETIMEDOUT:
      snprintf(err, err_size, "No answer from %s:%s. Check the address and "
               "that the console can reach it.", host, port);
      break;
    default:
      snprintf(err, err_size, "Could not connect to %s:%s (%s).", host, port,
               strerror(last_errno));
    }
  }
  return fd;
}

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

/* Case-insensitive search for a header name within the header block. */
static const char*
find_header(const char *headers, const char *end, const char *name) {
  size_t len = strlen(name);

  for(const char *p = headers; p && p < end;) {
    if(!strncasecmp(p, name, len) && p[len] == ':') {
      p += len + 1;
      while(*p == ' ' || *p == '\t') {
        p++;
      }
      return p;
    }
    if((p = strstr(p, "\r\n"))) {
      p += 2;
    }
  }
  return NULL;
}

/* Rewrite a chunked body in place as one contiguous block. */
static size_t
dechunk(char *body, size_t len) {
  char *in = body, *end = body + len, *out = body;

  while(in < end) {
    char *eol;
    unsigned long size = strtoul(in, &eol, 16);

    if(eol == in || !(eol = strstr(eol, "\r\n"))) {
      break;
    }
    in = eol + 2;
    if(!size || in + size > end) {
      break;
    }
    memmove(out, in, size);
    out += size;
    in += size + 2; /* the data and its trailing CRLF */
  }
  *out = 0;
  return (size_t)(out - body);
}

int
http_request(const struct config *cfg, const char *method, const char *path,
             int send_token, const char *json_body, char *buf, size_t buf_size,
             struct http_reply *reply) {
  struct timeval tv = {NET_TIMEOUT_S, 0};
  char head[1024];
  size_t used = 0;
  char *sep, *eol;
  const char *te;
  ssize_t n;
  int len;
  int fd;

  memset(reply, 0, sizeof *reply);
  reply->body = buf;
  buf[0] = 0;

  len = snprintf(head, sizeof head,
                 "%s %s%s HTTP/1.1\r\n"
                 "Host: %s:%s\r\n"
                 "User-Agent: ps-floppy/" PSF_VERSION "\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n",
                 method, cfg->base, path, cfg->host, cfg->port);
  if(send_token && len > 0 && (size_t)len < sizeof head) {
    len += snprintf(head + len, sizeof head - (size_t)len,
                    "Authorization: Bearer %s\r\n", cfg->token);
  }
  if(json_body && len > 0 && (size_t)len < sizeof head) {
    len += snprintf(head + len, sizeof head - (size_t)len,
                    "Content-Type: application/json\r\n"
                    "Content-Length: %zu\r\n", strlen(json_body));
  }
  if(len > 0 && (size_t)len < sizeof head) {
    len += snprintf(head + len, sizeof head - (size_t)len, "\r\n");
  }
  if(len <= 0 || (size_t)len >= sizeof head) {
    snprintf(reply->error, sizeof reply->error, "Request was too large.");
    return -1;
  }

  fd = connect_with_timeout(cfg->host, cfg->port, reply->error,
                            sizeof reply->error);
  if(fd < 0) {
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

  if(send_all(fd, head, (size_t)len) ||
     (json_body && send_all(fd, json_body, strlen(json_body)))) {
    snprintf(reply->error, sizeof reply->error,
             "Connected, but sending the request failed (%s).",
             strerror(errno));
    close(fd);
    return -1;
  }

  while(used < buf_size - 1 &&
        (n = recv(fd, buf + used, buf_size - 1 - used, 0)) > 0) {
    used += (size_t)n;
  }
  buf[used] = 0;
  close(fd);
  reply->truncated = used == buf_size - 1;

  if(!used) {
    snprintf(reply->error, sizeof reply->error,
             "%s:%s closed the connection without replying.", cfg->host,
             cfg->port);
    return -1;
  }
  /* "HTTP/1.x NNN" */
  if(used < 12 || strncmp(buf, "HTTP/1.", 7) ||
     !(reply->status = atoi(buf + 9))) {
    snprintf(reply->error, sizeof reply->error,
             "%s:%s answered, but not like a web server. Is that the right "
             "port?", cfg->host, cfg->port);
    reply->status = 0;
    return -1;
  }

  if(!(sep = strstr(buf, "\r\n\r\n"))) {
    reply->body = buf + used;
    return 0;
  }
  reply->body = sep + 4;
  reply->body_len = used - (size_t)(reply->body - buf);

  eol = strstr(buf, "\r\n");
  te = find_header(eol + 2, sep + 2, "Transfer-Encoding");
  if(te && !strncasecmp(te, "chunked", 7)) {
    reply->body_len = dechunk(reply->body, reply->body_len);
  }
  return 0;
}
