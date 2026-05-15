#!/usr/bin/env python3
"""
Extrae del .amigaprofile (JSON de Amiga Debug) a una carpeta temporal:
  - screenshot.jpg     miniatura WinUAE (data:image/... en .screenshot o regex)
  - chipmem.bin        snapshot chip
  - bogoMem.bin        si existe
  - bitmaps/*.bin      recursos gfx (planos interleaved en chip)
  - manifest.json      metadatos (gfxResources, tamaños)

Uso:
  python scripts/extract-amigaprofile-images.py <ruta.amigaprofile> [carpeta_salida]

Por defecto la salida es %TEMP%\\amiga-profile-extract-<nombre-base>\\
"""

from __future__ import annotations

import base64
import json
import re
import sys
from pathlib import Path


def b64_field(val: str) -> bytes:
    if not isinstance(val, str) or not val:
        raise ValueError("empty field")
    if val.startswith("data:"):
        return base64.b64decode(val.split(",", 1)[1])
    # Amiga Debug: chipMem/bogoMem suelen ir como base64 crudo (sin data: URL).
    return base64.b64decode(val)


def find_screenshot(frame: dict) -> tuple[str, bytes] | None:
    ss = frame.get("screenshot")
    if isinstance(ss, str) and ss.startswith("data:"):
        return ss, b64_field(ss)
    cap = frame.get("capture")
    if isinstance(cap, str) and cap.startswith("data:"):
        return cap, b64_field(cap)
    return None


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    src = Path(sys.argv[1]).resolve()
    if not src.is_file():
        print(f"No existe: {src}", file=sys.stderr)
        return 2
    if len(sys.argv) >= 3:
        out = Path(sys.argv[2]).resolve()
    else:
        import os

        out = Path(os.environ.get("TEMP", "/tmp")) / f"amiga-profile-extract-{src.stem}"
    out.mkdir(parents=True, exist_ok=True)

    raw_text = src.read_text(encoding="utf-8", errors="replace")
    data = json.loads(raw_text)

    if isinstance(data, list):
        frames = data
        fmt = "IAmigaProfile (array)"
    elif isinstance(data, dict):
        if "frames" in data:
            frames = data["frames"]
            fmt = data.get("$id", "IAmigaProfileSplit")
        else:
            frames = [data]
            fmt = "single object"
    else:
        print("Formato JSON no reconocido", file=sys.stderr)
        return 2

    print(f"Formato: {fmt}")
    print(f"Frames: {len(frames)}")
    print(f"Salida: {out}")

    manifest: dict = {"source": str(src), "format": fmt, "frames": []}

    for fi, frame in enumerate(frames):
        fdir = out / f"frame-{fi:03d}"
        fdir.mkdir(exist_ok=True)
        entry: dict = {"index": fi, "dir": str(fdir)}

        shot = find_screenshot(frame)
        if shot:
            mime, blob = shot
            ext = "jpg" if "jpeg" in mime or "jpg" in mime else "png"
            path = fdir / f"screenshot.{ext}"
            path.write_bytes(blob)
            entry["screenshot"] = str(path)
            print(f"  frame {fi}: screenshot -> {path} ({len(blob)} bytes)")

        if not shot:
            m = re.search(r"data:image/[^;]+;base64,([A-Za-z0-9+/=]+)", raw_text)
            if m and fi == 0:
                blob = base64.b64decode(m.group(1))
                path = fdir / "screenshot-regex.jpg"
                path.write_bytes(blob)
                entry["screenshot"] = str(path)
                print(f"  frame {fi}: screenshot (regex) -> {path}")

        base = frame.get("$base") or {}
        if isinstance(base, dict):
            for mem_key, fname in (("chipMem", "chipmem.bin"), ("bogoMem", "bogomem.bin")):
                if mem_key in base:
                    try:
                        chip = b64_field(base[mem_key])
                        p = fdir / fname
                        p.write_bytes(chip)
                        entry[mem_key] = {"path": str(p), "size": len(chip)}
                        print(f"  frame {fi}: {mem_key} -> {p} ({len(chip)} bytes)")
                    except ValueError:
                        pass

        amiga = frame.get("$amiga") or {}
        if isinstance(amiga, dict):
            entry["$amiga_keys"] = sorted(amiga.keys())
            entry["dmaRecords"] = len(amiga.get("dmaRecords") or [])
            entry["gfxResources"] = amiga.get("gfxResources") or []

            chip_path = fdir / "chipmem.bin"
            if chip_path.is_file():
                chipbytes = chip_path.read_bytes()
                bdir = fdir / "bitmaps"
                bdir.mkdir(exist_ok=True)
                for res in amiga.get("gfxResources") or []:
                    if res.get("type") != 0:
                        continue
                    bm = res.get("bitmap") or {}
                    addr = int(res.get("address", 0))
                    size = int(res.get("size", 0))
                    name = (res.get("name") or "res").replace("/", "_").replace("\\", "_")
                    chunk = chipbytes[addr : addr + size]
                    bp = bdir / f"{name}-0x{addr:x}.bin"
                    bp.write_bytes(chunk)
                    entry.setdefault("extracted_bitmaps", []).append(
                        {
                            "file": str(bp),
                            "name": name,
                            "address": addr,
                            "size": size,
                            "width": bm.get("width"),
                            "height": bm.get("height"),
                            "numPlanes": bm.get("numPlanes"),
                        }
                    )
                n_bm = len(entry.get("extracted_bitmaps") or [])
                if n_bm:
                    print(f"  frame {fi}: {n_bm} bitmap(s) planar en {bdir}")

        manifest["frames"].append(entry)

    (out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"manifest -> {out / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
