# SC360SE Reverse Engineering Notes

Last updated: 2026-05-12

This file records current reverse-engineering state so future sessions do not
repeat finished static checks or resurrect refuted protocol names.

## Working Rules

- Treat live visible LED behavior and captured host/device frames as stronger
  evidence than XML names, UI labels, or ACK/readback alone.
- Do not run multiple `sc360se`, `sc360se-gui`, `status`, `read`, `monitor`, or
  raw recv processes against hidraw at the same time. They can steal replies
  from each other and create shifted or partial evidence.
- For raw send/recv validation, prefer one process or start recv first.
- Keep `0x06/0x05` names provisional in old Linux UI/code paths. Current EXE
  reversing maps it to sensor advanced, not visible light control.

## Files Already Checked

- `extracted/app/device/mouse_sc360se.xml`
- `extracted/app/config.xml`
- `extracted/app/language/1033.lan`
- `extracted/app/language/2052.lan`
- `extracted/app/MUI.dll`
- `extracted/app/mfc140u.dll`
- `extracted/app/AULA SC360SE.exe`
- Existing Linux driver notes/code under `sc360se-linux/`
- Capture inventory via `capinfos` for all root `*.pcapng`

Useful commands already run:

```sh
rg -n "move_close|closelight|close|light|Light|LIGHT|led|LED|dpi|DPI|battery|report|profile|dev_mode|mode|wireless|sleep|sleep_time|rate|poll|color|rgb|RGB" \
  extracted/app/device/mouse_sc360se.xml extracted/app/config.xml \
  extracted/app/language/1033.lan extracted/app/language/2052.lan \
  sc360se-linux/src sc360se-linux/tests sc360se-linux/README.md

iconv -f UTF-16LE -t UTF-8 extracted/app/language/1033.lan | sed -n '1,120p;140,180p;246,286p;500,540p'
iconv -f UTF-16LE -t UTF-8 extracted/app/language/2052.lan | sed -n '1,120p;140,180p;246,286p;500,540p'

rabin2 -I extracted/app/MUI.dll extracted/app/mfc140u.dll 'extracted/app/AULA SC360SE.exe'
rabin2 -E extracted/app/MUI.dll extracted/app/mfc140u.dll 'extracted/app/AULA SC360SE.exe'
rabin2 -i extracted/app/MUI.dll
rabin2 -i 'extracted/app/AULA SC360SE.exe'

strings -a -el extracted/app/MUI.dll | rg -i "light|dpi|mouse|hid|device|report|sleep|wake|close|move|sensor|lod|color|profile|firmware|battery|xml|ini|registry|reg"
strings -a -el extracted/app/mfc140u.dll | rg -i "Microsoft|MFC|Version|mfc|Dll|CWinApp|Afx|light|dpi|hid|mouse|SC360|AULA"

capinfos -c -d -a -e -M *.pcapng
```

## Official XML / UI Metadata

`extracted/app/device/mouse_sc360se.xml` says:

- `<menu light="0" />`
- `<custom show_dpitips="0" show_savetips="0" show_lod="0" show_sensor="0" show_respondtime="0"/>`
- `dpi_info default_dpi="1" dpi_count="6"`
- DPI defaults:
  - 1: `800`, `#FF0000`
  - 2: `1600`, `#00FF00`
  - 3: `2400`, `#0000FF`
  - 4: `3200`, `#FF00FF`
  - 5: `6500`, `#FFFF00`
  - 6: `10000`, `#00FFFF`
- `report_rate default_value="1" report_max="4"`
- `light_info default_light="2"`
  - `light_1` name `流光`, enable `0`
  - `light_2` name `呼吸`, enable `1`
  - `light_3` name `常亮`, enable `1`
  - `light_4` name `霓虹`, enable `0`
  - `light_5` name `七彩波浪`, enable `0`
  - `light_6` name `关闭`, enable `1`
- `sleep_light value="90"`
- `move_wakeup value="0"`
- `move_closelight value="0"`
- `lod value="1"`
- `button_respondtime value="8"`

Important interpretation:

- `light_info` and `move_closelight` are distinct XML fields. Do not collapse
  them into one HID byte without capture evidence.
