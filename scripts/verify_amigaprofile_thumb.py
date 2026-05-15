#!/usr/bin/env python3
"""
Extrae la miniatura JPEG embebida en un .amigaprofile (JSON de la extensión)
y aplica heurísticas para detectar fallos típicos de captura (banda negra
arriba por filas sin rellenar, cizalla fuerte por pitch incorrecto).

Uso:
  python scripts/verify_amigaprofile_thumb.py ruta/al/archivo.amigaprofile
  python scripts/verify_amigaprofile_thumb.py ruta/miniatura.jpg   # JPEG suelto

Salida: código 0 si OK, 1 si sospechoso/fallo (requiere PIL/Pillow).
"""

from __future__ import annotations

import base64
import json
import re
import sys
from pathlib import Path


def _luma(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def analyze_image(path: Path) -> tuple[bool, str]:
    try:
        from PIL import Image
    except ImportError:
        return True, "Pillow no instalado; omito análisis (instala: pip install Pillow)"

    im = Image.open(path).convert("RGB")
    w, h = im.size
    if w < 32 or h < 32:
        return True, f"imagen muy pequeña {w}x{h}"

    px = im.load()
    # Franja superior oscura vs centro (banda sin inicializar / offset vertical)
    top_h = max(3, h // 20)
    mid_y0 = h // 2 - top_h // 2
    sum_top = 0.0
    sum_mid = 0.0
    n = 0
    for y in range(top_h):
        for x in range(0, w, max(1, w // 200)):
            sum_top += _luma(px[x, y])
            sum_mid += _luma(px[x, mid_y0 + y])
            n += 1
    avg_top = sum_top / max(1, n)
    avg_mid = sum_mid / max(1, n)

    issues: list[str] = []
    if avg_mid > 80 and avg_top < 25 and (avg_mid - avg_top) > 60:
        issues.append(
            f"banda superior muy oscura (luma media top={avg_top:.1f} vs centro={avg_mid:.1f})"
        )

    # Cizalla: correlación fila a fila del patrón de diferencias horizontales
    # (texto que debería ser horizontal aparece escalonado → muchas filas con
    # gradiente horizontal casi idéntico desplazado).
    mid = h // 2
    row_sig: list[float] = []
    for y in range(mid - 10, mid + 10):
        if y < 1 or y >= h - 1:
            continue
        s = 0.0
        for x in range(2, w - 2, 4):
            gx = abs(_luma(px[x, y]) - _luma(px[x - 1, y]))
            s += gx
        row_sig.append(s / max(1, (w - 4) // 4))

    if len(row_sig) >= 4:
        mean_sig = sum(row_sig) / len(row_sig)
        var_sig = sum((s - mean_sig) ** 2 for s in row_sig) / len(row_sig)
        # Patrones muy regulares de fila a fila con baja varianza + alto contraste
        # suelen aparecer con shear fuerte en UI clara; umbral conservador.
        if mean_sig > 8 and var_sig < 0.15:
            issues.append(
                f"patrón horizontal sospechosamente uniforme entre filas (var={var_sig:.3f}, mean={mean_sig:.1f})"
            )

    if issues:
        return False, "; ".join(issues)
    return True, f"OK ({w}x{h}, luma top={avg_top:.1f}, mid={avg_mid:.1f})"


def extract_jpeg_from_amigaprofile(p: Path) -> Path | None:
    text = p.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"data:image/[^;]+;base64,([A-Za-z0-9+/=]+)", text)
    if not m:
        return None
    raw = base64.b64decode(m.group(1))
    out = p.with_suffix(".thumb_extracted.jpg")
    out.write_bytes(raw)
    return out


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"No existe: {path}", file=sys.stderr)
        return 2

    if path.suffix.lower() in (".jpg", ".jpeg"):
        jpeg = path
    else:
        jpeg = extract_jpeg_from_amigaprofile(path)
        if jpeg is None:
            print("No se encontró data:image/...;base64,... en el JSON", file=sys.stderr)
            return 2
        print(f"JPEG extraído: {jpeg}")

    ok, msg = analyze_image(jpeg)
    print(msg)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
