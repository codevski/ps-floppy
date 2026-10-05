/* Reading a game's name from its metadata. Pure parsing, so it runs and is
 * tested on the desktop. The console build reads the files. */
#pragma once

#include <stddef.h>

/* The name to match a PS5 game by, from the text of its param.json. The
 * file holds one name per language, in alphabetical order, so the first one
 * can be Arabic or Spanish. IGDB and Floppy use English titles, so the
 * order is: en-US, the game's default language, en-GB, then the first name
 * in the file. Returns 0, or -1 if there is no name. */
int appmeta_title(const char *param_json, char *out, size_t size);
