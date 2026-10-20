# Erzeugt docs/images/wiring.svg als Schaltplan im Stil eines EDA-Programms (KiCad-ähnlich)
# Aufruf: python3 scripts/wiring_gen.py docs/images/wiring.svg
import sys

W, H = 1760, 1452
DY = 212   # rechte Spalte unter dem Pumpenblock nach unten versetzt
BG, WIRE, COMP, FILL = "#FAF8F2", "#008400", "#840000", "#FFFFC2"
PINNAME, PINNUM, REF, NC = "#006464", "#A90000", "#006464", "#0000C8"
FONT = "'DejaVu Sans Mono','Liberation Mono',Consolas,'Courier New',monospace"
o = []


def t(x, y, s, size=12, color="#000", anchor="start", weight="normal", rot=0, italic=False):
    tr = f' transform="rotate({rot} {x} {y})"' if rot else ""
    st = ' font-style="italic"' if italic else ""
    o.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}"{st}{tr}>{s}</text>')


def wire(*pts, color=WIRE, w=1.6):
    o.append(f'<polyline points="{" ".join(f"{x},{y}" for x, y in pts)}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linejoin="round"/>')


def line(x1, y1, x2, y2, color=COMP, w=1.4, extra=""):
    o.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}" {extra}/>')


def dot(x, y):
    o.append(f'<circle cx="{x}" cy="{y}" r="3.4" fill="{WIRE}"/>')


def nc(x, y):
    line(x - 5, y - 5, x + 5, y + 5, NC, 1.5); line(x - 5, y + 5, x + 5, y - 5, NC, 1.5)


def box(x, y, w, h):
    o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"/>')


def pin(x, y, d, name, num=None, L=20, namecol=PINNAME):
    """Pin am Gehäuse (x,y), zeigt in Richtung d (l/r/u/d). Liefert den Anschlusspunkt."""
    dx, dy = {"l": (-L, 0), "r": (L, 0), "u": (0, -L), "d": (0, L)}[d]
    line(x, y, x + dx, y + dy, COMP, 1.4)
    if name:
        if d == "l": t(x + 5, y + 4, name, 11, namecol)
        elif d == "r": t(x - 5, y + 4, name, 11, namecol, "end")
        elif d == "u": t(x, y + 14, name, 11, namecol, "middle")
        else: t(x, y - 6, name, 11, namecol, "middle")
    if num is not None:
        if d in "lr": t(x + dx / 2, y - 4, str(num), 9, PINNUM, "middle")
        else: t(x + 4, y + dy / 2, str(num), 9, PINNUM)
    return x + dx, y + dy


def label(x, y, name, side):
    """Netzlabel am Leitungsende (x,y); side = Richtung, in die die Leitung vom Label weg zum Bauteil läuft."""
    if side == "r": t(x + 3, y - 4, name, 12, "#000")
    else: t(x - 3, y - 4, name, 12, "#000", "end")


def pwr(x, y, name, d="u"):
    """Versorgungssymbol (+5V, +12V, +3V3) – Strich mit Querbalken."""
    if d == "u":
        line(x, y, x, y - 14); line(x - 8, y - 14, x + 8, y - 14, COMP, 1.8); t(x, y - 19, name, 11, COMP, "middle")
    elif d == "l":
        line(x, y, x - 14, y); line(x - 14, y - 8, x - 14, y + 8, COMP, 1.8); t(x - 19, y + 4, name, 11, COMP, "end")
    elif d == "r":
        line(x, y, x + 14, y); line(x + 14, y - 8, x + 14, y + 8, COMP, 1.8); t(x + 19, y + 4, name, 11, COMP)


