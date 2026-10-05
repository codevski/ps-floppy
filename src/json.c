#include "json.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Floppy's replies nest about five deep. The limit stops a hostile or broken
 * reply from running the stack out. */
#define JSON_MAX_DEPTH 32

static const char*
skip_ws(const char *p) {
  while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  return p;
}

static int
hex4(const char *p, unsigned *out) {
  unsigned v = 0;

  for(int i = 0; i < 4; i++) {
    char c = p[i];
    v <<= 4;
    if(c >= '0' && c <= '9') v |= (unsigned)(c - '0');
    else if(c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
    else if(c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
    else return -1;
  }
  *out = v;
  return 0;
}

/* Parse the string starting at the opening quote `p`. Decodes into `out` when
 * it is non-NULL. Returns a pointer just past the closing quote, or NULL. */
static const char*
parse_string(const char *p, char *out, size_t size) {
  size_t n = 0;

  if(*p++ != '"') {
    return NULL;
  }
  while(*p && *p != '"') {
    unsigned cp = (unsigned char)*p++;

    if(cp == '\\') {
      switch(*p++) {
      case '"':  cp = '"';  break;
      case '\\': cp = '\\'; break;
      case '/':  cp = '/';  break;
      case 'b':  cp = '\b'; break;
      case 'f':  cp = '\f'; break;
      case 'n':  cp = '\n'; break;
      case 'r':  cp = '\r'; break;
      case 't':  cp = '\t'; break;
      case 'u':
        if(hex4(p, &cp)) return NULL;
        p += 4;
        break;
      default:
        return NULL;
      }
      /* Escapes above 0x7f become UTF-8 (basic plane only). */
      if(out && cp > 0x7f) {
        if(cp < 0x800) {
          if(n + 2 >= size) return NULL;
          out[n++] = (char)(0xc0 | (cp >> 6));
          out[n++] = (char)(0x80 | (cp & 0x3f));
        } else {
          if(n + 3 >= size) return NULL;
          out[n++] = (char)(0xe0 | (cp >> 12));
          out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
          out[n++] = (char)(0x80 | (cp & 0x3f));
        }
        continue;
      }
    }
    if(out) {
      if(n + 1 >= size) return NULL;
      out[n++] = (char)cp;
    }
  }
  if(*p != '"') {
    return NULL;
  }
  if(out) {
    out[n] = 0;
  }
  return p + 1;
}

int
json_get_string(const char *json, const char *key, char *out, size_t size) {
  const char *p = skip_ws(json);
  int depth = 0;
  char name[64];

  /* Walk the document token by token so a key name that appears inside a
   * value, or inside a nested object, is never mistaken for the key. */
  while(*p) {
    if(*p == '{' || *p == '[') {
      depth++;
      p++;
    } else if(*p == '}' || *p == ']') {
      depth--;
      p++;
    } else if(*p == '"') {
      const char *end = parse_string(p, name, sizeof name);
      const char *after;

      if(!end) {
        /* Too long to be one of our keys, or malformed: skip or give up. */
        if(!(end = parse_string(p, NULL, 0))) return -1;
        name[0] = 0;
      }
      after = skip_ws(end);
      if(depth == 1 && *after == ':' && !strcmp(name, key)) {
        after = skip_ws(after + 1);
        if(*after != '"') return -1;
        return parse_string(after, out, size) ? 0 : -1;
      }
      p = end;
    } else {
      p++;
    }
  }
  return -1;
}

int
json_escape(const char *in, char *out, size_t size) {
  size_t n = 0;

  for(; *in; in++) {
    unsigned char c = (unsigned char)*in;
    char esc[8];
    size_t len;

    if(c == '"' || c == '\\') {
      esc[0] = '\\'; esc[1] = (char)c; len = 2;
    } else if(c < 0x20) {
      len = (size_t)snprintf(esc, sizeof esc, "\\u%04x", c);
    } else {
      esc[0] = (char)c; len = 1;
    }
    if(n + len >= size) return -1;
    memcpy(out + n, esc, len);
    n += len;
  }
  if(n >= size) return -1;
  out[n] = 0;
  return 0;
}

static int
is_digit(char c) {
  return c >= '0' && c <= '9';
}

static const char*
skip_value(const char *p, int depth) {
  p = skip_ws(p);
  if(depth > JSON_MAX_DEPTH) {
    return NULL;
  }
  switch(*p) {
  case '"':
    return parse_string(p, NULL, 0);
  case '{':
  case '[': {
    char close = *p == '{' ? '}' : ']';
    int is_obj = *p == '{';

    p = skip_ws(p + 1);
    if(*p == close) {
      return p + 1;
    }
    for(;;) {
      if(is_obj) {
        if(!(p = parse_string(p, NULL, 0))) return NULL;
        p = skip_ws(p);
        if(*p++ != ':') return NULL;
      }
      if(!(p = skip_value(p, depth + 1))) return NULL;
      p = skip_ws(p);
      if(*p == close) return p + 1;
      if(*p++ != ',') return NULL;
      p = skip_ws(p);
    }
  }
  case 't':
    return strncmp(p, "true", 4) ? NULL : p + 4;
  case 'f':
    return strncmp(p, "false", 5) ? NULL : p + 5;
  case 'n':
    return strncmp(p, "null", 4) ? NULL : p + 4;
  default:
    if(*p == '-') p++;
    if(!is_digit(*p)) return NULL;
    while(is_digit(*p)) p++;
    if(*p == '.') {
      if(!is_digit(*++p)) return NULL;
      while(is_digit(*p)) p++;
    }
    if(*p == 'e' || *p == 'E') {
      p++;
      if(*p == '+' || *p == '-') p++;
      if(!is_digit(*p)) return NULL;
      while(is_digit(*p)) p++;
    }
    return p;
  }
}

const char*
json_skip(const char *v) {
  return v ? skip_value(v, 0) : NULL;
}

int
json_valid(const char *doc) {
  const char *end = json_skip(doc);

  return end && !*skip_ws(end) ? 0 : -1;
}

const char*
json_member(const char *obj, const char *key) {
  const char *p;
  char name[64];

  if(!obj || *(p = skip_ws(obj)) != '{') {
    return NULL;
  }
  p = skip_ws(p + 1);
  while(*p == '"') {
    const char *end = parse_string(p, name, sizeof name);

    if(!end) {
      /* Longer than any key we look for: skip it. */
      if(!(end = parse_string(p, NULL, 0))) return NULL;
      name[0] = 0;
    }
    p = skip_ws(end);
    if(*p++ != ':') return NULL;
    p = skip_ws(p);
    if(name[0] && !strcmp(name, key)) {
      return p;
    }
    if(!(p = json_skip(p))) return NULL;
    p = skip_ws(p);
    if(*p != ',') return NULL;
    p = skip_ws(p + 1);
  }
  return NULL;
}

const char*
json_first(const char *arr) {
  const char *p;

  if(!arr || *(p = skip_ws(arr)) != '[') {
    return NULL;
  }
  p = skip_ws(p + 1);
  return *p == ']' ? NULL : p;
}

const char*
json_next(const char *elem) {
  const char *p = json_skip(elem);

  if(!p) {
    return NULL;
  }
  p = skip_ws(p);
  return *p == ',' ? skip_ws(p + 1) : NULL;
}

int
json_string(const char *v, char *out, size_t size) {
  if(!v) {
    return -1;
  }
  v = skip_ws(v);
  return *v == '"' && parse_string(v, out, size) ? 0 : -1;
}

int
json_long(const char *v, long *out) {
  long n = 0;
  int neg;

  if(!v) {
    return -1;
  }
  v = skip_ws(v);
  neg = *v == '-';
  if(neg) v++;
  if(!is_digit(*v)) {
    return -1;
  }
  while(is_digit(*v)) {
    int d = *v++ - '0';

    /* Accumulate as a negative so LONG_MIN fits too. */
    if(n < (LONG_MIN + d) / 10) return -1;
    n = n * 10 - d;
  }
  if(*v == '.' || *v == 'e' || *v == 'E') {
    return -1;
  }
  if(!neg) {
    if(n == LONG_MIN) return -1;
    n = -n;
  }
  *out = n;
  return 0;
}
