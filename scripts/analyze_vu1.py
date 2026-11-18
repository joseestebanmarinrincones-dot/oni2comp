#!/usr/bin/env python3
"""Analiza los volcados de microcodigo VU1 de Onimusha 2.

Los .bin de games/onimusha2/vu1 son 16 KB de memoria de codigo VU1. Este
script no ejecuta el microcodigo: extrae las instrucciones que revelan el
layout de datos que espera cada programa.

Instrucciones relevantes (par de 64 bits: lower en +0, upper en +4):

  LQ vf[t], imm(vi[s])   lower opcode 0x00. Si s == 0, imm es una direccion
                         absoluta de memoria de datos VU1 (en qwords).
  IADDIU vi[t], vi[s], n lower opcode 0x08. Construye bases dinamicas.
  XGKICK vi[s]           lower special index 0x6C. Envia un paquete GIF
                         desde la memoria de datos VU1.

Uso:
    python scripts/analyze_vu1.py
    python scripts/analyze_vu1.py --root games/onimusha2/vu1
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def decode_dump(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    lq: list[tuple[int, int, int, int]] = []
    iaddiu: list[tuple[int, int, int, int]] = []
    xgkick: list[tuple[int, int]] = []

    for pc in range(0, len(data) - 7, 8):
        lo, _hi = struct.unpack_from("<II", data, pc)
        opcode = (lo >> 25) & 0x7F

        if opcode == 0x00:  # LQ
            dest = (lo >> 21) & 0xF
            ft = (lo >> 16) & 0x1F
            fs = (lo >> 11) & 0xF
            imm = sign_extend(lo & 0x7FF, 11)
            if dest:
                lq.append((pc, ft, fs, imm))
        elif opcode == 0x08:  # IADDIU
            it = (lo >> 16) & 0xF
            fs = (lo >> 11) & 0xF
            imm = sign_extend(lo & 0x7FFF, 15)
            iaddiu.append((pc, it, fs, imm))
        elif (lo >> 25) == 0x40:
            funct = lo & 0x3F
            if funct >= 0x3C:
                index = ((lo >> 6) & 0x1F) * 4 + (funct & 0x3)
                if index == 0x6C:  # XGKICK
                    fs = (lo >> 11) & 0xF
                    xgkick.append((pc, fs))

    nonzero = [i for i, byte in enumerate(data) if byte]
    return {
        "size": len(data),
        "first": nonzero[0] if nonzero else -1,
        "last": nonzero[-1] if nonzero else -1,
        "lq": lq,
        "iaddiu": iaddiu,
        "xgkick": xgkick,
    }


def matrix_hint(lq: list[tuple[int, int, int, int]]) -> str:
    """Busca 16 qwords consecutivos leidos con LQ desde vi0."""
    absolute = sorted({imm & 0x3FF for _pc, _ft, fs, imm in lq if fs == 0})
    runs: list[tuple[int, int]] = []
    start = previous = None
    for address in absolute:
        if start is None:
            start = previous = address
        elif address == previous + 1:
            previous = address
        else:
            runs.append((start, previous))
            start = previous = address
    if start is not None:
        runs.append((start, previous))

    for begin, end in runs:
        count = end - begin + 1
        if count >= 16:
            return (f"posible bloque de 4 matrices en qword 0x{begin:03X} "
                    f"({count} qwords)")
    for base in (0x000, 0x0D0):
        if base in absolute:
            return (f"constantes en qword 0x{base:03X}; no hay 16 qwords "
                    f"consecutivos en vi0")
    return "sin bloque de matrices evidente"


def main() -> int:
    repo = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=pathlib.Path,
        default=repo / "games" / "onimusha2" / "vu1",
        help="directorio con los .bin de VU1",
    )
    args = parser.parse_args()

    dumps = sorted(args.root.glob("*.bin"))
    if not dumps:
        print(f"No encontre .bin en {args.root}", file=sys.stderr)
        return 1

    for path in dumps:
        info = decode_dump(path)
        print(path.name)
        print(f"  bytes: {info['size']}, primer byte no cero: "
              f"0x{info['first']:04X}, ultimo: 0x{info['last']:04X}")
        print(f"  {matrix_hint(info['lq'])}")
        print("  XGKICK: " + (", ".join(
            f"pc=0x{pc:X} vi{fs}" for pc, fs in info["xgkick"]) or "ninguno"))
        print("  IADDIU: " + ("; ".join(
            f"0x{pc:X}: vi{it}=vi{fs}{imm:+d}"
            for pc, it, fs, imm in info["iaddiu"]) or "ninguno"))
        absolute = sorted(
            {imm & 0x3FF for _pc, _ft, fs, imm in info["lq"] if fs == 0})
        dynamic = [row for row in info["lq"] if row[2] != 0]
        print("  LQ vi0: " + (", ".join(f"0x{a:03X}" for a in absolute) or
                              "ninguno"))
        print(f"  LQ dinamicos: {len(dynamic)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
