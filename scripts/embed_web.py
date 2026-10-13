# Packt web/index.html gzip-komprimiert als C-Array in src/web_index.h
# Läuft automatisch vor jedem PlatformIO-Build (extra_scripts = pre:...)
import gzip, os, sys

def embed(root):
    src = os.path.join(root, "web", "index.html")
    dst = os.path.join(root, "src", "web_index.h")
    data = gzip.compress(open(src, "rb").read(), compresslevel=9, mtime=0)
    lines = []
    for i in range(0, len(data), 20):
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 20]))
    out = ("#pragma once\n// AUTOMATISCH ERZEUGT aus web/index.html – nicht von Hand bearbeiten!\n"
           "#include <Arduino.h>\n"
           f"static const size_t WEB_INDEX_GZ_LEN = {len(data)};\n"
           "static const uint8_t WEB_INDEX_GZ[] PROGMEM = {\n" + ",\n".join(lines) + "\n};\n")
    if not os.path.exists(dst) or open(dst).read() != out:
        open(dst, "w").write(out)
    print(f"web_index.h: {len(data)} Bytes (gzip)")

try:
    Import("env")  # noqa: F821  (PlatformIO)
    embed(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    embed(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