- `light_6=关闭` is likely an effect-mode value or DPI light close mode, while
  `move_closelight` is a motion-related policy toggle.
- `sleep_light=90` matches the known sleep timeout command path better than a
  pure LED brightness/value field.
- `<menu light="0" />` means the app may hide or disable a top-level Lighting
  page, even though DPI light controls still exist.

`config.xml` says the same hardware is supported in USB and 2.4G mode:

- USB: `vid=248A`, `pid=5D2E`, `dev_id=S057`, `MI_02`
- 2.4G: `vid=249A`, `pid=5C2F`, `dev_id=S057`, `MI_02`
- software version: `1.0.0.2`
- firmware upgrade URL:
  `https://firmware-upgrade-1325974173.cos.ap-guangzhou.myqcloud.com/SC360SE/`
- software update URL:
  `https://software-upgrade-1325974173.cos.ap-guangzhou.myqcloud.com/SC360SE-update/`

Language files are UTF-16LE. Direct `sed` produces garbage; use `iconv` first.

Relevant English/Chinese UI strings:

- `75=Lighting` / `灯光`
- `150=RGB Effect Control` / `灯光控制`
- `151=Light Effect Switch` / `灯效切换`
- `265=DPI Light Settings` / `DPI灯光设置`
- `266=Lighting Effect` / `灯光效果`
- `268=Brightness Adjustment` / `亮度调节`
- `269=Speed Adjustment` / `速度调节`
- `280..285=Wave, Breathing, Static, Neon, Wave multicolor, Close`
- `501=LOD` / `鼠标LOD高度`
- `505..517=Ripple Control, Angle Snap, Motion Sync` /
  `纹波控制, 直线修正, Motion Sync开启`
- `525=Debounce Time` / `按键延迟响应(毫秒)`
- `526=High-Speed Mode` / `高速模式`
- `531=Sleep Settings` / `休眠设置`
- `532=Wake up mode` / `唤醒方式`
- `533=Move to wake up` / `移动唤醒`
- `534=Move to turn off the light effect` / `移动关闭DPI灯效`

The language strings confirm at least three separate UI concepts:

1. DPI light mode/effect.
2. Light effect switch / close.
3. Motion-related close-light policy.

They also show that the Windows driver exposes more sensor/power features than
the current Linux driver models directly:

1. `LOD` / `liftoff_height`
2. `Ripple Control`, `Angle Snap`, `Motion Sync`
3. `Debounce Time`
4. `High-Speed Mode`
5. Windows pointer-side settings like sensitivity, wheel lines, and
   double-click time

## Original Driver Default Config Database

`AULA SC360SE.exe` contains a large initialization function at `0x401900` that
creates and populates a profile/config SQLite database. This is stronger
evidence than bare XML defaults because it shows values the original driver
actually writes into its own config store.

High-confidence config keys observed in the EXE:

- `lightmode`
- `sleep_light`
- `move_wakeup`
- `move_closelight`
- `report_rate`
- `report_rate_wireless`
- `report_count`
- `report_flag`
- `button_respondtime`
- `sensor_flag`
- `liftoff_height`
- `scroll_flag`
- `e-sports_flag`
- `leftbtn_lock`
- `dpi_flag`
- `dpi_index`
- `dpi_count`
- `dpi_group0_value .. dpi_group7_value`
- `dpi_group0_rgb .. dpi_group8_rgb`
- `mouse_speed`
- `mouse_sensitivity`
- `wheel_scrollflag`
- `wheel_scrolllines`
- `doubleclick_time`

Defaults written by the EXE init function:

- `lightmode = 3`
- `sleep_light = 30`
- `move_wakeup = 1`
- `move_closelight = 1`
- `report_rate = 2`
- `report_rate_wireless = 1`
- `report_count = 4`
- `report_flag = 127`
- `button_respondtime = 8`
- `sensor_flag = 53`
- `liftoff_height = 1`
- `scroll_flag = 1`
- `e-sports_flag = 0`
- `leftbtn_lock = 1`
- `dpi_flag = 6`
- `dpi_index = 1`
- `dpi_count = 6`
- `mouse_speed = 10`
- `mouse_sensitivity = 1`
- `wheel_scrollflag = 0`
- `wheel_scrolllines = 3`
- `doubleclick_time = 550`

