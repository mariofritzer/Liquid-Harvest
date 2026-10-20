# Prüft den Weg des Online-Updates genau so, wie ihn der ESP32 geht – aber vom CI-Runner aus:
#  1. api.github.com/repos/<repo>/releases per HTTP/1.0 (wie die Firmware) – nur mit den Root-Zertifikaten aus src/ca_roots.h
#  2. Release mit liquid-harvest-ota.bin finden
#  3. Datei über github.com/.../releases/download/... laden (Weiterleitung auf release-assets.githubusercontent.com),
#     wieder nur mit diesen Wurzeln, und prüfen, dass es ein ESP32-App-Image ist
# Scheitert etwas, könnten Geräte im Feld nicht mehr online updaten -> Build-Fehler.
#
# Aufruf: python scripts/check_update_tls.py [tag]
import http.client, json, os, re, ssl, sys, urllib.request

ASSET = "liquid-harvest-ota.bin"


def note(kind, msg):
    print(f"::{kind} title=Online-Update::{msg}" if os.environ.get("GITHUB_ACTIONS") else f"[{kind}] {msg}")


def main(tag=None):
    hdr = open("src/ca_roots.h", encoding="utf-8").read()
    pem = re.search(r'R"PEM\((.*?)\)PEM"', hdr, re.S).group(1)
    repo = re.search(r'#define\s+FW_REPO\s+"([^"]+)"', open("src/version.h").read()).group(1)
    if os.environ.get("GITHUB_REPOSITORY") and os.environ["GITHUB_REPOSITORY"].lower() != repo.lower():
        note("warning", f"FW_REPO in src/version.h ist {repo}, gebaut wird aber {os.environ['GITHUB_REPOSITORY']} – Geräte suchen Updates in {repo}.")
    ctx = ssl.create_default_context(cadata=pem)   # NUR die eingebetteten Wurzeln

    # 1. Release-Liste wie der ESP32: HTTP/1.0, kein Chunked-Encoding
    c = http.client.HTTPSConnection("api.github.com", context=ctx, timeout=20)
    c._http_vsn, c._http_vsn_str = 10, "HTTP/1.0"
    try:
        c.request("GET", f"/repos/{repo}/releases?per_page=15",
                  headers={"User-Agent": "LiquidHarvest-CI", "Accept": "application/vnd.github+json"})
        r = c.getresponse()
        body = r.read()
    except ssl.SSLError as e:
        note("error", f"api.github.com lässt sich mit den eingebetteten Root-Zertifikaten nicht prüfen: {e}")
        return 1
    if r.status != 200:
        note("warning", f"api.github.com antwortet {r.status} – Prüfung übersprungen")
        return 0
    if (r.getheader("Transfer-Encoding") or "").lower() == "chunked":
        note("error", "api.github.com antwortet trotz HTTP/1.0 chunked – die Firmware könnte die Liste nicht lesen")
        return 1
    rels = [x for x in json.loads(body) if not x.get("draft") and any(a["name"] == ASSET for a in x.get("assets", []))]
    if tag:
        rels = [x for x in rels if x["tag_name"] == tag] or rels
    if not rels:
        note("notice", "Noch kein Release mit Update-Datei – Download-Prüfung übersprungen")
        return 0
    rel = rels[0]
    size = next(a["size"] for a in rel["assets"] if a["name"] == ASSET)

    # 2. Download über die Weiterleitung, nur mit den eingebetteten Wurzeln
    url = f"https://github.com/{repo}/releases/download/{rel['tag_name']}/{ASSET}"
    try:
        op = urllib.request.build_opener(urllib.request.HTTPSHandler(context=ctx))
        with op.open(urllib.request.Request(url, headers={"User-Agent": "LiquidHarvest-CI"}), timeout=60) as f:
            data = f.read()
            final = f.geturl()
    except ssl.SSLError as e:
        note("error", f"Download-Server lässt sich mit den eingebetteten Root-Zertifikaten nicht prüfen: {e}")
        return 1
    host = re.sub(r"^https://([^/]+)/.*$", r"\1", final)
    if len(data) != size or data[:1] != b"\xe9":
        note("error", f"{rel['tag_name']}/{ASSET}: {len(data)} Bytes statt {size} oder kein ESP32-Image")
        return 1
    note("notice", f"Online-Update geprüft: {len(rels)} Version(en), {rel['tag_name']} lädt über {host} ({size // 1024} kB) – Zertifikate passen")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else None))
