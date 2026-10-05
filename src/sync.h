/* Sends waiting playtime to Floppy, on its own thread so the tracker never
 * waits on the network. It never works on the open game, and never while a
 * game is on screen.
 *
 * For each game with whole minutes waiting: settle any write left unconfirmed
 * by a crash or a timeout, find the game's entry (the user's library first,
 * then IGDB), then add the minutes, or track the game if every guard against
 * a duplicate passes. With "Send playtime to Floppy" off, it does every read
 * and only reports the write it would make. */
#pragma once

#include "app.h"

/* Thread entry point. `arg` is the struct app. Runs forever. */
void *sync_run(void *arg);
