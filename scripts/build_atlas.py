#!/usr/bin/env python3
"""Builds the hybrid function atlas: Ghidra pseudocode + eerecomp C++.

Each entry is one function:
  - metadata (address, size, callers, callees)
  - the Ghidra C decompilation (readable)
  - the exact eerecomp C++ excerpt (faithful implementation)

Modules: the main executable and the non-Sony overlays. Sony ranges are read
from disasm/sony_ranges.txt and never enter the atlas.
"""

import argparse
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = ROOT / "games" / "onimusha2"
GENERATED = ROOT / "generated" / "onimusha2"
DISASM = GAME / "disasm"

FUNC = re.compile(r"^([0-9a-f]{8})\s+(\S+)\s+(\S+)")
LABEL = re.compile(r"^L_([0-9A-Fa-f]{8}):")
MAIN_UNIT = re.compile(r"\{\s*0x([0-9A-Fa-f]+)u,\s*(\d+)u,\s*&unit_(\w+),\s*entries_")

MODULES = [
    {
        "name": "SLUS_203.93",
        "kind": "main",
        "base": 0x100000,
        "size": 0x1E1600,
        "out": GAME / "translated" / "SLUS_203.93",
        "funcs": DISASM / "ee" / "SLUS_203.93.funcs.txt",
        "sony": True,
        "main": 0x1496D0,
    },
    {
        "name": "01F64000_856903D1",
        "kind": "overlay",
        "base": 0x01F64000,
        "size": 0x28000,
        "out": GAME / "translated" / "overlays" / "01F64000_856903D1",
        "funcs": DISASM / "overlays" / "01F64000_856903D1.funcs.txt",
        "sony": False,
    },
    {
        "name": "01F90000_86124295",
        "kind": "overlay",
        "base": 0x01F90000,
        "size": 0x4000,
        "out": GAME / "translated" / "overlays" / "01F90000_86124295",
        "funcs": DISASM / "overlays" / "01F90000_86124295.funcs.txt",
        "sony": False,
    },
    {
        "name": "01F90000_D3325583",
        "kind": "overlay",
        "base": 0x01F90000,
        "size": 0x4000,
        "out": GAME / "translated" / "overlays" / "01F90000_D3325583",
        "funcs": DISASM / "overlays" / "01F90000_D3325583.funcs.txt",
        "sony": False,
    },
]


def read_main_units():
    units = []
    for line in (GENERATED / "table.cpp").read_text(encoding="utf-8", errors="replace").splitlines():
        m = MAIN_UNIT.search(line)
        if m:
            units.append({"base": int(m.group(1), 16), "size": int(m.group(2)), "unit": m.group(3)})
    units.sort(key=lambda u: u["base"])
    return units


def read_functions(path):
    functions = set()
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = FUNC.match(line)
        if m:
            functions.add(int(m.group(1), 16))
    return functions


