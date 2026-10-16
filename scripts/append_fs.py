# Hängt das Dateisystem-Image (Standard-Einrichtung) an seine Partitionsadresse ans Gesamt-Image an.
# Aufruf nach "pio run -t buildfs" und "pio run": python scripts/append_fs.py [littlefs-image.bin]
import csv, os, sys
b = ".pio/build/esp32"
merged = os.path.join(b, "firmware-merged.bin")
fs = sys.argv[1] if len(sys.argv) > 1 else os.path.join(b, "littlefs.bin")
if not os.path.exists(fs):
    sys.exit("littlefs.bin fehlt")
start = None
for row in csv.reader(l for l in open("partitions.csv") if l.strip() and not l.startswith("#")):
    row = [c.strip() for c in row]
    if row[0] == "spiffs":
        start = int(row[3], 16)
img = open(merged, "rb").read()
if start is None or len(img) > start:
    sys.exit("Partitionsadresse ungültig oder Firmware überlappt das Dateisystem")
data = open(fs, "rb").read()
open(merged, "wb").write(img + b"\xff" * (start - len(img)) + data)
print(f"Dateisystem ({len(data)//1024} kB) bei {hex(start)} angehängt, Gesamt {(start+len(data))//1024} kB")
