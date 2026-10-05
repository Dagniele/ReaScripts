#!/usr/bin/env python3
"""Turn the built tab page into a C++ byte array for the extension."""

import pathlib
import sys


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: embed_ui.py index.html ui_embed.cpp")
    html = pathlib.Path(sys.argv[1]).read_bytes()
    if not html:
        raise SystemExit(f"{sys.argv[1]} is empty")
    lines = [
        "#include <cstddef>",
        "namespace guitartabs {",
        "extern const unsigned char kEmbeddedUi[] = {",
    ]
    for offset in range(0, len(html), 24):
        chunk = html[offset : offset + 24]
        lines.append("  " + ",".join(str(byte) for byte in chunk) + ",")
    lines.append("};")
    lines.append(f"extern const std::size_t kEmbeddedUiSize = {len(html)};")
    lines.append("}")
    lines.append("")
    pathlib.Path(sys.argv[2]).write_text("\n".join(lines))


if __name__ == "__main__":
    main()
