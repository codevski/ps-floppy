/* The config page and its small JSON API, served on the LAN. */
#pragma once

#include "app.h"

/* Thread entry point. Serves until the process exits, and rebuilds its
 * listening socket if it stops working (as it can after rest mode). */
void *http_server_run(void *app);