EXE DPI value defaults:

- `dpi_group0_value = 800`
- `dpi_group1_value = 1600`
- `dpi_group2_value = 2400`
- `dpi_group3_value = 3200`
- `dpi_group4_value = 6400`
- `dpi_group5_value = 26000`
- `dpi_group6_value = 26000`
- `dpi_group7_value = 0`

EXE DPI RGB defaults:

- `dpi_group0_rgb = 255` (`0x0000ff`)
- `dpi_group1_rgb = 16711680` (`0x00ff0000`)
- `dpi_group2_rgb = 65280` (`0x0000ff00`)
- `dpi_group3_rgb = 65535` (`0x0000ffff`)
- `dpi_group4_rgb = 16711935` (`0x00ff00ff`)
- `dpi_group5_rgb = 16776960` (`0x00ffff00`)
- `dpi_group6_rgb = 16712191` (`0x00ff01ff`)
- `dpi_group7_rgb = 16777215` (`0x00ffffff`)
- `dpi_group8_rgb = 2130943` (`0x0020a3ff`)

Color caution:

- These are raw integer values from the Windows driver's config DB init path.
- Do not assume they are stored as plain `#RRGGBB`; the ordering may follow
  Windows `COLORREF`-style packing or another internal convention.
- They are useful as exact driver-side constants even before the channel order
  is fully proven.

Important cautions about these defaults:

- The EXE database defaults do not fully match `mouse_sc360se.xml`.
- XML says `sleep_light=90`, `move_wakeup=0`, `move_closelight=0`, and DPI
  values `800/1600/2400/3200/6500/10000`.
- The EXE init path instead writes `sleep_light=30`, enables both motion
  toggles, and uses a wider internal DPI/color table with extra slots.
- Therefore, do not assume XML values are the only or final truth. The original
  driver appears to merge XML metadata with a separate internal default-config
  model.

Likely interpretations:

- `dpi_flag=6` and `dpi_count=6` agree with the six-stage public DPI UI.
- `dpi_group7_value=0` and `dpi_group8_rgb` suggest the internal table is wider
  than the visible six-stage UI and may reuse a generic engine shared with
  other models.
- `sensor_flag=53` is likely a packed advanced-sensor bitfield, not a plain
  scalar option.
- `report_rate=2`, `report_rate_wireless=1`, `report_count=4`, and
  `report_flag=127` likely participate in a richer report-rate/transport model
  than the current Linux driver's single `0x02/0x01 [4]` byte abstraction.

## Original Driver Light Data Model

The EXE also contains a separate `t_light_data` table, which is broader than
the currently known SC360SE Linux light-mode surface:

- `profile`
- `name`
- `mode`
- `brightness`
- `speed`
- `direction`
- `colorful`
- `colorindex`
- `color_value1 .. color_value7`
- `config_func`
- `reserved`
- `status`

This means the vendor software was designed around a generic light engine with
brightness/speed/direction/custom-color fields even if this specific mouse only
exposes part of that surface over the proven HID commands.

## Firmware / Platform Hints From EXE Strings

Useful non-XML strings found in `AULA SC360SE.exe`:

- `sensor is P3395--------`
- `Read_p3395_0x6c:%x--`
- `init_p3395_ok--`
- `cmd get Sensor advanced par`
- `set sensor parameters -`
- `cmd get power_management`
- `set power management -`
- `USB Gaming Mouse`
- `tlsr8278`

Likely interpretation:

- `P3395` is a strong static hint that the sensor path is built around PixArt
  PMW3395-class logic.
- `Read_p3395_0x6c` suggests at least one advanced-sensor parameter path is
  implemented in the original stack beyond the Linux driver's current public
  surface.
- `power_management` lines line up with the XML sleep/move-wakeup fields and
  make it more plausible that these settings are grouped under a dedicated
  command family.
- `tlsr8278` is likely a radio/MCU/platform hint from shared vendor code or
  firmware assets rather than a directly user-visible parameter.

