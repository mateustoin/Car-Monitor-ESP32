#!/usr/bin/env python3
"""Gera sample.tsv: viagem sintética de ~30 s (2a -> 3a -> 4a) para o replay.
Uso: python3 gen_sample.py > sample.tsv"""
RATIOS = {2: 13.6, 3: 20.0, 4: 27.1}   # km/h por 1000 RPM (perfil sandero_k7m)
SUPPORTED = (0x04, 0x05, 0x0C, 0x0D, 0x11)
mask = 0
for pid in SUPPORTED:
    mask |= 1 << (32 - pid)

def rec(t, cmd, resp):
    print(f"{t}\t{cmd}\t{resp}")

t = 0
for cmd, resp in (("ATZ", "ELM327 v1.5"), ("ATE0", "OK"), ("ATL0", "OK"),
                  ("ATS0", "OK"), ("ATH0", "OK"), ("ATSP0", "OK"),
                  ("0100", f"4100{mask:08X}"), ("ATDPN", "A6")):
    rec(t, cmd, resp)
    t += 50

def emit(t, rpm, speed, load):
    r = int(round(rpm * 4))
    rec(t, "010C", f"410C{r:04X}")
    rec(t + 40, "010D", f"410D{int(round(speed)):02X}")
    rec(t + 80, "0104", f"4104{int(round(load * 255 / 100)):02X}")
    rec(t + 120, "0111", f"4111{int(round(15 * 255 / 100)):02X}")

STEP = 400
t = 1000
n = 0
for gear in (2, 3, 4):
    ratio = RATIOS[gear]
    start = 1800 if gear == 2 else (RATIOS[gear - 1] * 2400 / ratio)
    steps = 25
    for i in range(steps + 1):
        rpm = start + (2400 - start) * i / steps
        emit(t, rpm, ratio * rpm / 1000, 15)
        if n % 5 == 4:
            rec(t + 160, "0105", "4105" + f"{85 + 40:02X}")
        n += 1
        t += STEP
    if gear < 4:   # embreagem: RPM cai sem mudar a velocidade
        emit(t, 1100, ratio * 2400 / 1000, 5)
        t += STEP
