#include "config.h"
#include "json.h"

#include <sys/stat.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
set_err(char *err, size_t n, const char *msg) {
  if(err && n) {
    snprintf(err, n, "%s", msg);
  }
}

int
config_set_url(struct config *cfg, const char *url, char *err, size_t n) {
  char work[256];
  char *host, *path, *port;
  size_t len;

  while(isspace((unsigned char)*url)) {
    url++;
  }
  len = strlen(url);
  while(len && isspace((unsigned char)url[len - 1])) {
    len--;
  }
  if(!len) {
    set_err(err, n, "Enter the address of your Floppy server.");
    return -1;
  }
  if(len >= sizeof work) {
    set_err(err, n, "That address is too long.");
    return -1;
  }
  memcpy(work, url, len);
  work[len] = 0;

  /* Anything outside plain printable ASCII could smuggle extra lines into
   * the HTTP request this address ends up in. */
  for(char *p = work; *p; p++) {
    if((unsigned char)*p <= 0x20 || (unsigned char)*p >= 0x7f) {
      set_err(err, n, "The address contains a space or an invalid character.");
      return -1;
    }
  }

  if(!strncmp(work, "https://", 8)) {
    set_err(err, n, "HTTPS is not supported yet. Use the plain http:// "
            "address of Floppy on your network, for example "
            "http://192.168.1.10:8000.");
    return -1;
  }
  if(strncmp(work, "http://", 7)) {
    set_err(err, n, "The address must start with http://, for example "
            "http://192.168.1.10:8000.");
    return -1;
  }

  host = work + 7;
  if((path = strchr(host, '/'))) {
    *path++ = 0;
  }
  if((port = strchr(host, ':'))) {
    *port++ = 0;
  }
  if(!*host || strlen(host) >= sizeof cfg->host) {
    set_err(err, n, "The address is missing a host name or IP.");
    return -1;
  }
  if(port) {
    long v = strtol(port, &port, 10);
    if(*port || v < 1 || v > 65535) {
      set_err(err, n, "The port must be a number between 1 and 65535.");
      return -1;
    }
    snprintf(cfg->port, sizeof cfg->port, "%ld", v);
  } else {
    snprintf(cfg->port, sizeof cfg->port, "80");
  }
  memcpy(cfg->host, host, strlen(host) + 1); /* length checked above */

  cfg->base[0] = 0;
  if(path) {
    len = strlen(path);
    while(len && path[len - 1] == '/') {
      len--;
    }
    if(len + 2 > sizeof cfg->base) {
      set_err(err, n, "The path part of the address is too long.");
      return -1;
    }
    if(len) {
      cfg->base[0] = '/';
      memcpy(cfg->base + 1, path, len); /* length checked above */
      cfg->base[len + 1] = 0;
    }
  }

  if(strcmp(cfg->port, "80")) {
    snprintf(cfg->floppy_url, sizeof cfg->floppy_url, "http://%s:%s%s",
             cfg->host, cfg->port, cfg->base);
  } else {
    snprintf(cfg->floppy_url, sizeof cfg->floppy_url, "http://%s%s",
             cfg->host, cfg->base);
  }
  return 0;
}

int
config_set_token(struct config *cfg, const char *token, char *err, size_t n) {
  size_t len;

  while(isspace((unsigned char)*token)) {
    token++;
  }
  len = strlen(token);
  while(len && isspace((unsigned char)token[len - 1])) {
    len--;
  }
  if(len >= sizeof cfg->token) {
    set_err(err, n, "That token is too long.");
    return -1;
  }
  for(size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)token[i];
    if(c <= 0x20 || c >= 0x7f) {
      set_err(err, n, "The token contains a space or an invalid character. "
              "Copy it again from Floppy.");
      return -1;
    }
  }
  memcpy(cfg->token, token, len);
  cfg->token[len] = 0;
  return 0;
}

int
config_is_complete(const struct config *cfg) {
  return cfg->host[0] && cfg->token[0];
}

int
config_load(struct config *cfg, const char *path) {
  char buf[2048];
  char value[320];
  const char *send;
  size_t len;
  FILE *f;

  memset(cfg, 0, sizeof *cfg);
  if(!(f = fopen(path, "rb"))) {
    return 0;
  }
  len = fread(buf, 1, sizeof buf - 1, f);
  fclose(f);
  buf[len] = 0;

  if(!json_get_string(buf, "floppy_url", value, sizeof value) && value[0]) {
    if(config_set_url(cfg, value, NULL, 0)) {
      memset(cfg, 0, sizeof *cfg);
      return -1;
    }
  }
  if(!json_get_string(buf, "token", value, sizeof value)) {
    config_set_token(cfg, value, NULL, 0);
  }
  /* Off unless the file says exactly true: a damaged file never turns
   * writes on. */
  cfg->send = !json_valid(buf) &&
              (send = json_member(buf, "send_to_floppy")) &&
              !strncmp(send, "true", 4);
  return 0;
}

int
config_save(const struct config *cfg, const char *path) {
  char url[2 * sizeof cfg->floppy_url];
  char token[2 * sizeof cfg->token];
  char tmp[512];
  FILE *f;

  if(json_escape(cfg->floppy_url, url, sizeof url) ||
     json_escape(cfg->token, token, sizeof token)) {
    return -1;
  }
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  if(!(f = fopen(tmp, "wb"))) {
    return -1;
  }
  /* The token gives full access to the Floppy account: owner-only file. */
  chmod(tmp, 0600);
  fprintf(f, "{\n  \"floppy_url\": \"%s\",\n  \"token\": \"%s\",\n"
          "  \"send_to_floppy\": %s\n}\n", url, token,
          cfg->send ? "true" : "false");
  if(fclose(f)) {
    unlink(tmp);
    return -1;
  }
  if(rename(tmp, path)) {
    unlink(tmp);
    return -1;
  }
  return 0;
}