Cross-device/shared-code caution:

- Strings such as `Legion M6XPro BT` and the wider `t_light_data` schema show
  that `MouseHub` is not SC360SE-only code.
- When a value appears only in the EXE and not in captures/XML, consider the
  possibility that it belongs to a shared generic engine rather than a proven
  SC360SE runtime command.

## Current HID Protocol Evidence

Known or currently modeled frames:

- `0x02/0x01`: polling rate, `[4] = 1/2/4/8` for `1000/500/250/125 Hz`.
- `0x03/0x25`: DPI config and active stage,
  `[4] = (active << 4) | count`, `[5..28] = 6 * (x,y)` as cpi/100 LE u16.
- `0x04/0x12`: DPI per-stage RGB table,
  `[4..21] = 6 * RGB`, `[22..27] = six flag bytes`.
- `0x06/0x05`: sensor-advanced write group, not a proven light-mode command.
  Static EXE reversing of `fcn.00435750` shows the setter reads
  `liftoff_height` and `sensor_flag`, then builds the same command family that
  the driver queries with `0x16`. `[4] = liftoff_height`, `[5] = 1 when
  `sensor_flag & 0x08`, and `[6] = 1 when `sensor_flag & 0x04`. `[7]` remained
  zero in observed Windows writes.
- `0x07/0x04`: power-management group. Windows sleep-setting capture
  `休眠设置.pcapng` confirms `[4] = second-stage sleep timeout in 10-second
  units`, `[5] = move_wakeup`, `[6] = 0x00 in the tested sleep/wakeup UI path`,
  and `[7] = button_respondtime`. Existing Linux code that parses `[4..5]` as
  a little-endian seconds value is wrong. Write/read examples:
  - 1 min:  `07 00 01 04 06 01 00 08 ...`
  - 2 min:  `07 00 01 04 0c 01 00 08 ...`
  - 5 min:  `07 00 01 04 1e 01 00 08 ...`
  - 10 min: `07 00 01 04 3c 01 00 08 ...`
  - 15 min: `07 00 01 04 5a 01 00 08 ...`
  - 20 min: `07 00 01 04 78 01 00 08 ...`
  - 30 min: `07 00 01 04 b4 01 00 08 ...`
  - move wake off/on toggles only `[5]`: `b4 00 00 08` / `b4 01 00 08`.
- `0x0f/0x01`: factory reset, `[4] = ff`; also repairs hidden DPI-key state.
- `0x10`: device info/battery query reply has `S057`, version fields,
  `[13] = battery percent`, `[14] = online/ready`.
- `0x11..0x17`: readback family already used by Linux code.
- `0x15`: profile/light-data status selector. Known reply is
  `15 00 01 02 04 42 ...`; EXE stores `[4]` at object offset `+0x770`, uses it
  as a profile/light-data selector, and splits another byte into high/low
  nibbles before updating `t_light_data` status. The exact visible meaning is
  still not fully assigned.
- `0x16`: sensor-advanced group despite earlier Linux light-only naming. EXE
  maps `[4]` to `liftoff_height` and builds `sensor_flag` from `[5]` and `[6]`
  booleans: if `[6] == 1`, add `0x04`; if `[5] == 1`, add `0x08`.
- `0x17`: power-management readback. Known reply
  `17 00 01 05 5a 00 00 08 ...` maps to second-stage sleep timeout
  `0x5a * 10 = 900 seconds = 15 minutes`, `move_wakeup=0`,
  `[6]=0`, and `button_respondtime=8`.
- `0x20`: firmware/RF-version readback. Known reply
  `20 00 01 02 21 03 ...`; EXE formats `[4]` and `[5]` using `%X%02X`, so
  this displays as version `21.03`, not a user-tunable parameter.
- `0xc0`: unsolicited link/battery notify, `[1] = online flag`,
  `[2] = battery percent`.
- `0xc2`: unsolicited physical DPI-key runtime notification,
  `[1] = (active << 4) | count`, `[2..3] = cpi/100 LE`.

Important refuted or weak interpretations:

