/* Watches which game is on screen and counts the time it is played. */
#pragma once

#include "app.h"

/* Runs forever on the calling thread. */
void tracker_run(struct app *app);
