# Changelog

What changed in each version, for people using the tracker. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions below 1.0 are early: expect changes. Versions up to 0.1.4 were built and tested on one console and were not published.

## Unreleased

### Added

- `make sdk` fetches the pinned PS5 payload SDK (v0.43) and checks it.
- Tests and a console build run on every pull request.
- Releases are built and published from GitHub Actions.

### Changed

- Licensed under GPL-3.0, like the PS5 payload SDK it is built on.
- README rewritten: install steps for a downloaded release, how playtime is counted, and a development guide.

## 0.1.4 - 2026-10-06

### Fixed

- Game names are read in English. The tracker took the first name in the game's metadata, which can be another language: Marvel's Wolverine came out in Arabic and could not be matched. Names already saved are corrected when the tracker starts.

### Added

- Every waiting game on the page has a line, including "Not checked yet: the game is open."
- The page says "Not saved yet" while the Send switch differs from the saved setting.

## 0.1.3 - 2026-10-06

### Added

- **Send playtime to Floppy**, a switch on the config page, off by default. When on, whole minutes are added to the game's entry after the game closes.
- Games not in the library are added as In progress with their minutes, but only when IGDB has exactly one game of that name on PS5 or PS4, and nothing in the library could already be it.
- A warning when Floppy's total for a game changed outside the tracker, for example by a Steam import in overwrite mode.

### Changed

- Every write is saved on the console before it is sent and checked against Floppy afterwards. A crash, a timeout or an error reply can no longer make the same minutes count twice.
- After launch, nothing is sent until the tracker has checked whether a game is on screen.

## 0.1.2 - 2026-10-06

### Added

- Test mode for sending: the tracker finds each game in the Floppy library, in any edition, or on IGDB, and shows on the page what it would send. Nothing is written.
- Each game's Floppy match is remembered, so a later edition added to the library does not take the time away from the one in use.

## 0.1.1 - 2026-10-05

### Added

- Playtime counting. Time counts only while the game is on screen, pauses on the home screen, and skips rest mode. Sessions under two minutes are dropped.
- Counted time is saved on the console every minute and survives the payload being stopped or replaced.
- The page shows the game being played and the time waiting to be sent.

## 0.1.0 - 2026-10-05

### Added

- The background payload: one copy at a time, named `ps-floppy.elf` in Payload Manager.
- The config page on port 8765: Floppy address, API token, and Save and test.
- The token is write-only: the page shows its last four characters, never the whole token.
- The config page refuses requests from other websites.