- `0x06/0x05` is not a generic commit/save command, and the newest static
  evidence no longer supports naming `[4]` as a light mode.
- Values previously called mode `1` and mode `2` are better explained as
  `liftoff_height` values. Captured Windows frames
  `06 00 01 05 01 00 01 00 ...` and `06 00 01 05 02 00 00 00 ...` map cleanly
  to `(liftoff_height=1, sensor_flag=0x04)` and
  `(liftoff_height=2, sensor_flag=0x00)`.
- The historical `payload[4]=0x02` frame can be ACKed and may appear to read
  back, but later live tests refuted treating it as a reliable visible
  static-light restore.
- Rewriting `0x04/0x12` color table alone did not restore constant LED behavior
  after host-side DPI writes.
- Sleep/wake and disconnect/reconnect captures showed firmware-internal
  recovery: around recovery, captures show `c0 00 5a...` / `c0 01 5a...`, not
  host recovery writes through `0x06`, `0x03`, `0x04`, or `0x16`.

## Likely Lighting-Related Fields To Investigate

High-confidence existing fields:

- `0x04/0x12` RGB table and six flag bytes. The flag bytes are lighting-related
  because they are returned with RGB, but their exact visible meaning is not
  fully proven.
- `0x16` readback and `0x06/0x05` write form the sensor-advanced pair:
  `liftoff_height` plus two boolean bits from `sensor_flag`.

Refuted lighting candidates:

- `0x06/0x05 [4]` should no longer be treated as XML light mode 1..6 without
  new capture evidence. The EXE's setter path uses `liftoff_height`, and the
  earlier profile captures fit the sensor interpretation.

Medium-confidence candidate fields:

- `0x04/0x12 [22..27]`: per-stage RGB flags. Need systematic visible tests:
  one stage at a time, flags `00/01/ff`, observe whether color is stored,
  disabled, static-only, or runtime-only.

Lower-confidence candidate fields:

- `move_wakeup`, `[6]`, and `button_respondtime` are no longer
  separate unknown-opcode candidates for basic read/write mapping. They belong
  to the `0x07/0x04` power-management frame. `休眠设置.pcapng` proves `[5]` is
  the second-stage sleep "move wakeup" toggle: when enabled, movement or click
  wakes the mouse; when disabled, only click wakes it. `[6]` stayed `00` in
  the sleep/wakeup UI capture, so do not expose or name it as a proven
  `move_closelight` control without a dedicated capture.
- `light_6=Close` might be only a DPI effect mode, not global LED power off.
  Compare it with `move_closelight` toggles if a Windows capture can be made.

Sleep model from manual plus live tests:

- The mouse has two sleep stages.
- Stage 1 is fixed at about 1 minute: the DPI light turns off, the 2.4G
  receiver can still answer `0x10`, but byte `[14]` becomes `00` and full
  config readback fails until the mouse is moved or clicked. This fixed
  first-stage timeout is not the configurable Windows "sleep setting".
- Stage 2 is the Windows-configurable sleep setting written through
  `0x07/0x04 [4]` in 10-second units. The UI values 1/2/5/10/15/20/30 minutes
  map to raw bytes `06/0c/1e/3c/5a/78/b4`.
- Stage 2 wake policy is `0x07/0x04 [5]`: `1` allows movement or click to wake;
  `0` requires click wake. Do not confuse this with the fixed stage-1
  movement wake behavior.

Power-management write construction from `AULA SC360SE.exe`:

- `fcn.004358c0` is the Windows driver write path for `0x07/0x04`.
- It builds a frame equivalent to:
  `07 00 01 04 <sleep_light> <move_wakeup> <move_closelight> <button_respondtime> ...`.
- The function reads all four bytes from the profile/config database using
  string keys and converts them with `_wtoi` before copying the low byte into
  the outgoing frame:
  - `sleep_light` -> frame `[4]`
  - `move_wakeup` -> frame `[5]`
  - `move_closelight` (`0x5606a4` UTF-16 string) -> frame `[6]`
  - `button_respondtime` (`0x5606e4` UTF-16 string) -> frame `[7]`
