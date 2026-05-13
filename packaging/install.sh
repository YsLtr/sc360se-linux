#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "Run this installer as root: sudo ./install.sh" >&2
  exit 1
fi

install -Dm755 usr/bin/sc360se /usr/bin/sc360se
install -Dm644 usr/lib/udev/rules.d/60-aula-sc360se.rules \
  /usr/lib/udev/rules.d/60-aula-sc360se.rules

if [ -f usr/bin/sc360se-gui ]; then
  install -Dm755 usr/bin/sc360se-gui /usr/bin/sc360se-gui
  install -Dm644 usr/share/applications/sc360se-gui.desktop \
    /usr/share/applications/sc360se-gui.desktop
  install -d /usr/share/sc360se/profiles
  install -m644 usr/share/sc360se/profiles/*.cfg \
    /usr/share/sc360se/profiles/
fi

udevadm control --reload-rules
udevadm trigger --action=add --subsystem-match=hidraw

echo "Installed sc360se."
echo "If non-root access still fails, unplug/replug the mouse or reconnect the 2.4G dongle."
