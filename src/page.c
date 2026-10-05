#include "page.h"

#include <stdio.h>
#include <string.h>

#ifdef HOST_BUILD

/* Desktop build: read the file on every request, so the page can be edited
 * and refreshed without rebuilding. */
const char*
page_html(size_t *len) {
  static char buf[128 * 1024];
  FILE *f = fopen("web/index.html", "rb");
  size_t n = 0;

  if(f) {
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
  } else {
    n = (size_t)snprintf(buf, sizeof buf,
                         "web/index.html not found. Run from the project "
                         "root.");
  }
  buf[n] = 0;
  *len = n;
  return buf;
}

#else

/* Console build: the page is baked into the binary, so the payload stays a
 * single file with nothing to copy alongside it. */
__asm__(".pushsection .rodata\n"
        ".globl psf_page_start\n"
        "psf_page_start:\n"
        ".incbin \"web/index.html\"\n"
        ".globl psf_page_end\n"
        "psf_page_end:\n"
        ".byte 0\n"
        ".popsection\n");

extern const char psf_page_start[];
extern const char psf_page_end[];

const char*
page_html(size_t *len) {
  *len = (size_t)(psf_page_end - psf_page_start);
  return psf_page_start;
}

#endif