- Therefore `[6]` and `[7]` are not random padding. They are official
  driver-side config fields even though the SC360SE UI may hide one or both.
- `button_respondtime` is written to the device by Windows, but live tests with
  larger raw values did not produce obvious click latency. Treat its visible
  effect as unproven; it may be clamped, only affect debounce edge cases, or be
  accepted but unused on this model.
- Hidden UI range logic exists for `button_respondtime`: `fcn.0041ffe0` handles
  event/field id `0x1d`, reads a `NumberEdit`, clamps the value to `4..20`,
  then saves it through the `button_respondtime` config key. This matches the
  language string "Debounce Time" / `按键延迟响应(毫秒)`, but
  `mouse_sc360se.xml` has `show_respondtime="0"`, so the SC360SE Windows UI
  hides the control. Use `4..20` as the official UI-supported range if exposing
  this field in Linux, with default `8`.
- `move_closelight` is presented as a policy toggle in language/XML
  (`Move to turn off the light effect` / `移动关闭DPI灯效`) and has a default
  `0`. Treat the UI-supported range as boolean `0/1` unless a dedicated capture
  proves additional values.

## Capture Inventory Already Checked

`capinfos -c -d -a -e -M *.pcapng` was run. Root captures available:

- `1分钟睡眠点击恢复.pcapng`: 32156 packets, 2026-05-12 11:25:57..11:27:52
- `DPI配置.pcapng`: 38368 packets, 2026-05-11 20:04:42..20:05:55
- `将配置1切换到配置2、3、4.pcapng`: 34260 packets, 2026-05-07 22:39:56..22:41:23
- `插入接收器+识别.pcapng`: 15296 packets, 2026-05-12 12:11:23..12:11:59
- `检测连接鼠标.pcapng`: 14186 packets, 2026-05-12 10:25:30..10:25:57
- `连接+切换DPI.pcapng`: 35596 packets, 2026-05-11 20:45:17..20:46:23
- `配置切换.pcapng`: 18386 packets, 2026-05-07 23:16:00..23:16:55
- `驱动断联重连.pcapng`: 38392 packets, 2026-05-12 10:26:25..10:29:09
- `休眠设置.pcapng`: 52540 packets, 2026-05-12 20:32:55..20:34:38.
  Operation order was sleep timeout 1/2/5/10/15/20/30 minutes, then move wake
  off and move wake on. It proves the `0x07/0x04` second-stage sleep mapping:
  `[4] = minutes * 6`, `[5] = move_wakeup`, `[6] = 00`, `[7] = 08`.

Do not re-run broad `capinfos` unless file set changed.

Early `tshark` reads without filtering mostly showed endpoint `0x81` normal
mouse input reports. For HID config traffic, filter for the config endpoints
used by previous scripts, especially host `0x05` and device `0x84`, or use the
existing PDML/text parsing helpers.

## Static DLL / EXE Findings

### `mfc140u.dll`

Conclusion: likely Microsoft MFC runtime, not a SC360SE protocol component.

Evidence:

- PE32 x86 DLL, signed.
- PDB path from `rabin2 -I`:
  `d:\a01\_work\38\s\\binaries\x86ret\bin\i386\\mfc140u.i386.pdb`
- Strings are generic MFC/ATL items such as `Afx...`, `MFCButton`,
  `MFCColorButton`, `MouseManager`, `LIGHT`, `Light`, etc. These are framework
  strings and not enough to infer SC360SE protocol logic.

Do not spend more time reversing `mfc140u.dll` unless there is a specific
reason to inspect Microsoft runtime internals.

### `MUI.dll`

Conclusion: real UI framework/component library, but probably not the primary
device protocol implementation.

Evidence:

- PE32 x86 DLL, built from the vendor project.
- `rabin2 -I` PDB path:
  `E:\驱动资料0407\驱动程序\XiChenMouseDriver - 20251201(AULA  SC360SE)\bin\MUI.pdb`
