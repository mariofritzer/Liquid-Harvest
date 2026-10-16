# Prüft das fertige Gesamt-Image für den Web-Installer:
#  - liegt das Dateisystem an der richtigen Adresse?
#  - enthält es die Standard-Einrichtung (setup.json, Bild/Video) aus setup/default-setup.json?
# Ergebnis wird als GitHub-Annotation ausgegeben, bei Fehlern bricht der Build ab.
#
# Aufruf: python scripts/verify_image.py <gesamt-image.bin>
import csv, json, os, sys

def note(kind, msg):
    print(f"::{kind} title=Image-Prüfung::{msg}" if os.environ.get("GITHUB_ACTIONS") else f"[{kind}] {msg}")

def main(img_path):
    start = size = None
    for row in csv.reader(l for l in open("partitions.csv") if l.strip() and not l.startswith("#")):
        row = [c.strip() for c in row]
        if row[0] == "spiffs":
            start, size = int(row[3], 16), int(row[4], 16)
    img = open(img_path, "rb").read()
    want = os.path.exists("setup/default-setup.json")
    if len(img) < start + 4096:
        if want:
            note("error", f"Im Image fehlt das Dateisystem (Image nur {len(img)//1024} kB, Dateisystem gehört an {hex(start)}).")
            return 1
        note("notice", "Keine Standard-Einrichtung im Repo – Image ohne Dateisystem.")
        return 0

    from littlefs import LittleFS
    region = bytearray(img[start:start + size].ljust(size, b"\xff"))
    fs = LittleFS(block_size=4096, block_count=size // 4096, read_size=256, prog_size=256, mount=False)
    fs.context.buffer = region
    try:
        fs.mount()
    except Exception as e:
        note("error", f"Dateisystem im Image lässt sich nicht einhängen: {e}")
        return 1
    files = {n: fs.stat("/" + n).size for n in fs.listdir("/")}
    listing = ", ".join(f"{n} ({s // 1024} kB)" if s >= 1024 else f"{n} ({s} B)" for n, s in sorted(files.items()))
    if not want:
        note("notice", f"Dateisystem im Image: {listing or 'leer'}")
        return 0
    if "setup.json" not in files:
        note("error", f"setup.json fehlt im Image. Gefunden: {listing or 'nichts'}")
        return 1
    with fs.open("/setup.json", "r") as f:
        s = json.load(f)
    src = json.load(open("setup/default-setup.json", encoding="utf-8"))
    need = [b for k, b in (("img", "img.bin"), ("vid", "vid.bin")) if src.get(k)]
    missing = [b for b in need if b not in files]
    if missing:
        note("error", f"Im Image fehlt: {', '.join(missing)}. Gefunden: {listing}")
        return 1
    ui = json.loads(s.get("cfg", {}).get("ui") or "{}")
    vid = ui.get("vid") or {}
    note("notice", f"Standard-Einrichtung {s.get('ver')} ist im Image: {listing} · Video {'an' if vid.get('on') else 'aus'}"
                   f", {vid.get('n', 0)} Bilder · Winkel {s.get('cfg', {}).get('ang')}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
