# ps-floppy: PS5 playtime tracker for Floppy.
#
# Everything stays inside the project folder. The SDK is expected at
# ./.sdk/ps5-payload-sdk, so nothing is installed to /opt.
#
#   make sdk                          fetch the pinned PS5 payload SDK
#   make                              build the console ELF and its card
#   make push PS5_HOST=<console-ip>   upload both to Payload Manager over FTP
#   make host                         build a desktop copy for working on the page
#   make run-host                     build and run it at http://localhost:8765
#   make check                        run the host tests
#
# Bump VERSION on every change. It is compiled into the payload and written to
# the card Payload Manager shows.

VERSION := 0.1.4

NAME := ps-floppy
ELF  := $(NAME).elf
CARD := $(ELF).json
HOST := build/$(NAME)-host

PS5_HOST ?= ps5
PS5_PORT ?= 9021

# Where Payload Manager keeps this payload, and the FTP server to reach it.
FTP_PORT   ?= 1337
PLDMGR_DIR ?= /data/pldmgr/payloads/$(NAME)

COMMON_SRC := src/main.c src/json.c src/config.c src/store.c src/tracker.c \
              src/http_client.c src/http_server.c src/page.c src/match.c \
              src/floppy.c src/sync.c src/writes.c src/appmeta.c