def gnd(x, y, d="d"):
    if d == "d":
        line(x, y, x, y + 10); line(x - 9, y + 10, x + 9, y + 10, COMP, 1.8); line(x - 5.5, y + 14, x + 5.5, y + 14, COMP, 1.6); line(x - 2, y + 18, x + 2, y + 18, COMP, 1.6)
    elif d == "l":
        line(x, y, x - 10, y); line(x - 10, y - 9, x - 10, y + 9, COMP, 1.8); line(x - 14, y - 5.5, x - 14, y + 5.5, COMP, 1.6); line(x - 18, y - 2, x - 18, y + 2, COMP, 1.6)
    elif d == "r":
        line(x, y, x + 10, y); line(x + 10, y - 9, x + 10, y + 9, COMP, 1.8); line(x + 14, y - 5.5, x + 14, y + 5.5, COMP, 1.6); line(x + 18, y - 2, x + 18, y + 2, COMP, 1.6)


def res_h(x1, x2, y, ref, val):
    m = (x1 + x2) / 2
    line(x1, y, m - 15, y); line(m + 15, y, x2, y)
    o.append(f'<rect x="{m-15}" y="{y-5}" width="30" height="10" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    t(m, y - 10, ref, 11, REF, "middle"); t(m, y + 19, val, 11, REF, "middle")


def res_v(x, y1, y2, ref, val, side="r"):
    m = (y1 + y2) / 2
    line(x, y1, x, m - 15); line(x, m + 15, x, y2)
    o.append(f'<rect x="{x-5}" y="{m-15}" width="10" height="30" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    if side == "r": t(x + 10, m - 3, ref, 11, REF); t(x + 10, m + 11, val, 11, REF)
    else: t(x - 10, m - 3, ref, 11, REF, "end"); t(x - 10, m + 11, val, 11, REF, "end")


def section(x1, y1, x2, y2, title):
    o.append(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" fill="none" stroke="{COMP}" stroke-width="1" stroke-dasharray="7 4" opacity=".55"/>')
    t(x1 + 10, y1 + 18, title, 13, COMP, weight="bold")


# ---------------------------------------------------------------- Blatt
o.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
o.append(f'<rect x="20" y="20" width="{W-40}" height="{H-40}" fill="none" stroke="{COMP}" stroke-width="1.6"/>')
o.append(f'<rect x="36" y="36" width="{W-72}" height="{H-72}" fill="none" stroke="{COMP}" stroke-width="1"/>')
for i in range(8):
    x = 36 + (W - 72) * (i + .5) / 8
    t(x, 32, str(i + 1), 11, COMP, "middle"); t(x, H - 23, str(i + 1), 11, COMP, "middle")
    if i: line(36 + (W - 72) * i / 8, 20, 36 + (W - 72) * i / 8, 36, COMP, 1); line(36 + (W - 72) * i / 8, H - 36, 36 + (W - 72) * i / 8, H - 20, COMP, 1)
for i, c in enumerate("ABCDEF"):
    y = 36 + (H - 72) * (i + .5) / 6
    t(28, y + 4, c, 11, COMP, "middle"); t(W - 28, y + 4, c, 11, COMP, "middle")
    if i: line(20, 36 + (H - 72) * i / 6, 36, 36 + (H - 72) * i / 6, COMP, 1); line(W - 36, 36 + (H - 72) * i / 6, W - 20, 36 + (H - 72) * i / 6, COMP, 1)

# ---------------------------------------------------------------- Stromversorgung
section(50, 54, 1120, 336, "Stromversorgung")
# U1 PD-Trigger mit USB-C-Buchse
box(200, 130, 130, 110)
o.append(f'<rect x="192" y="168" width="22" height="34" rx="9" fill="#e9e6da" stroke="{COMP}" stroke-width="1.4"/>')
t(223, 190, "USB-C", 10, PINNAME)
t(200, 122, "U1", 12, REF); t(265, 258, "PDC004 (fest 12 V)", 11, REF, "middle")
a = pin(330, 160, "r", "OUT+", 1); b = pin(330, 215, "r", "OUT−", 2)
t(70, 172, "USB-C PD-", 11, "#333"); t(70, 187, "Netzteil", 11, "#333"); t(70, 202, "≥ 20 W, mit", 11, "#333"); t(70, 217, "12-V-Profil", 11, "#333")
o.append(f'<path d="M160 190 h24 m-7 -5 l7 5 l-7 5" fill="none" stroke="#333" stroke-width="1.4"/>')
# +12V-Netz
wire(a, (520, 160)); dot(430, 160); wire((430, 160), (430, 120)); pwr(430, 120, "+12V")
wire(b, (520, 215)); dot(460, 215); gnd(460, 215)
# U2 Step-Down
box(540, 130, 140, 110)
t(540, 122, "U2", 12, REF); t(610, 258, "Step-Down ≥ 15 W", 11, REF, "middle")
t(610, 194, "12V→5V", 12, PINNAME, "middle", "bold")
pin(540, 160, "l", "IN+", 1); pin(540, 215, "l", "IN−", 2)
c = pin(680, 160, "r", "OUT+", 3); d = pin(680, 215, "r", "OUT−", 4)
wire(c, (880, 160)); dot(800, 160); wire((800, 160), (800, 120)); pwr(800, 120, "+5V")
wire(d, (880, 215)); dot(830, 215); gnd(830, 215)
t(895, 164, "→ ESP32 VIN, Servo,", 11, "#333"); t(895, 179, "   WS2812B", 11, "#333")
t(895, 219, "Sternpunkt Masse:", 11, "#333"); t(895, 234, "alle GND hier zusammen", 11, "#333")
t(70, 300, "+3V3 kommt vom Spannungsregler auf dem ESP32-Board (Pin 30) und versorgt HX711 und Drehencoder.", 11, "#555", italic=True)

# ---------------------------------------------------------------- ESP32 DevKit V1
EX, EY, EW, SP = 800, 420, 200, 36
EH = 14 * SP + 60
box(EX, EY, EW, EH)
o.append(f'<rect x="{EX+60}" y="{EY+8}" width="80" height="26" fill="none" stroke="{COMP}" stroke-width="1" stroke-dasharray="3 3"/>')
t(EX + 100, EY + 25, "Antenne", 10, PINNAME, "middle")
o.append(f'<rect x="{EX+78}" y="{EY+EH-18}" width="44" height="18" rx="4" fill="none" stroke="{COMP}" stroke-width="1.2"/>')
t(EX + 100, EY + EH - 5, "USB", 10, PINNAME, "middle")
t(EX, EY - 10, "U3", 12, REF); t(EX + EW, EY - 10, "ESP32 DevKit V1 (30 Pin)", 11, REF, "end")
t(EX + 100, EY + EH / 2, "ESP32-WROOM-32", 12, PINNAME, "middle", "bold", rot=-90)
left = [("EN", None), ("IO36/VP", "DT1"), ("IO39/VN", "DT2"), ("IO34", "DT3"), ("IO35", "DT4"), ("IO32", "DT5"), ("IO33", "DT6"),
        ("IO25", "BTN"), ("IO26", "BTN_LED"), ("IO27", "PUMP_PWM"), ("IO14", None), ("IO12", None), ("IO13", "SERVO"), ("GND", "GND"), ("VIN", "+5V")]
right = [("IO23", "LED_DIN"), ("IO22", None), ("TX0", None), ("RX0", None), ("IO21", None), ("IO19", "PUMP_IN2"), ("IO18", "ENC_SW"), ("IO5", None),
         ("TX2/IO17", "ENC_DT"), ("RX2/IO16", "ENC_CLK"), ("IO4", "HX_SCK"), ("IO2", None), ("IO15", None), ("GND", "GND"), ("3V3", "+3V3")]
for i, (n, net) in enumerate(left):
    y = EY + 40 + i * SP
    px, py = pin(EX, y, "l", n, i + 1, L=30)
    if net is None: nc(px, py)
    elif net == "GND": wire((px, py), (px - 20, py)); gnd(px - 20, py, "l")
    elif net == "+5V": wire((px, py), (px - 20, py)); pwr(px - 20, py, "+5V", "l")
    else: wire((px, py), (px - 100, py)); label(px - 100, py, net, "r")
for i, (n, net) in enumerate(right):
    y = EY + 40 + i * SP
    px, py = pin(EX + EW, y, "r", n, 30 - i, L=30)
    if net is None: nc(px, py)
    elif net == "GND": wire((px, py), (px + 20, py)); gnd(px + 20, py, "r")
    elif net == "+3V3": wire((px, py), (px + 20, py)); pwr(px + 20, py, "+3V3", "r")
    else: wire((px, py), (px + 100, py)); label(px + 100, py, net, "l")

# ---------------------------------------------------------------- Waagen
section(50, 360, 660, 874, "Waagen – 6 × HX711, gemeinsamer Takt HX_SCK")
for i in range(6):
    col, row = i % 2, i // 2
    x0, y0 = 62 + col * 300, 386 + row * 158
    bx, by = x0 + 100, y0 + 40
    box(bx, by, 92, 96)
    t(bx, by - 8, f"U{4+i}", 12, REF)
    t(bx + 46, by + 48, "HX711", 12, PINNAME, "middle", "bold", rot=-90)
    ys = [by + 18, by + 38, by + 58, by + 78]
    p = pin(bx, ys[0], "l", "VCC", 4); wire(p, (x0 + 60, ys[0]), (x0 + 60, ys[0] - 18)); pwr(x0 + 60, ys[0] - 18, "+3V3")
    p = pin(bx, ys[1], "l", "SCK", 3); wire(p, (x0 + 4, ys[1])); label(x0 + 4, ys[1], "HX_SCK", "r")
    p = pin(bx, ys[2], "l", "DT", 2); wire(p, (x0 + 4, ys[2])); label(x0 + 4, ys[2], f"DT{i+1}", "r")
    p = pin(bx, ys[3], "l", "GND", 1); wire(p, (x0 + 60, ys[3]), (x0 + 60, ys[3] + 8)); gnd(x0 + 60, ys[3] + 8)
    # Wägezelle (Messbrücke)
    lx = bx + 92 + 40
    o.append(f'<rect x="{lx}" y="{by+6}" width="52" height="84" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"/>')
    t(lx, by - 8, f"LC{i+1}", 12, REF)
    t(lx + 40, by + 48, "1 kg", 11, PINNAME, "middle", rot=-90)
    for j, (pn, wcol) in enumerate([("E+", "rt"), ("E−", "sw"), ("A−", "ws"), ("A+", "gn")]):
        p = pin(bx + 92, ys[j], "r", pn, 5 + j)
        wire(p, (lx, ys[j]))
        t(lx + 4, ys[j] + 4, wcol, 10, PINNAME)
t(62, 864, "rt/sw/ws/gn = Aderfarben der Wägezelle · RATE-Pin der HX711 auf GND (10 Messungen/s)", 11, "#555", italic=True)

# ---------------------------------------------------------------- Taster
section(50, 884, 660, 1100, "Taster mit Beleuchtung")
wire((150, 950), (200, 950)); label(150, 950, "BTN", "r")
# SW1 Taster (Schließer)
o.append(f'<circle cx="204" cy="950" r="3.5" fill="none" stroke="{COMP}" stroke-width="1.5"/><circle cx="256" cy="950" r="3.5" fill="none" stroke="{COMP}" stroke-width="1.5"/>')
line(206, 938, 254, 938, COMP, 1.6); line(230, 938, 230, 926, COMP, 1.6); line(222, 926, 238, 926, COMP, 1.6)
t(230, 918, "SW1", 11, REF, "middle"); t(230, 972, "Taster (NO)", 11, REF, "middle")
wire((260, 950), (300, 950), (300, 960)); gnd(300, 960)
# BTN_LED -> R3 -> Q1
wire((150, 1030), (190, 1030)); label(150, 1030, "BTN_LED", "r")
res_h(190, 290, 1030, "R3", "1 kΩ")
wire((290, 1030), (346, 1030))
o.append(f'<circle cx="368" cy="1030" r="20" fill="none" stroke="{COMP}" stroke-width="1.5"/>')
line(358, 1016, 358, 1044, COMP, 2.2); line(346, 1030, 358, 1030)
line(358, 1023, 376, 1010); line(376, 1010, 376, 994)
line(358, 1037, 376, 1050); line(376, 1050, 376, 1064)
o.append(f'<path d="M370 1049 l6 1.5 l-3 -5.5 z" fill="{COMP}"/>')
t(396, 1026, "Q1", 11, REF); t(396, 1040, "BC547", 11, REF)
wire((376, 1064), (376, 1070)); gnd(376, 1070)
# LED im Taster
wire((376, 994), (376, 986))
o.append(f'<path d="M366 962 h20 l-10 18 z" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
line(366, 980, 386, 980, COMP, 1.8); line(376, 980, 376, 986); line(376, 962, 376, 940)
o.append(f'<path d="M390 966 l10 -8 m-4 0 l4 0 l0 4 M392 976 l10 -8 m-4 0 l4 0 l0 4" fill="none" stroke="{COMP}" stroke-width="1.2"/>')
t(410, 968, "D8  Taster-LED", 11, REF); t(410, 982, "(5–12 V, Vorwiderstand im Taster)", 11, REF)
pwr(376, 940, "+12V")

# ---------------------------------------------------------------- Pumpe
section(1150, 54, 1724, 552, "Pumpe – Variante A oder B bestücken")
t(1170, 96, "A", 22, COMP, weight="bold"); t(1170, 114, "MOSFET-", 11, "#222"); t(1170, 128, "Modul", 11, "#222")
line(1160, 345, 1714, 345, COMP, 1, 'stroke-dasharray="3 4" opacity=".6"')
wire((1260, 190), (1340, 190)); label(1260, 190, "PUMP_PWM", "r")
dot(1300, 190); res_v(1300, 190, 260, "R2", "10 kΩ", "l"); gnd(1300, 260)
box(1360, 130, 130, 150)
t(1360, 122, "U10", 12, REF); t(1425, 298, "D4184 MOSFET-Modul", 11, REF, "middle")
pin(1360, 190, "l", "PWM", 1)
p = pin(1360, 230, "l", "GND", 2); p2 = pin(1360, 262, "l", "VIN−", 4)
wire(p, (1330, 230), (1330, 262), p2); dot(1330, 262); wire((1330, 262), (1330, 280)); gnd(1330, 280)
p = pin(1425, 130, "u", "VIN+", 3); pwr(1425, 110, "+12V")
a = pin(1490, 170, "r", "OUT+", 5); b = pin(1490, 250, "r", "OUT−", 6)
wire(a, (1640, 170), (1640, 186)); wire(b, (1640, 250), (1640, 234))
dot(1570, 170); dot(1570, 250)
# Freilaufdiode D1 (Kathode an OUT+)
wire((1570, 170), (1570, 198)); wire((1570, 222), (1570, 250))
o.append(f'<path d="M1560 222 h20 l-10 -18 z" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
line(1560, 202, 1580, 202, COMP, 1.8); line(1570, 202, 1570, 198)
t(1520, 206, "D1", 11, REF); t(1520, 220, "1N5819", 11, REF)
# Pumpenmotor M2
o.append(f'<circle cx="1640" cy="210" r="24" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"/>')
t(1640, 216, "M", 16, COMP, "middle", "bold")
t(1672, 204, "M2", 11, REF); t(1672, 218, "Pumpe", 11, REF); t(1672, 232, "12 V", 11, REF)
t(1170, 315, "D1 zwingend – das Modul hat keine Freilaufdiode.", 11, "#555", italic=True); t(1170, 329, "U10: 2 × AOD4184A, Last 5–36 V, Steuerung 3,3–20 V, PWM bis 20 kHz", 11, "#555", italic=True)

# Variante B: H-Brücke – vorwärts (IN1) und rückwärts zum Zurücksaugen (IN2), beide per PWM
t(1170, 376, "B", 22, COMP, weight="bold"); t(1170, 394, "H-Brücke", 11, "#222")
wire((1260, 420), (1340, 420)); label(1260, 420, "PUMP_PWM", "r")
wire((1260, 450), (1340, 450)); label(1260, 450, "PUMP_IN2", "r")
box(1360, 392, 130, 116)
t(1360, 384, "U11", 12, REF); t(1425, 470, "DRV8871 o. ä.", 11, REF, "middle", "bold")
pin(1360, 420, "l", "IN1", 1); pin(1360, 450, "l", "IN2", 2)
p = pin(1360, 486, "l", "GND", 3); wire(p, (1330, 486)); gnd(1330, 486)
pin(1425, 392, "u", "VM", 4); pwr(1425, 372, "+12V")
a = pin(1490, 420, "r", "OUT1", 5); b = pin(1490, 480, "r", "OUT2", 6)
wire(a, (1640, 420), (1640, 426)); wire(b, (1640, 480), (1640, 474))
o.append(f'<circle cx="1640" cy="450" r="24" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"/>')
t(1640, 456, "M", 16, COMP, "middle", "bold")
t(1672, 444, "M2", 11, REF); t(1672, 458, "Pumpe", 11, REF); t(1672, 472, "12 V", 11, REF)
t(1170, 528, "Kein D1/R2 nötig. BTS7960: RPWM = IN1, LPWM = IN2, R_EN + L_EN an +3V3.", 11, "#555", italic=True)
t(1170, 542, "L298N: Jumper auf ENA stecken lassen. App: Dosierung → Pumpentreiber.", 11, "#555", italic=True)

# ---------------------------------------------------------------- Glas-LEDs
shift_from = len(o)
section(1150, 360, 1724, 540, "Glas-LEDs – WS2812B, LED 1 = Glas 1")
wire((1250, 460), (1262, 460)); label(1250, 460, "LED_DIN", "l")
wire((1160, 460), (1250, 460))
res_h(1262, 1318, 460, "R1", "330 Ω")
x = 1328
pwr(1300, 412, "+5V"); wire((1300, 412), (1300 + 0, 412))
for i in range(6):
    bx = x + i * 62
    box(bx, 430, 42, 62)
    t(bx + 21, 456, f"D{2+i}", 11, REF, "middle", "bold"); t(bx + 21, 470, f"Glas", 9, PINNAME, "middle"); t(bx + 21, 481, f"{i+1}", 9, PINNAME, "middle")
    line(bx, 460, bx - 10, 460) if i == 0 else None
    pin(bx + 42, 460, "r", None, None, L=10)
    if i > 0: line(bx - 10, 460, bx, 460)
    pin(bx + 21, 430, "u", None, None, L=18); pin(bx + 21, 492, "d", None, None, L=18)
    dot(bx + 21, 412) if i < 5 else None
    dot(bx + 21, 510) if i < 5 else None
wire((1318, 460), (1328, 460))
wire((1300, 412), (x + 5 * 62 + 21, 412))
wire((1300, 510), (x + 5 * 62 + 21, 510)); gnd(1300, 510)
wire((x + 5 * 62 + 52, 460), (x + 5 * 62 + 66, 460)); t(x + 5 * 62 + 54, 452, "→D9", 9, "#000")
t(1340, 532, "VDD oben · DIN → DOUT → DIN … · VSS unten", 10, "#555", italic=True)

# ---------------------------------------------------------------- Drehencoder
section(1150, 560, 1724, 716, "Drehencoder")
box(1420, 590, 110, 112)
t(1420, 582, "ENC1", 12, REF); t(1530, 582, "KY-040", 11, REF, "end")
ys = [600, 622, 644, 666, 688]
p = pin(1420, ys[0], "l", "+", 4); wire(p, (1370, ys[0])); pwr(1370, ys[0], "+3V3", "l")
for j, (pn, net) in enumerate([("CLK", "ENC_CLK"), ("DT", "ENC_DT"), ("SW", "ENC_SW")]):
    p = pin(1420, ys[j + 1], "l", pn, j + 1); wire(p, (1260, ys[j + 1])); label(1260, ys[j + 1], net, "r")
p = pin(1420, ys[4], "l", "GND", 5); wire(p, (1370, ys[4])); gnd(1370, ys[4], "l")
o.append(f'<circle cx="1630" cy="644" r="26" fill="none" stroke="{COMP}" stroke-width="1.4"/><circle cx="1630" cy="644" r="9" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
for k in range(12):
    import math
    a_ = k * math.pi / 6
    line(1630 + 26 * math.cos(a_), 644 + 26 * math.sin(a_), 1630 + 31 * math.cos(a_), 644 + 31 * math.sin(a_), COMP, 1.2)
t(1540, 706, "Pull-ups auf dem Modul", 10, "#555", italic=True)

# ---------------------------------------------------------------- Servo
section(1150, 736, 1724, 880, "Servo (Abtankrohr)")
wire((1260, 790), (1420, 790)); label(1260, 790, "SERVO", "r")
box(1440, 770, 100, 80)
t(1440, 762, "M1", 12, REF); t(1540, 762, "MG90S", 11, REF, "end")
pin(1440, 790, "l", "PWM", 1); t(1430, 806, "or", 10, PINNAME, "middle")
a = pin(1540, 790, "r", "+5V", 2); b = pin(1540, 830, "r", "GND", 3)
t(1566, 784, "rt", 10, PINNAME); t(1566, 846, "br", 10, PINNAME)
wire(a, (1660, 790)); wire(b, (1660, 830))
dot(1610, 790); dot(1610, 830)
# C1 Elko
line(1610, 790, 1610, 804); line(1598, 804, 1622, 804, COMP, 2.2)
o.append(f'<path d="M1598 816 q12 -6 24 0" fill="none" stroke="{COMP}" stroke-width="2.2"/>'); line(1610, 813, 1610, 830)
t(1592, 802, "+", 11, COMP, "end")
pwr(1660, 790, "+5V", "r"); gnd(1660, 830, "r")
t(1572, 862, "C1  470–1000 µF", 11, REF)

# ---------------------------------------------------------------- Hinweise
section(1150, 900, 1724, 1072, "Hinweise")
for k, s in enumerate([
    "1. Masse sternförmig führen – Pumpen- und Servostrom nicht über die Waagen-Masse.",
    "2. HX711 und Drehencoder an +3V3, nicht an 5 V (Pegel passt dann zum ESP32).",
    "3. C1 möglichst direkt an den Servo-Anschluss löten.",
    "4. Taster-LED (5–12 V) an +12V, Q1 schaltet nach GND; IO26 dimmt per PWM.",
    "5. Schlauch und Rohr lebensmittelecht (Silikon, alkoholbeständig).",
    "6. Ungenutzte Pins (×) frei lassen; IO0, 2, 5, 12, 15 sind Boot-Pins."]):
    t(1162, 942 + k * 21, s, 11, "#222")

o[shift_from:] = [f'<g transform="translate(0,{DY})">'] + o[shift_from:] + ["</g>"]

# ---------------------------------------------------------------- Rundumleuchte
section(50, 1120, 660, 1416, "Rundumleuchte (optional) – am Ende der LED-Kette")
wire((150, 1236), (210, 1236)); label(150, 1236, "BEACON_DIN", "r")
pwr(190, 1188, "+5V"); gnd(190, 1290)
for i in range(4):
    bx = 230 + i * 92
    dash = i > 0
    o.append(f'<rect x="{bx}" y="{1206}" width="46" height="62" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"{' stroke-dasharray="5 3"' if dash else ''}/>')
    t(bx + 23, 1232, f"D{9+i}", 11, REF, "middle", "bold"); t(bx + 23, 1248, f"LED {7+i}", 9, PINNAME, "middle")
    line(bx - 20, 1236, bx, 1236); line(bx + 46, 1236, bx + 66, 1236) if i < 3 else None
    line(bx + 23, 1206, bx + 23, 1188); line(bx + 23, 1268, bx + 23, 1290)
    dot(bx + 23, 1188) if i < 3 else None; dot(bx + 23, 1290) if i < 3 else None
wire((190, 1188), (230 + 3 * 92 + 23, 1188)); wire((190, 1290), (230 + 3 * 92 + 23, 1290))
wire((210, 1236), (230, 1236))
nc(230 + 3 * 92 + 56, 1236)
t(64, 1330, "D9 sitzt am DOUT von D7 (Glas 6). D10–D12 (gestrichelt) nach Bedarf – max. 4.", 11, "#222")
t(64, 1352, "App: LEDs → LED-Anzahl erhöhen (z. B. 8) → Rundumleuchte → LED Nr. 7, 8 eintragen.", 11, "#222")
t(64, 1374, "Muster wählbar: Drehlicht, abwechselnd faden, Doppelblitz, Wechselblitz,", 11, "#222")
t(64, 1392, "Lauflicht, Dauerlicht. Leuchtet, solange das Rohr fährt oder die Pumpe läuft.", 11, "#222")

# ---------------------------------------------------------------- Stückliste
section(670, 1010, 1130, 1416, "Stückliste")
rows = [("U1", "PDC004 USB-C PD-Trigger, 12 V", "1"), ("U2", "Step-Down 12 V → 5 V, ≥ 15 W", "1"), ("U3", "ESP32 DevKit V1 (WROOM-32)", "1"),
        ("U4–U9", "HX711-Modul", "6"), ("LC1–LC6", "Wägezelle 1 kg", "6"), ("U10", "D4184 MOSFET-Modul (Variante A)", "1"), ("U11", "H-Brücke DRV8871 (Variante B)", "(1)"), ("M1 / M2", "Servo MG90S / Pumpe 12 V", "1/1"),
        ("D1", "1N5819 (nur Variante A)", "1"), ("D2–D7", "WS2812B Glas-LEDs", "6"), ("D9–D12", "WS2812B Rundumleuchte", "1–4"), ("ENC1 / SW1", "KY-040 / Taster mit LED", "1/1"),
        ("Q1, R1–R3, C1", "BC547 · 330 Ω · 10 kΩ · 1 kΩ · 1000 µF", ""),
        ("", "R2 nur bei Variante A", "")]
y = 1046
for r in rows:
    t(682, y, r[0], 11, PINNUM); t(800, y, r[1], 11, "#222"); t(1118, y, r[2], 11, "#222", "end")
    line(678, y + 5, 1122, y + 5, COMP, .5, 'opacity=".4"'); y += 22

# ---------------------------------------------------------------- Schriftfeld
X1, Y1, X2, Y2 = 1150, 1092 + DY, 1724, 1204 + DY
o.append(f'<rect x="{X1}" y="{Y1}" width="{X2-X1}" height="{Y2-Y1}" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
line(X1, Y1 + 46, X2, Y1 + 46, COMP, 1); line(X1, Y1 + 80, X2, Y1 + 80, COMP, 1)
line(X1 + 290, Y1 + 46, X1 + 290, Y2, COMP, 1); line(X1 + 430, Y1 + 46, X1 + 430, Y2, COMP, 1)
t(X1 + 10, Y1 + 18, "Titel", 9, COMP); t(X1 + 10, Y1 + 38, "Liquid Harvest – Verdrahtung", 18, "#000", weight="bold")
t(X1 + 10, Y1 + 58, "Datei", 9, COMP); t(X1 + 10, Y1 + 74, "docs/images/wiring.svg", 11, "#000")
t(X1 + 300, Y1 + 58, "Rev.", 9, COMP); t(X1 + 300, Y1 + 74, "1.6.0", 11, "#000")
t(X1 + 440, Y1 + 58, "Blatt", 9, COMP); t(X1 + 440, Y1 + 74, "1 / 1", 11, "#000")
t(X1 + 10, Y1 + 92, "Projekt", 9, COMP); t(X1 + 10, Y1 + 108, "github.com/mariofritzer/Liquid-Harvest", 11, "#000")
t(X1 + 300, Y1 + 92, "Datum", 9, COMP); t(X1 + 300, Y1 + 108, "2026-10-20", 11, "#000")
t(X1 + 440, Y1 + 92, "Format", 9, COMP); t(X1 + 440, Y1 + 108, "A3", 11, "#000")

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{FONT}">'
       + "".join(o) + "</svg>")
open(sys.argv[1], "w", encoding="utf-8").write(svg)
