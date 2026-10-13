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
