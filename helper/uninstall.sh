#!/bin/sh
set -eu
HELPER_PLIST="$HOME/Library/LaunchAgents/com.catdesk.helper.plist"
launchctl bootout "gui/$(id -u)/com.catdesk.helper" 2>/dev/null || true
rm -f "$HELPER_PLIST" "$HOME/Library/Application Support/CatDesk/CatDeskHelper"
rmdir "$HOME/Library/Application Support/CatDesk" 2>/dev/null || true
printf 'CatDeskHelper removed. Saved device preferences and logs are preserved.\n'
