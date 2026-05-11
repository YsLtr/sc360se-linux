#!/usr/bin/env bash
# capture.sh — sniff USB feature reports between AULA SC360SE and the
# original Windows app under Wine, so the exact wire format can be filled
# into src/protocol.c (replacing TODO_PAYLOAD regions).
#
# Requires: usbmon kernel module, tshark (wireshark-cli).

set -euo pipefail

VID_W=248a; PID_W=5d2e
VID_R=249a; PID_R=5c2f

require() { command -v "$1" >/dev/null || { echo "missing: $1"; exit 1; }; }
require lsusb
require tshark

sudo modprobe usbmon

bus=$(lsusb | awk -v v="$VID_W" -v p="$PID_W" -v v2="$VID_R" -v p2="$PID_R" \
  '($6==v":"p)||($6==v2":"p2){gsub(":","",$2); print $2; exit}')
if [[ -z "$bus" ]]; then
  echo "AULA SC360SE not found in lsusb"
  exit 1
fi

iface="usbmon${bus#0}"
out="${1:-sc360se-$(date +%Y%m%d-%H%M%S).pcap}"

echo "Capturing on $iface → $out"
echo "Now run the AULA Windows app under Wine and click the buttons you"
echo "want to learn the protocol for. Ctrl-C when done."
sudo tshark -i "$iface" -w "$out" -f 'usb.transfer_type==2'

echo
echo "Decode with:"
echo "  tshark -r $out -T fields -e frame.time_relative \\"
echo "         -e usb.bmRequestType -e usb.setup.bRequest \\"
echo "         -e usb.setup.wValue   -e usb.capdata"
