#!/bin/sh
set -eu
CATDESK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CATDESK_CHECK_DIR=$(mktemp -d /tmp/catdesk-check.XXXXXX)
trap 'rm -rf "$CATDESK_CHECK_DIR"' EXIT HUP INT TERM
cd "$CATDESK_ROOT"
set -- -std=c11 -O1 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ifirmware/components/catdesk_ui/include
clang "$@" tools/test_model.c firmware/components/catdesk_ui/catdesk_model.c -o "$CATDESK_CHECK_DIR/model"
"$CATDESK_CHECK_DIR/model"
clang "$@" tools/test_link.c firmware/components/catdesk_ui/catdesk_model.c firmware/components/catdesk_ui/catdesk_link.c -o "$CATDESK_CHECK_DIR/link"
"$CATDESK_CHECK_DIR/link"
clang "$@" tools/test_ui.c firmware/components/catdesk_ui/catdesk_model.c \
  firmware/components/catdesk_ui/catdesk_render.c firmware/components/catdesk_ui/catdesk_couple.c firmware/components/catdesk_ui/catdesk_room.c firmware/components/catdesk_ui/catdesk_cats.c \
  firmware/components/catdesk_ui/catdesk_font.c firmware/components/catdesk_ui/pixel_ui.c -o "$CATDESK_CHECK_DIR/ui"
"$CATDESK_CHECK_DIR/ui"
clang "$@" tools/test_pixel_ui.c firmware/components/catdesk_ui/pixel_ui.c \
  firmware/components/catdesk_ui/catdesk_font.c -o "$CATDESK_CHECK_DIR/pixel-ui"
"$CATDESK_CHECK_DIR/pixel-ui"
python3 tools/test_presenter.py
python3 tools/test_persistence.py
python3 tools/test_feedback.py
clang "$@" tools/test_room.c firmware/components/catdesk_ui/catdesk_model.c \
  firmware/components/catdesk_ui/catdesk_room.c -o "$CATDESK_CHECK_DIR/room"
"$CATDESK_CHECK_DIR/room"
python3 tools/test_room_integration.py
clang "$@" tools/test_welcome.c firmware/components/catdesk_ui/catdesk_welcome.c \
  firmware/components/catdesk_ui/catdesk_model.c firmware/components/catdesk_ui/catdesk_render.c firmware/components/catdesk_ui/catdesk_couple.c \
  firmware/components/catdesk_ui/catdesk_room.c firmware/components/catdesk_ui/catdesk_font.c \
  firmware/components/catdesk_ui/catdesk_cats.c firmware/components/catdesk_ui/pixel_ui.c \
  -o "$CATDESK_CHECK_DIR/welcome"
"$CATDESK_CHECK_DIR/welcome"

clang "$@" tools/test_couple.c firmware/components/catdesk_ui/catdesk_model.c \
  firmware/components/catdesk_ui/catdesk_render.c firmware/components/catdesk_ui/catdesk_couple.c \
  firmware/components/catdesk_ui/catdesk_room.c firmware/components/catdesk_ui/catdesk_font.c \
  firmware/components/catdesk_ui/catdesk_cats.c firmware/components/catdesk_ui/pixel_ui.c \
  -o "$CATDESK_CHECK_DIR/couple"
"$CATDESK_CHECK_DIR/couple"