- Large export table, about 2270 symbols, mostly C++ `DUI::` UI controls.
- Important exported classes/symbols include:
  - `DUI::LightSlider`
  - `DUI::DPISlider`
  - `DUI::DPIControl`
  - `DUI::MouseCtrl`
  - `DUI::MouseKeyCtrl`
  - `DUI::ReporteCtrl`
  - `DUI::BatteryCtrl`
  - `DUI::CColorPalette`, `DUI::CColorSelect`, `DUI::ColorButtonEx`
- Imports are mostly Win32 UI/resource/GDI/GDI+/OLE/common-library functions.
- No `SETUPAPI.dll` import was seen in `MUI.dll`, which makes it less likely to
  own HID enumeration.
- It does import `CreateFileW` / `WriteFile`, but the surrounding imports are
  also resource/font/file helpers, so this alone is not proof of HID protocol
  ownership.
- UTF-16 strings found by `strings -a -el` were mostly class names and fonts,
  not XML field names or protocol command names.

Possible use of `MUI.dll` later:

- It can help recover UI event/control names, especially sliders and combo boxes
  for DPI/light pages.
- It is not the best first target for HID opcode discovery.

### `AULA SC360SE.exe`

Conclusion: this is now the strongest static target for device protocol logic.

Evidence:

- PE32 x86 GUI executable, PDB path:
  `E:\驱动资料0407\驱动程序\XiChenMouseDriver - 20251202(AULA SC360SE)\bin\MouseHub.pdb`
- Imports `SETUPAPI.dll` functions:
  - `SetupDiGetDeviceRegistryPropertyA`
  - `SetupDiEnumDeviceInfo`
  - `SetupDiGetDeviceInterfaceDetailA`
  - `SetupDiGetClassDevsA`
  - `SetupDiEnumDeviceInterfaces`
  - `SetupDiDestroyDeviceInfoList`
- Imports HID-relevant generic I/O:
  - `CreateFileW`, `CreateFileA`
  - `ReadFile`, `WriteFile`
  - `CancelIo`, `GetOverlappedResult`, `CreateEventW`
- Imports device/input notification APIs:
  - `RegisterRawInputDevices`
  - `GetRawInputData`
  - `RegisterDeviceNotificationW`

Additional static mapping from `fcn.00432f60` / `fcn.00431d50`:

- Both functions build bare query frames by zeroing a 0x41-byte buffer, writing
  words such as `0x1500`, `0x1600`, `0x1700`, and calling the HID wrapper with
  length `0x21`. This matches the existing Linux query style.
- `0x15` reply handling stores `[4]` into object offset `+0x770`, then uses it
  to select/update rows in `t_light_data`; another byte is split into high and
  low nibbles for active mode/status handling. Treat it as profile/light-data
  status until more profile-switch captures vary the value.
- `0x16` reply handling writes `[4]` to `liftoff_height`; it derives
  `sensor_flag` as `(reply[6] == 1 ? 0x04 : 0) | (reply[5] == 1 ? 0x08 : 0)`.
  This means the current Linux name "light readback" is semantically wrong for
  the original driver, even though Linux currently reads only `[4]` as
  `light_mode`.
- `0x17` reply handling writes `[4]` to `sleep_light`, `[5]` to `move_wakeup`,
  `[6]` to `move_closelight`, and `[7]` to `button_respondtime`. The string
  addresses `0x5606a4` and `0x5606e4` resolve to `move_closelight` and
  `button_respondtime`.
- `0x20` is only queried after a connection-mode check and is formatted for
  display with `%X%02X` plus language IDs `551` / `580`. Language files map
  `551` to "Mouse Firmware" / `鼠标版本`, and `580` to an update-status string.
  Do not model `0x20` as a tunable setting.

Additional static mapping from `fcn.004340e0`:

- The function builds the button macro table in memory and then sends a
  `0x09/0x0f` frame. The frame prefix is assembled from constants equivalent
  to `09 00 01 0f`, then up to six 3-byte slots are copied before HID send.
- The write path confirms that the original driver uses the same broad command
  families already observed in captures rather than a hidden commit opcode for
  ordinary profile application.

Additional static mapping from `fcn.00435750`:

- This is the setter paired with the `0x16` sensor-advanced readback. It builds
  a `0x06/0x05` frame, reads `liftoff_height`, and writes it to payload byte
  `[4]`.
