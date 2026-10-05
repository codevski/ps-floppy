/* The few things that differ between the console and a desktop test build. */
#pragma once

#include <stddef.h>

/* Name the process and replace any copy that is already running, so there is
 * only ever one tracker. Call first. */
void plat_init(void);

/* Show a message to the user: an on-screen notification on the console,
 * standard output on a desktop. */
void plat_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Which game is running, if any. Returns 1 and fills `title_id` when a game
 * is open, 0 when none is. `focused` is 0 while the game is minimized (the
 * user is on the home screen), 1 while it is the app on screen. */
int plat_current_game(char title_id[10], int *focused);

/* The game's display name as the console knows it. Returns 0 on success. */
int plat_game_name(const char *title_id, char *out, size_t size);

/* Directory for the config file, the game table and the log. */
const char *plat_data_dir(void);
