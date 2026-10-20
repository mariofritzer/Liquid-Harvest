#!/bin/bash
# Prüft die Update-Absicherung im ESP32-Emulator:
#  app0 = normale Firmware, app1 = absichtlich abstürzende Firmware (-DLH_CRASH_TEST), otadata zeigt auf app1 ("neu").
#  Erwartet: app1 startet "auf Probe", stürzt ab -> Bootloader holt app0 zurück -> app0 meldet den Rücksprung.
#
# Aufruf: scripts/qemu_rollback_test.sh <gesamt-image.bin> <crash-app.bin>
set -u
IMG="$1"; CRASH="$2"
QV=esp-develop-9.2.2-20250817
QF=qemu-xtensa-softmmu-esp_develop_9.2.2_20250817-x86_64-linux-gnu.tar.xz
W=$(mktemp -d)
ann() { if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::$1 title=Rollback-Test::$2"; else echo "[$1] $2"; fi; }

if ! curl -fsSL -o "$W/q.tar.xz" "https://github.com/espressif/qemu/releases/download/$QV/$QF" || ! tar xJf "$W/q.tar.xz" -C "$W"; then
  ann warning "Emulator nicht verfügbar – Test übersprungen"; exit 0
fi
Q="$W/qemu/bin/qemu-system-xtensa"
if ! "$Q" --version >/dev/null 2>&1 && command -v apt-get >/dev/null; then
  sudo apt-get update -qq >/dev/null 2>&1; sudo apt-get install -y -qq libslirp0 libsdl2-2.0-0 libpixman-1-0 >/dev/null 2>&1
fi
"$Q" --version >/dev/null 2>&1 || { ann warning "Emulator startet nicht – Test übersprungen"; exit 0; }

python3 - "$IMG" "$CRASH" "$W/flash.bin" <<'EOF'
import struct, sys, zlib
img = bytearray(open(sys.argv[1], "rb").read().ljust(4 * 1024 * 1024, b"\xff"))
app = open(sys.argv[2], "rb").read()
img[0x190000:0x190000 + 0x180000] = app.ljust(0x180000, b"\xff")          # app1 = abstürzende Firmware
seq = 2                                                                     # (seq-1) % 2 = 1 -> app1
e = struct.pack("<I", seq) + b"\xff" * 20 + struct.pack("<I", 0) + struct.pack("<I", zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF))
img[0xE000:0x10000] = e.ljust(0x1000, b"\xff") + b"\xff" * 0x1000           # otadata: app1, Zustand "neu"
open(sys.argv[3], "wb").write(img)
EOF

timeout 45 "$Q" -nographic -machine esp32 -drive file="$W/flash.bin",if=mtd,format=raw \
  -global driver=timer.esp32.timg,property=wdt_disable,value=true \
  -nic user,model=open_eth -serial file:"$W/rb.log" -monitor none >/dev/null 2>&1
L=$(grep -a -E "^\[(Update|Test)\]" "$W/rb.log" | tr -d '\r' | head -6)
echo "$L"
if echo "$L" | grep -q "app1, Zustand auf Probe" && echo "$L" | grep -q "app0.*nicht sauber gestartet"; then
  ann notice "Absturz einer neuen Firmware -> automatisch zurück auf die vorherige (app1 → app0) und Hinweis angezeigt"
  exit 0
fi
ann error "Rücksprung nach Absturz hat nicht funktioniert. Ausgabe: $(echo "$L" | tr '\n' ' ' | head -c 400)"
exit 1