- It reads `sensor_flag` and expands bits into two payload booleans:
  `sensor_flag & 0x08` becomes `[5] = 1` and `sensor_flag & 0x04` becomes
  `[6] = 1`. The generic UI names these bits as sensor performance toggles:
  `0x04 = Ripple`, `0x08 = Angle Snap`, and `0x10 = Motion Sync`.
  `fcn.00420340` saves all three bits from checkboxes at offsets
  `+0x46c/+0x470/+0x474`, but `fcn.00435750` and the paired `0x16` readback
  parser only write/read the `0x04` and `0x08` bits through this HID path.
  Treat `0x10` as a UI/config bit whose device-side command path is not yet
  proven.
- Payload byte `[7]` is not assigned by the Windows `0x06/0x05` setter and is
  ignored by the Windows `0x16` parser. Captures show Windows writing `[7]=0`
  in `0x06/0x05` frames while the device may read back `[7]=1` in `0x16`
  replies. Treat it as reserved/device-status or firmware-default until a live
  write/read matrix proves it is writable and produces a behavior change.
- This makes the observed Windows profile frames line up as sensor settings:
  `06 00 01 05 01 00 01 00 ...` means `liftoff_height=1, sensor_flag=0x04`;
  `06 00 01 05 02 00 00 00 ...` means `liftoff_height=2, sensor_flag=0x00`.
  Do not use these two frames as evidence for XML light mode 1/2.
- The decompiled generic UI exposes `liftoff_height` as only two same-group
  radio buttons, labeled `1.0mm` and `2.0mm` (`IDC_PER_STC_LOD`). The save path
  writes string `"1"` or `"2"` to the `liftoff_height` config key, and the load
  path compares only against `"2"` before selecting the second radio; all other
  values fall back to the first radio. For SC360SE specifically,
  `mouse_sc360se.xml` has `show_lod="0"` and default `<lod value="1"/>`, so the
  official UI range is hidden for this model, but the shared UI-supported LOD
  range is still only `1..2`. Treat protocol-accepted values such as `3` as
  non-UI/unsupported test values unless new evidence appears.

Next static reversing should focus on `AULA SC360SE.exe`, not `mfc140u.dll`, and
only use `MUI.dll` as UI context.

## Next Reverse-Engineering Steps

1. Update the Linux model to stop treating `0x06/0x05` and `0x16` as a
   light-mode surface. Preserve the existing user-facing command only as an
   explicitly experimental raw write, or replace it with a sensor-advanced
   command once live testing confirms visible/behavioral effects.
2. For `0x06/0x05`, build a table of all captured writes and `0x16` replies,
   including bytes `[4..7]`, checksum, timing, and whether the frame correlates
   with LOD/sensor UI actions rather than lighting.
3. Update the Linux power-management model: `0x07/0x04 [4]` is second-stage
   sleep timeout in 10-second units, `[5]` is second-stage move wakeup, `[7]`
   is button debounce/response time, and `[6]` remains unassigned in current
   sleep captures. Preserve a note that the manual describes a fixed first
   sleep stage at about 1 minute which turns off the light and makes config
   readback fail until movement or click wakes the mouse.
4. Capture Windows changes for `sensor_flag`, `button_respondtime`, and `LOD`
   one at a time, then confirm whether writes use `0x06/0x05`, `0x07/0x04`,
   and `0x16`/`0x17` readbacks exactly as the static parser implies.
5. Continue reversing `AULA SC360SE.exe` around any remaining true light setter.
   The current `0x06/0x05` target has resolved to sensor advanced, so the next
   light search should pivot to `lightmode`, `t_light_data`, XML `light_info`,
   and UI callbacks rather than this opcode.
6. Use `MUI.dll` exports only to connect UI controls to event callbacks if the
   EXE call graph references imported `DUI::LightSlider`, `DUI::DPIControl`, or
   combo/list control APIs.

## Current `todo` Relation

Root `todo` still has open items:

- Find why modifying DPI can make constant LED fail.
- Rewrite lower-level light-effect code after the LED model is clearer.
- Better battery display.

The notes above mainly support the first two items.
