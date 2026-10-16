#!/bin/bash
# Startet das fertige Gesamt-Image zweimal im ESP32-Emulator (Espressif QEMU) und prüft,
# dass die Standard-Einrichtung beim ersten Start übernommen wird und nach einem Neustart erhalten bleibt.
# WLAN gibt es im Emulator nicht – geprüft wird alles, was vor dem WLAN-Start passiert.
#
# Aufruf: scripts/qemu_boot_test.sh <gesamt-image.bin>
set -u
IMG="$1"
QV=esp-develop-9.2.2-20250817
QF=qemu-xtensa-softmmu-esp_develop_9.2.2_20250817-x86_64-linux-gnu.tar.xz
W=$(mktemp -d)
ann() { if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::$1 title=Boot-Test::$2"; else echo "[$1] $2"; fi; }

if ! curl -fsSL -o "$W/q.tar.xz" "https://github.com/espressif/qemu/releases/download/$QV/$QF" || ! tar xJf "$W/q.tar.xz" -C "$W"; then
  ann warning "Emulator nicht verfügbar – Boot-Test übersprungen"; exit 0
fi
command -v apt-get >/dev/null && ! ldconfig -p | grep -q libslirp && sudo apt-get install -y -qq libslirp0 >/dev/null 2>&1
Q="$W/qemu/bin/qemu-system-xtensa"
if ! "$Q" --version >/dev/null 2>&1; then ann warning "Emulator startet nicht – Boot-Test übersprungen"; exit 0; fi

# Flash auf 4 MB auffüllen (wie ein gelöschter Chip)
python3 - "$IMG" "$W/flash.bin" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read()
open(sys.argv[2], "wb").write(d + b"\xff" * (4 * 1024 * 1024 - len(d)))
EOF

boot() {
  timeout 40 "$Q" -nographic -machine esp32 -drive file="$W/flash.bin",if=mtd,format=raw \
    -global driver=timer.esp32.timg,property=wdt_disable,value=true \
    -nic user,model=open_eth -serial file:"$W/$1.log" -monitor none -no-reboot >/dev/null 2>&1
  grep -a -E "^\[(FS|Setup)\]|^=== " "$W/$1.log" | tr -d '\r'
}

B1=$(boot boot1); B2=$(boot boot2)
echo "--- 1. Start ---"; echo "$B1"; echo "--- 2. Start ---"; echo "$B2"
S1=$(echo "$B1" | grep -a "Standard-Einrichtung:" | sed 's/^\[Setup\] //')
S2=$(echo "$B2" | grep -a "Standard-Einrichtung:" | sed 's/^\[Setup\] //')

if [ -z "$S1" ]; then
  ann error "Firmware startet im Emulator nicht bis zur Einrichtung. Ausgabe: $(head -c 400 "$W/boot1.log" | tr -d '\r' | tr '\n' ' ')"; exit 1
fi
ann notice "1. Start: $S1"
ann notice "2. Start: $S2"
if [ -f setup/default-setup.json ]; then
  echo "$S1" | grep -q "neu übernommen" || { ann error "Standard-Einrichtung wurde beim ersten Start nicht übernommen"; exit 1; }
  echo "$S2" | grep -q "aktiv" || { ann error "Standard-Einrichtung ist nach dem Neustart nicht mehr aktiv"; exit 1; }
fi
exit 0
