# sc360se — Linux user-space driver for the AULA SC360SE mouse

A Linux port of the AULA SC360SE Windows configuration utility.
PAW3395-based wireless gaming mouse, configured via 32-byte HID
Output/Input Reports on the vendor interface (`bInterfaceNumber=2`).

| Mode        | VID    | PID    | Iface |
|-------------|--------|--------|-------|
| USB wired   | `248A` | `5D2E` | 02    |
| 2.4G dongle | `249A` | `5C2F` | 02    |

## Status — verified against captures

Every byte sequence the Linux driver generates is **byte-for-byte
identical** to what the Windows driver sends. Verified by `tests/selftest.c`
against 9 captured frames (run `make test`).

| Command | Verified | Source |
|---|---|---|
| Discovery handshake (0x10..0x17, 0x20) | ✅ | wireshark_catch1 |
| Polling rate (1000/500/250/125 Hz) | ✅ | wireshark_catch1 |
| DPI 6-stage X/Y (cpi/100 LE u16) | ✅ | 把DPI档位2改为5678 |
| DPI per-stage colors | ✅ | DPI color captures |
| Config readback (0x11-0x17) | ✅ | 连接+切换DPI.pcapng |
| Battery (0x10 reply, 0xc0 notify) | ✅ | 连接+切换DPI.pcapng |
| DPI button notify (0xc2) | ✅ | 连接+切换DPI.pcapng |
| Sleep timeout | ✅ | wireshark_catch1 |
| Button mapping (5 safe slots + locked DPI key) | ✅ | side / wheel / DPI key captures |
| Factory reset (op 0x0f) | ✅ | DPI配置.pcapng |
| Static DPI light restore (op 0x06) | ✅ | live test + profile captures |
| Profile switching | ✅ | host-side rewrite of full config |

## Build & install

```sh
make
make test          # run protocol unit tests
sudo make install  # CLI + udev rule
sudo make install-gui  # CLI + udev + Python/GTK4 GUI + .desktop entry
sudo udevadm control --reload && sudo udevadm trigger --action=add --subsystem-match=hidraw
```

GUI dependency: `python-gobject` + `gtk4` (Arch: `pacman -S python-gobject gtk4`).

Zero deps for the C driver itself — libc + Linux ≥ 2.6 (`hidraw`).

## GUI

```
sc360se-gui
```

A single-window app:

- **Header bar** — refresh status, restore static light, *Apply to mouse* (suggested action)
- **Profile bar** — dropdown of saved profiles with Load / Save as / Delete
- **DPI section** — active-stage selector, count spinner, 6 rows of
  spin + slider + color picker (sliders snap to 100/500 cpi)
- **Buttons section** — 6 rows, each with a type dropdown that swaps
  between mouse-button / DPI / disable / keyboard / consumer parameter
  widgets
- **Other** — polling rate (125/250/500/1000 Hz), sleep timeout

Profiles live in `~/.config/sc360se/*.cfg` (auto-seeded from the
shipped `profile-1-default.cfg`, `profile-2-fps.cfg`,
`profile-3-office.cfg` on first run). The same files work with the
CLI: `sc360se apply ~/.config/sc360se/profile-2-fps.cfg`.

## CLI use

```sh
sc360se info
sc360se read                     # dump current config from device
sc360se battery                  # read battery percentage
sc360se polling 1000
sc360se dpi 0 4  800 1600 3200 6400        # active=0, 4 stages enabled
sc360se dpi 0 2  800/1200 1600             # X/Y per stage
sc360se color 2 #00FF00                    # stage 2 color
sc360se sleep 90

# Buttons (slot 0=L 1=R 2=wheel 3=back-side 4=front-side 5=DPI-key)
sc360se button 4 mouse left
sc360se button 4 dpi cycle
sc360se button 4 disable
sc360se button 4 key 0x06 mod=ctrl         # Ctrl+C (HID usage 0x06)
sc360se button 4 consumer 0xe9             # Volume Up

sc360se static-light                       # restore constant DPI LED mode

# Low-level
sc360se send  09 00 01 0f 10 01 00 ...     # auto-checksums
sc360se recv 1000                          # dump next IN frame
sc360se monitor                            # follow raw frames
sc360se watch                              # decoded battery & DPI events
```

## Wire format

```
[0]      command class
[1]      0x00 = request, 0x01 = ack, 0x02 = error
[2]      0x00 = meta, 0x01 = config, 0xff = error context
[3]      sub-command
[4..30]  payload
[31]     checksum = sum(bytes[4..30]) mod 256
```

