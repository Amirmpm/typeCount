#!/usr/bin/env python3
"""Pack all TypeCount UI assets into compiled C++ byte arrays for in-memory WebView2 serving."""
import json
import os
import pathlib

MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".woff2": "font/woff2",
}

EXCLUDE = {"locales.json"}


def main():
    ui_dir = pathlib.Path("ui")
    out_dir = pathlib.Path("src/win")
    out_dir.mkdir(parents=True, exist_ok=True)

    # Always refresh ui/locales.js from ui/locales.json
    with open(ui_dir / "locales.json", "r", encoding="utf-8") as f:
        msgs = json.load(f)
    (ui_dir / "locales.js").write_text(
        "window.clickpulseMessages=" + json.dumps(msgs, ensure_ascii=False, separators=(",", ":")) + ";",
        encoding="utf-8",
    )

    files = []
    for p in sorted(ui_dir.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(ui_dir).as_posix()
        if rel in EXCLUDE:
            continue
        ext = p.suffix.lower()
        mime = MIME_TYPES.get(ext, "application/octet-stream")
        files.append((rel, mime, p.read_bytes()))

    hpp = """#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace tc {

struct UiAsset {
    const uint8_t* data;
    size_t size;
    const char* mime;
};

const UiAsset* find_ui_asset(const std::string& path);

} // namespace tc
"""
    (out_dir / "ui_bundle.hpp").write_text(hpp, encoding="utf-8")

    lines = [
        '#include "ui_bundle.hpp"',
        '#include <unordered_map>',
        "",
        "namespace tc {",
        "namespace {",
    ]

    entries = []
    for idx, (rel, mime, raw) in enumerate(files):
        sym = f"asset_{idx}"
        hex_chunks = []
        for off in range(0, len(raw), 24):
            chunk = raw[off : off + 24]
            hex_chunks.append("    " + ",".join(f"0x{b:02x}" for b in chunk))
        lines.append(f"// {rel} ({len(raw)} bytes)")
        lines.append(f"const uint8_t {sym}[] = {{")
        lines.append(",\n".join(hex_chunks) if hex_chunks else "    0x00")
        lines.append("};")
        entries.append((rel, sym, len(raw), mime))

    lines.append("} // namespace")
    lines.append("")
    lines.append("const UiAsset* find_ui_asset(const std::string& path) {")
    lines.append("    static const std::unordered_map<std::string, UiAsset> map = {")
    for rel, sym, sz, mime in entries:
        lines.append(f'        {{"{rel}", UiAsset{{{sym}, {sz}u, "{mime}"}}}},')
    lines.append("    };")
    lines.append("    std::string key = path;")
    lines.append('    while (!key.empty() && key.front() == \'/\') key.erase(key.begin());')
    lines.append("    auto q = key.find('?'); if (q != std::string::npos) key.resize(q);")
    lines.append('    if (key.empty()) key = "index.html";')
    lines.append("    auto it = map.find(key);")
    lines.append("    return it != map.end() ? &it->second : nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace tc")

    (out_dir / "ui_bundle.cpp").write_text("\n".join(lines), encoding="utf-8")
    total_kb = sum(len(x[2]) for x in files) / 1024.0
    print(f"Packed {len(files)} UI files ({total_kb:.1f} KB) into src/win/ui_bundle.cpp")


if __name__ == "__main__":
    main()
