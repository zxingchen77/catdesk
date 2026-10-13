#!/bin/sh
# Explicit opt-in only; this script installs a per-user login LaunchAgent.
set -eu
HELPER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HELPER_DEST="$HOME/Library/Application Support/CatDesk"
HELPER_PLIST="$HOME/Library/LaunchAgents/com.catdesk.helper.plist"
HELPER_LABEL="gui/$(id -u)/com.catdesk.helper"
"$HELPER_DIR/build.sh"
mkdir -p "$HELPER_DEST" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs/CatDesk"
launchctl bootout "$HELPER_LABEL" 2>/dev/null || true
cp "$HELPER_DIR/build/CatDeskHelper" "$HELPER_DEST/CatDeskHelper"
# PlistBuddy creates and quotes each property rather than interpolating XML.
rm -f "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c 'Add :Label string com.catdesk.helper' "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c 'Add :ProgramArguments array' "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c "Add :ProgramArguments:0 string $HELPER_DEST/CatDeskHelper" "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c 'Add :RunAtLoad bool true' "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c 'Add :KeepAlive bool true' "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c 'Add :ThrottleInterval integer 10' "$HELPER_PLIST"
/usr/libexec/PlistBuddy -c "Add :StandardErrorPath string $HOME/Library/Logs/CatDesk/helper.log" "$HELPER_PLIST"
launchctl bootstrap "gui/$(id -u)" "$HELPER_PLIST"
printf 'Installed CatDeskHelper for this user. Log: %s\n' "$HOME/Library/Logs/CatDesk/helper.log"
printf 'Stop it before flashing: launchctl bootout %s\n' "$HELPER_LABEL"