HEADERS    := $(wildcard src/*.h)
DEFS       := -DPSF_VERSION='"$(VERSION)"'
WARN       := -Wall -Wextra -Werror

# The PS5 payload SDK, pinned so every build, local or CI, uses the same one.
SDK_VERSION := v0.43
SDK_SHA256  := a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb
SDK_URL     := https://github.com/ps5-payload-dev/sdk/releases/download/$(SDK_VERSION)/ps5-payload-sdk.zip

# Compiler for the desktop build and the tests.
HOST_CC ?= cc

.PHONY: all sdk host run-host push test check clean
all: $(ELF) $(CARD)

# ---- console build --------------------------------------------------------
# Only pulled in when a console target is asked for, so `make host` works on a
# machine with no SDK.

HOST_ONLY_GOALS := sdk host run-host check clean
ifneq ($(filter-out $(HOST_ONLY_GOALS),$(or $(MAKECMDGOALS),all)),)

# := on purpose: a stale PS5_PAYLOAD_SDK in the shell must not win.
PS5_PAYLOAD_SDK := $(CURDIR)/.sdk/ps5-payload-sdk

# macOS: use Homebrew's LLVM 18 if the caller has not chosen one.
ifeq ($(shell uname -s),Darwin)
    LLVM_CONFIG ?= $(shell brew --prefix llvm@18 2>/dev/null)/bin/llvm-config
    export LLVM_CONFIG
endif

ifeq ($(wildcard $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk),)
    $(error SDK not found at $(PS5_PAYLOAD_SDK). Run `make sdk` to fetch it)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

# The page is baked into the ELF, so it is a build input too.
$(ELF): $(COMMON_SRC) src/platform_ps5.c $(HEADERS) web/index.html Makefile
	$(CC) $(WARN) -O2 -g $(DEFS) -o $@ $(COMMON_SRC) src/platform_ps5.c -lpthread -lSceSystemService

endif

# Metadata file Payload Manager reads for the name and version on its card.
$(CARD): $(ELF)
	@sum=$$( (sha256sum $(ELF) 2>/dev/null || shasum -a 256 $(ELF)) | cut -d' ' -f1 ); \
	printf '%s\n' \
	  '{' \
	  '  "url": "",' \
	  '  "source": "",' \
	  '  "source_direct": "",' \
	  '  "last_update": "$(shell date +%Y-%m-%d)",' \
	  '  "version": "$(VERSION)",' \
	  '  "install_source": "manual_upload",' \
	  '  "install_source_detail": "$(PLDMGR_DIR)",' \
	  '  "name": "Floppy Tracker",' \
	  '  "filename": "$(ELF)",' \
	  '  "description": "Sends PS5 playtime to a self-hosted Floppy library. Config page on port 8765.",' \
	  "  \"checksum\": \"$$sum\"" \
	  '}' > $@
	@echo "wrote $@ (version $(VERSION))"

# Upload the ELF and its card into Payload Manager's folder over FTP.
push: all
	curl -sS --ftp-create-dirs -T $(ELF)  ftp://$(PS5_HOST):$(FTP_PORT)$(PLDMGR_DIR)/$(ELF)
	curl -sS --ftp-create-dirs -T $(CARD) ftp://$(PS5_HOST):$(FTP_PORT)$(PLDMGR_DIR)/$(CARD)
	@echo "pushed $(ELF) $(VERSION) to $(PS5_HOST):$(PLDMGR_DIR)"

# Send straight to an ELF loader, if one is listening on PS5_PORT.
test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(ELF)

# Fetch the pinned SDK into ./.sdk and check it before unpacking.
sdk:
	@mkdir -p .sdk
	curl -fsSL -o .sdk/ps5-payload-sdk.zip $(SDK_URL)
	@sum=$$( (sha256sum .sdk/ps5-payload-sdk.zip 2>/dev/null || \
	          shasum -a 256 .sdk/ps5-payload-sdk.zip) | cut -d' ' -f1 ); \
	if [ "$$sum" != "$(SDK_SHA256)" ]; then \
	  echo "SDK checksum does not match: got $$sum"; \
	  rm -f .sdk/ps5-payload-sdk.zip; exit 1; \
	fi
	rm -rf .sdk/ps5-payload-sdk
	unzip -q -d .sdk .sdk/ps5-payload-sdk.zip
	@echo "SDK $(SDK_VERSION) ready in .sdk/ps5-payload-sdk"

# ---- desktop build --------------------------------------------------------
# Same server and page, run on this machine. It reads web/index.html from disk
# on every request, so page edits show on refresh. Settings go to ./.host-data.

host: $(HOST)

$(HOST): $(COMMON_SRC) src/platform_host.c $(HEADERS) Makefile
	@mkdir -p build
	$(HOST_CC) $(WARN) -O1 -g -DHOST_BUILD $(DEFS) $(HOST_DEFS) -o $@ $(COMMON_SRC) src/platform_host.c -lpthread

run-host: $(HOST)
	./$(HOST)

# Host tests. Named `check` because `test` already sends to the console.
TEST_CC = $(HOST_CC) $(WARN) -O1 -g -fsanitize=address,undefined

# The Floppy tests talk to tests/mock_floppy.py on this port.
MOCK_PORT := 18765

check: build/json-test build/match-test build/writes-test build/appmeta-test \
       build/floppy-test
	./build/json-test
	./build/match-test
	./build/writes-test
	./build/appmeta-test
	@python3 tests/mock_floppy.py $(MOCK_PORT) & pid=$$!; sleep 1; \
	./build/floppy-test $(MOCK_PORT); rc=$$?; kill $$pid; exit $$rc

build/json-test: tests/json_test.c tests/fixtures.h src/json.c src/json.h
	@mkdir -p build
	$(TEST_CC) -o $@ tests/json_test.c src/json.c

build/match-test: tests/match_test.c tests/fixtures.h src/match.c src/match.h \
                  src/json.c src/json.h
	@mkdir -p build
	$(TEST_CC) -o $@ tests/match_test.c src/match.c src/json.c

build/writes-test: tests/writes_test.c tests/fixtures.h src/writes.c \
                   src/writes.h src/store.c src/store.h src/match.h \
                   src/json.c src/config.c src/config.h
	@mkdir -p build
	$(TEST_CC) -o $@ tests/writes_test.c src/writes.c src/store.c src/json.c \
	  src/config.c

build/appmeta-test: tests/appmeta_test.c tests/fixtures.h src/appmeta.c \
                    src/appmeta.h src/json.c src/json.h
	@mkdir -p build
	$(TEST_CC) -o $@ tests/appmeta_test.c src/appmeta.c src/json.c

FLOPPY_TEST_SRC := tests/floppy_test.c src/floppy.c src/match.c src/json.c \
                   src/http_client.c src/config.c src/writes.c

build/floppy-test: $(FLOPPY_TEST_SRC) $(HEADERS) tests/mock_floppy.py
	@mkdir -p build
	$(TEST_CC) -o $@ $(FLOPPY_TEST_SRC)

clean:
	rm -rf $(ELF) $(CARD) build