| Op  | Sub | Function | Payload key bytes |
|-----|-----|----------|-------------------|
| 0x02 | 0x01 | polling rate | [4]: 1=1000Hz 2=500 4=250 8=125 |
| 0x03 | 0x25 | DPI config | [4]=(active<<4)\|count; [5..28]=6×(X u16, Y u16), value=cpi/100 |
| 0x04 | 0x12 | DPI colors | [4..21]=6×(R G B); [22..27]=6×stored flag bytes (0x00 in static-light captures) |
| 0x06 | 0x05 | static DPI light mode | [4]=02 restores constant DPI LED after host-side DPI writes |
| 0x07 | 0x04 | sleep timeout | [4..5]=u16 LE seconds; [7]=0x08 |
| 0x09 | 0x0f | button mapping | 6 slots × 3 bytes (type, p1, p2); slot 5 is locked safe |
| 0x0f | 0x01 | factory reset | [4]=ff; also clears hidden DPI-key remap state |
| 0x10 | — | device info | reply has "S057" + version; [13]=battery% |
| 0x11 | — | button readback | query: bare op; reply has 6×3-byte slots |
| 0x12 | — | polling readback | query: bare op; reply has rate code at [4] |
| 0x13 | — | DPI readback | query: bare op; reply sub=0x19, [4..28]=config |
| 0x14 | — | color readback | query: bare op; reply sub=0x12, [4..27]=6×RGBF |
| 0x17 | — | sleep readback | query: bare op; reply sub=0x05, [4..5]=seconds |
| 0xc0 | — | battery notify | **unsolicited IN**; [2]=battery% 0-100 |
| 0xc2 | — | DPI notify | **unsolicited IN**; [1]=(active<<4)\|count, [2..3]=cpi/100 LE |

All readback queries (0x11-0x17, 0x20) use a bare opcode frame:
`op 00 00 00 00...00` — the device fills [2]=0x01 and [3]=sub in the reply.

**Important DPI-key caveat:** slot 5 is the physical DPI key. Captures show it
is not a normal remappable button: writing `DPI+`/`DPI-`/other actions to this
slot puts the firmware into a hidden remap state where local DPI switching stops
and the key may emit no useful function. Writing the visible mapping back to
`DPI cycle` does **not** repair it; only `0x0f 00 01 01 ff ...` factory reset
restores the hidden firmware path. For safety, high-level APIs, CLI and GUI
force slot 5 to factory `DPI cycle`. Use side buttons for remappable DPI+/DPI-.

**Button action types** (slot byte 0):

| Type | Meaning | param1 | param2 |
|------|---------|--------|--------|
| 0x10 | mouse button | bitmask: 1=L 2=R 4=M 8=fwd 0x10=back | 0 |
| 0x40 | DPI control | 1=cycle 2=up 3=down | 0 |
| 0x60 | disable | 0 | 0 |
| 0x70 | keyboard key | modifier bitmask | HID usage code |
| 0x80 | consumer page | LE u16 usage low | LE u16 usage high |

**Keyboard modifier bits** (0x70 param1):
0x01=LCtrl 0x02=LShift 0x04=LAlt 0x08=LGUI 0x10=RCtrl 0x20=RShift 0x40=RAlt 0x80=RGUI

## Hardware capability ceiling

The Windows driver is **not "stripped down"** — it just exposes
everything the firmware actually accepts. After exhaustive captures we
found *no command at all* for:

- full LED effect editing (only static DPI-light restore is implemented)
- LED brightness
- LOD (lift-off distance)
- Button debounce
- Sub-100 cpi DPI granularity (or sub-500 above 5000 cpi)

The PAW3395 sensor itself supports 50 cpi steps and configurable LOD,
but those registers are not wired through to a host-visible command in
this firmware. To add those features you would need to either:

1. **Modify firmware** — requires the MCU's flash dump, toolchain, and
   reverse-engineered code; impractical without a hardware debug port.
2. **Fuzz undocumented opcodes** — try every (op, sub) combo and see
   which produce non-error replies. `sc360se send …` lets you do this
   one frame at a time. High-risk: a bad write to a sensor register can
   brick the mouse.
3. **Software workarounds** — debounce can be done in libinput via a
   per-device quirk; LOD can't be worked around in software at all.

The honest summary: **a Linux driver for this device cannot do more
than the Windows driver does without modifying firmware.** This driver
matches the Windows feature set 1:1, plus a `send` / `recv` /
`monitor` escape hatch for further reverse-engineering.

## Files

```
src/sc360se.h     — public API
src/device.c      — hidraw discovery + frame I/O
src/protocol.c    — verified high-level commands
src/main.c        — CLI
tests/selftest.c  — byte-exact comparison vs captures
udev/             — uaccess rule
```
