# Erzeugt nach dem Build ein einzelnes Flash-Image (Bootloader + Partitionen + App)
# für das Web-Flash-Tool: .pio/build/<env>/firmware-merged.bin  (Offset 0x0)
import subprocess
Import("env")  # noqa: F821

def merge_bin(source, target, env):
    out = env.subst("$BUILD_DIR/${PROGNAME}-merged.bin")
    cmd = [env.subst("$PYTHONEXE"), env.subst("$OBJCOPY"), "--chip", env.get("BOARD_MCU"),
           "merge_bin", "-o", out, "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for offset, image in env.get("FLASH_EXTRA_IMAGES", []):
        cmd += [offset, env.subst(image)]
    cmd += [env.subst("$ESP32_APP_OFFSET"), target[0].get_abspath()]
    print("Merge:", " ".join(cmd))
    subprocess.check_call(cmd)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821


# Nach "pio run -t buildfs": Dateisystem (Standard-Einrichtung) an seine Partitionsadresse ins Gesamt-Image legen
def merge_fs(source, target, env):
    merged = env.subst("$BUILD_DIR/${PROGNAME}-merged.bin")
    fs_start = int(env.get("FS_START", 0x310000))
    img = open(merged, "rb").read()
    if len(img) > fs_start:
        raise SystemExit("Firmware-Image überlappt das Dateisystem")
    fs = open(target[0].get_abspath(), "rb").read()
    open(merged, "wb").write(img + b"\xff" * (fs_start - len(img)) + fs)   # Lücke wie gelöschter Flash
    print(f"Merge FS: Dateisystem ({len(fs)//1024} kB) bei {hex(fs_start)} angehängt")

env.AddPostAction("$BUILD_DIR/littlefs.bin", merge_fs)  # noqa: F821