def read_ranges():
    ranges = []
    for line in (DISASM / "sony_ranges.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) >= 2:
            ranges.append((int(parts[0], 16), int(parts[1], 16)))
    return ranges


def label_index(path):
    labels = {}
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for number, line in enumerate(lines):
        m = LABEL.match(line)
        if m:
            labels[int(m.group(1), 16)] = number
    return lines, labels


def main_unit_for(units, address):
    for unit in reversed(units):
        if unit["base"] <= address < unit["base"] + unit["size"]:
            return GENERATED / f"unit_{unit['unit']}.cpp", unit
    return None, None


def overlay_unit_for(module, address):
    index = (address - module["base"]) // 0x4000
    for candidate in (index, index + 1, index - 1):
        if candidate < 0:
            continue
        path = GENERATED / f"m_{module['name']}_unit_{candidate:04d}.cpp"
        if path.exists():
            return path, {"base": module["base"] + candidate * 0x4000, "size": 0x4000}
    return None, None


def excerpt(lines, labels, address, end_address):
    start = labels.get(address)
    if start is None:
        return None, None
    end = len(lines)
    for other, line in labels.items():
        if other >= end_address:
            end = min(end, line)
    return start + 1, lines[start:end]


def fence(text, language):
    return f"````{language}\n{text.rstrip()}\n````\n"


def build(module, only=None, limit=0):
    out = module["out"]
    ghidra_csv = out / "ghidra_functions.csv"
    if not ghidra_csv.exists():
        print(f"{module['name']}: no Ghidra export yet ({ghidra_csv})")
        return 0

    functions = read_functions(module["funcs"])
    ranges = read_ranges() if module["sony"] else []
    main_units = read_main_units() if module["kind"] == "main" else []

    rows = []
    with ghidra_csv.open(encoding="utf-8", errors="replace", newline="") as handle:
        for row in csv.DictReader(handle):
            try:
                address = int(row["address"], 16)
            except (KeyError, ValueError):
                continue
            if address not in functions:
                continue
            if any(lo <= address < hi for lo, hi in ranges):
                continue
            if only and address != only:
                continue
            rows.append((address, row))
    rows.sort(key=lambda item: item[0])
    if limit:
        rows = rows[:limit]

    atlas = out / "atlas"
    atlas.mkdir(parents=True, exist_ok=True)
    for old in atlas.glob("*.md"):
        old.unlink()

    index = []
    cache = {}
    for address, row in rows:
        if module["kind"] == "main":
            path, unit = main_unit_for(main_units, address)
        else:
            path, unit = overlay_unit_for(module, address)
        if path is None or unit is None:
            continue
        key = path
        if key not in cache:
            cache[key] = label_index(path)
        lines, labels = cache[key]
        try:
            end_address = address + int(row.get("size") or 0)
        except ValueError:
            end_address = address + 4
        start_line, body = excerpt(lines, labels, address, end_address)
        source = None
        if body is not None:
            source = "\n".join(body)
            if len(source) > 12000:
                source = source[:12000] + "\n// ... (recortado; ver el archivo completo)"

        ghidra_path = out / row["file"]
        ghidra_code = ghidra_path.read_text(encoding="utf-8", errors="replace") if ghidra_path.exists() else ""
        marker = ghidra_code.find("*/")
        if marker >= 0:
            ghidra_code = ghidra_code[marker + 2 :].lstrip()

        name = row.get("name", "FUN")
        scope = "game" if module["kind"] == "overlay" or address >= module.get("main", 0) else "pre-main"
        title = f"0x{address:08X} — {name}"
        md = [f"# {title}", ""]
        md.append(f"- **Módulo:** `{module['name']}`")
        md.append(f"- **Tamaño:** {row.get('size', '?')} bytes")
        md.append(f"- **Ámbito:** {scope}" + (" (código del juego)" if scope == "game" else " (antes de `main`; puede ser SDK/libc)"))
        md.append("- **Sony:** no (excluido de `sony_ranges.txt`)" if module["sony"] else "- **Sony:** no")
        md.append(f"- **Unidad eerecomp:** `{path.name}`")
        if start_line:
            md.append(f"- **Línea:** {start_line}")
        md.append(f"- **Ghidra:** `{row.get('file', '')}`")
        md.append("")
        md.append("## Ghidra — pseudocódigo C")
        md.append("")
        md.append(fence(ghidra_code, "c"))
        md.append("## eerecomp — C++ fiel")
        md.append("")
        if source:
            md.append(f"Desde `{path.name}:{start_line}`:")
            md.append("")
            md.append(fence(source, "cpp"))
        else:
            md.append("_No se encontró el cuerpo en la unidad generada (función inline o cruzando ventana)._")
            md.append("")
        md.append("## Llamadas")
        md.append("")
        callers = row.get("callers", "").strip()
        callees = row.get("callees", "").strip()
        md.append(f"- **Llamada desde:** {callers if callers else '—'}")
        md.append(f"- **Llama a:** {callees if callees else '—'}")
        md.append("")

        name_file = f"{address:08x}_{re.sub(r'[^A-Za-z0-9_.-]', '_', name)}.md"
        (atlas / name_file).write_text("\n".join(md), encoding="utf-8")
        index.append({
            "address": address,
            "name": name,
            "size": row.get("size", ""),
            "scope": scope,
            "unit": path.name,
            "line": start_line or "",
            "ghidra": row.get("file", ""),
            "file": f"atlas/{name_file}",
        })

    index.sort(key=lambda item: item["address"])
    game_count = sum(1 for item in index if item["scope"] == "game")
    pre_count = len(index) - game_count
    lines = [
        f"# Atlas de funciones — {module['name']}",
        "",
        f"{len(index)} funciones: {game_count} de juego y {pre_count} anteriores a `main`.",
        "Librerías Sony confirmadas: `../../disasm/SONY_LIBS.md`." if module["sony"] else
        "Overlay no Sony: sin banners `PsIIlib*` ni mensajes `sce*`/`libcdvd`/`libmc`.",
        "",
        "| Dirección | Función | Ámbito | Bytes | Unidad eerecomp | Atlas |",
        "|---|---|---|---:|---|---|",
    ]
    for item in index:
        lines.append(
            f"| `0x{item['address']:08X}` | `{item['name']}` | {item['scope']} | {item['size']} | "
            f"`{item['unit']}:{item['line']}` | [ver]({item['file']}) |"
        )
    (out / "ATLAS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    with (out / "atlas.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["address", "name", "size", "scope", "unit", "line", "ghidra", "file"])
        writer.writeheader()
        for item in index:
            row = dict(item)
            row["address"] = f"{item['address']:08x}"
            writer.writerow(row)

    print(f"{module['name']}: {len(index)} functions -> {out / 'ATLAS.md'}")
    return len(index)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--module", help="module name (default: all)")
    parser.add_argument("--only", help="single address to build")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    only = int(args.only, 16) if args.only else None
    total = 0
    for module in MODULES:
        if args.module and module["name"] != args.module:
            continue
        total += build(module, only=only, limit=args.limit)
    print(f"total: {total} functions")


if __name__ == "__main__":
    main()
