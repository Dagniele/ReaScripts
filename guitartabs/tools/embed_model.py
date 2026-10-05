#!/usr/bin/env python3
"""Pack the Basic Pitch Core ML package into a C++ byte table."""

import pathlib
import sys


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: embed_model.py nmp.mlpackage model_embed.cpp")
    root = pathlib.Path(sys.argv[1])
    files = [path for path in root.rglob("*") if path.is_file()]
    files.sort()
    if not files:
        raise SystemExit(f"{root} has no files")
    lines = [
        "#include \"model_embed.hpp\"",
        "",
        "namespace guitartabs {",
        "namespace {",
    ]
    symbols = []
    for index, path in enumerate(files):
        data = path.read_bytes()
        symbol = f"kFile{index}"
        symbols.append((path.relative_to(root).as_posix(), symbol, len(data)))
        lines.append(f"const unsigned char {symbol}[] = {{")
        for offset in range(0, len(data), 24):
            chunk = data[offset : offset + 24]
            lines.append("  " + ",".join(str(byte) for byte in chunk) + ",")
        lines.append("};")
    lines.append("}")
    lines.append("extern const ModelFile kBasicPitchFiles[] = {")
    for relative, symbol, size in symbols:
        lines.append(f"  {{\"{relative}\", {symbol}, {size}}},")
    lines.append("};")
    lines.append(f"extern const int kBasicPitchFileCount = {len(symbols)};")
    lines.append("}")
    lines.append("")
    pathlib.Path(sys.argv[2]).write_text("\n".join(lines))


if __name__ == "__main__":
    main()
