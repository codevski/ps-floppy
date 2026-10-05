# ps-floppy

Tracks PS5 playtime into a self-hosted [Floppy](https://github.com/dannyvfilms/Floppy) library. One background payload on a jailbroken console, talking straight to Floppy over your home network. No companion service, no PSN.

**Status: 0.1.4, early.** Counts playtime on the console and can send it to Floppy. Sending is off until you turn it on. Check your library after the first few games.

## What you need

- A jailbroken PS5 that can load payloads, for example through Payload Manager or an ELF loader. Tested on firmware 13.60 only.
- A self-hosted Floppy instance with an API token. Tested against Floppy `v26.9.17`. Floppy's API is young and may change.
- Floppy reachable from the console over plain HTTP on your home network. HTTPS is not supported yet.

## Install

1. Download `ps-floppy.elf` from the [Releases](../../releases) page. Each release lists its SHA-256 if you want to check the file.
2. Load it like any other payload: upload it in Payload Manager, or send it to an ELF loader (usually port 9021). A notification shows the address of the config page.
3. Open `http://<console-ip>:8765` from a phone or computer on the same network.
4. Enter your Floppy address and API token, then choose **Save and test**. The token comes from Floppy: Settings, then Integrations.
5. Play a game for at least two minutes and close it. The page shows what the tracker would send. When those lines look right, tick **Send playtime to Floppy** and choose **Save and test** again.

The jailbreak does not survive a reboot, so load the payload again after each one. Loading it while it is already running replaces the old copy. At most the last minute of counted time is lost.

## How playtime is counted

- Time counts only while the game is on screen. Minimized on the home screen, it pauses.
- Sessions shorter than two minutes are dropped.
- Rest mode is not counted.
- Counted time is saved on the console every minute, so a crash or a stopped payload loses at most a minute.
- Nothing is sent to Floppy while a game is on screen.

## Sending playtime to Floppy

Until you tick **Send playtime to Floppy** on the config page, the tracker only shows what it would send, under each game. Read those lines before turning it on, especially any "Would add" for a game that is not in your library.

Once it is on, after a game closes:

- If the game is in your library, in any edition, its whole minutes are added to that entry. Status, dates and notes are left alone. A game you also own on another platform counts together, if it is the same IGDB game.
- If it is not, and nothing in your library looks like it, the game is added as In progress with its minutes.
- If it is unclear (two possible entries, or a similar name), nothing is sent and the game shows "needs a match". The time stays on the console.

Each write is saved on the console before it is sent and checked against Floppy afterwards, so a crash or a dropped connection cannot add the same minutes twice.

**Steam imports in Floppy.** If Floppy imports your Steam library on a schedule in "overwrite" mode, each import sets a game's playtime back to Steam's number, replacing any PS5 minutes on a game you own on both. The tracker notices and says so on the page, but cannot bring the minutes back. Floppy's "new" import mode leaves existing games alone.

**Needs attention.** If a write could not be confirmed and Floppy's total then changed for another reason, the tracker stops writing that game and marks it. To release it: stop the tracker in Payload Manager, then in `/data/ps-floppy/games.tsv` empty the 6th, 7th and 8th columns of that game's line (the write it was waiting on: `update` or `track` and two totals), and start it again. Editing while it runs does not work, because the tracker rewrites the file.

## Files on the console

Everything lives in `/data/ps-floppy/`:

| File | Contents |
| --- | --- |
| `config.json` | Floppy address, token, and the send switch |
| `games.tsv` | Per game: waiting playtime, the matched Floppy entry, and any write in progress |
| `ps-floppy.log` | What the tracker did and why |

## Security

- The Floppy API token grants full read and write access to your Floppy account. It is stored in plain text at `/data/ps-floppy/config.json` on the console. Anyone with file access to the console can read it.
- The config page has no login. Anyone on your network can change the settings.
- The page never sends the saved token back to a browser. It shows the last four characters only.
- Requests from other websites are refused, so a page open in your browser cannot silently repoint the tracker at another server.
- Floppy is reached over plain HTTP. Use it on a network you trust.

## Building

The [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) lives inside the project, in `.sdk/`. `make sdk` fetches the pinned version and checks its SHA-256.

The SDK needs LLVM 18. On macOS: `brew install llvm@18`, found automatically. On Debian or Ubuntu: `apt install clang-18 lld-18 llvm-18`, then `export LLVM_CONFIG=llvm-config-18`.

| Command | What it does |
| --- | --- |
| `make` | Builds `ps-floppy.elf` and its Payload Manager card |
| `make push` | Uploads both to Payload Manager over FTP. Needs `PS5_HOST` (the console's address) and an FTP server on the console, port 1337 by default, override with `FTP_PORT=` |
| `make check` | Runs the tests on this machine |
| `make run-host` | Runs the tracker on this machine at http://localhost:8765 |

Set `PS5_HOST` once in your shell instead of on every `make push`, for example `export PS5_HOST=<console-ip>` (fish: `set -Ux PS5_HOST <console-ip>`).

Bump `VERSION` in the Makefile for every build that goes to a console. It shows on the Payload Manager card, in the start-up notification, in the log and at the bottom of the config page.

## Development

Everything except reading the console's state builds and runs on a desktop. `src/platform_ps5.c` holds all console-specific code; `src/platform_host.c` stands in for it.

**Tests.** `make check` builds the tests with the address and undefined-behaviour sanitizers and runs them. The Floppy tests go through the real HTTP client against `tests/mock_floppy.py`, a stand-in Floppy that follows the real API's behaviour, including its traps. It needs `python3`.

**The desktop build.** `make run-host` serves `web/index.html` straight from disk, so edit the page and refresh. Settings, the game table and the log go to `.host-data/`. To simulate a game, write one line to `.host-data/fake-game`: title ID, `1` for on screen or `0` for minimized, then the name. Delete the file to close the game.

```
echo 'PPSA21159 1 SILENT HILL f' > .host-data/fake-game
```

To watch sync against the mock, run `python3 tests/mock_floppy.py 18765` and point the desktop build at `http://127.0.0.1:18765` with the token `good-token`. The timers can be shortened for testing:

```
make clean
make host HOST_DEFS='-DPSF_POLL_SECONDS=1 -DPSF_SYNC_SECONDS=10 -DPSF_MIN_SESSION_SECONDS=5'
```

**Includes.** Files that use the FreeBSD headers keep `#include <sys/types.h>` first, in its own `clang-format off` block. Sorted after the others, the build fails.

## Layout

| Path | Purpose |
| --- | --- |
| `src/main.c` | Start-up and the log |
| `src/tracker.c` | Watches the running game and counts time on screen |
| `src/store.c` | Per-game waiting playtime, saved to `games.tsv` |
| `src/sync.c` | Sending waiting playtime, on its own thread |
| `src/match.c` | Which Floppy entry a console game belongs to |
| `src/floppy.c` | The calls to Floppy: searches, update, track |
| `src/writes.c` | Checking writes and settling them after a crash |
| `src/appmeta.c` | A game's name from its metadata, English first |
| `src/http_server.c` | Config page and its JSON API |
| `src/http_client.c` | Requests to Floppy |
| `src/config.c` | Settings validation and storage |
| `src/json.c` | Minimal JSON reading and escaping |
| `src/platform_ps5.c` | Console specifics: game detection, names, process name, notifications |
| `src/platform_host.c` | Desktop stand-in for the above |
| `web/index.html` | The config page, embedded into the console build |
| `tests/` | Tests and the mock Floppy |

## Licence

GPL-3.0, see `LICENSE`. The PS5 payload SDK this builds on is GPL-3.0 too, and its start-up code is part of every payload.
