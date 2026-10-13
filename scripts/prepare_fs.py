# Baut den Inhalt des Dateisystems (data/) aus einer optionalen Standard-Einrichtung:
#   setup/default-setup.json  (Export aus der App: ⚙️ → Darstellung → Exportieren)
# Bild und Video werden als Dateien abgelegt, die Einstellungen als setup.json.
# Die Firmware übernimmt sie beim ersten Start automatisch.
import base64, hashlib, json, os, shutil, sys

def prepare(root):
    src = os.path.join(root, "setup", "default-setup.json")
    data = os.path.join(root, "data")
    shutil.rmtree(data, ignore_errors=True)
    os.makedirs(data, exist_ok=True)
    if not os.path.exists(src):
        print("Standard-Einrichtung: keine (setup/default-setup.json fehlt)")
        return False
    d = json.load(open(src, encoding="utf-8"))
    if d.get("app") != "liquid-harvest-setup":
        sys.exit("setup/default-setup.json ist keine Liquid-Harvest-Einrichtung")
    for key, base in (("img", "img"), ("vid", "vid")):
        uri = d.get(key)
        if not uri:
            continue
        head, b64 = uri.split(",", 1)
        mime = head[5:].split(";")[0]
        open(os.path.join(data, base + ".bin"), "wb").write(base64.b64decode(b64))
        open(os.path.join(data, base + ".type"), "w").write(mime)
    cfg = d.get("cfg", {})
    ver = hashlib.md5((json.dumps(cfg, sort_keys=True) + (d.get("vid") or "")[:4096] + str(len(d.get("vid") or ""))).encode()).hexdigest()[:12]
    json.dump({"ver": ver, "name": d.get("name", ""), "cfg": cfg}, open(os.path.join(data, "setup.json"), "w"))
    size = sum(os.path.getsize(os.path.join(data, f)) for f in os.listdir(data))
    print(f"Standard-Einrichtung übernommen: {size // 1024} kB in data/")
    return True

try:
    Import("env")  # noqa: F821  (PlatformIO)
    prepare(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    prepare(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
