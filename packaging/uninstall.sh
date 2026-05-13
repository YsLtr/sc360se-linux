#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  echo "Run this uninstaller as root: sudo ./uninstall.sh" >&2
  exit 1
fi

rm -f /usr/bin/sc360se
rm -f /usr/bin/sc360se-gui
rm -f /usr/share/applications/sc360se-gui.desktop
rm -rf /usr/share/sc360se
rm -f /usr/lib/udev/rules.d/60-aula-sc360se.rules

udevadm control --reload-rules
udevadm trigger --action=add --subsystem-match=hidraw

echo "Uninstalled sc360se."
