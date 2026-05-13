sc360se Linux package

Direct CLI use without installing:
  sudo ./usr/bin/sc360se read

Recommended installation:
  sudo ./install.sh

Uninstall:
  sudo ./uninstall.sh

The installer copies the CLI, installs the udev rule, reloads udev, and
retriggers hidraw devices. The udev rule grants the active local desktop user
access to the SC360SE hidraw interface, so normal CLI and GUI use should not
require sudo after install.

If access still fails after installation, unplug/replug the mouse or reconnect
the 2.4G dongle.

GUI runtime dependencies are not bundled. On Arch Linux:
  sudo pacman -S python-gobject gtk4
