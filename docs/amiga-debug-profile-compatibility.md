# Amiga Debug profiler compatibility (WinUAE-DBG ↔ vscode-amiga-debug)

**Audience:** discussion with [@BartmanAbyss](https://github.com/BartmanAbyss) (vscode-amiga-debug / reference UAE profiler).  
**Context:** [WinUAE-DBG](https://github.com/dvdjg/WinUAE-DBG) — WinUAE upstream + Barto GDB server / `monitor profile` integration.  
**Date:** May 2026

---

## Summary

The **vscode-amiga-debug** extension works out of the box with **your reference UAE** because emulator and extension share a fixed binary contract for `.amigaprofile` files.

**WinUAE-DBG** is not that reference UAE: it is current WinUAE with the GDB/profiler grafted on. WinUAE’s DMA recording subsystem evolved (cyclic buffer, larger `struct dma_rec`). Without restoring the export contract on the emulator side, profiles look corrupted (Blitter stripes, wrong Copper/Screen data). Some **Screen** tab issues also come from extension client behaviour that became visible once DMA data was fixed.

This document separates **required emulator fixes** from **optional extension fork changes**, so we can discuss upstream vs fork maintenance.

---

## What works with the original Bartman stack

| Component | Role |
|-----------|------|
| Reference UAE (shipped with amiga-debug toolchain) | Writes `.amigaprofile` when CPU profiling stops |
| vscode-amiga-debug | Parses profile; drives Blitter, Copper, Screen (Denise replay), flame graph, etc. |

The extension does **not** target arbitrary WinUAE builds. It assumes a rigid format, including (see `src/backend/profile.ts`):

- DMA grid: **227 × 313** cells (`NR_DMA_REC_HPOS` × `NR_DMA_REC_VPOS` in `src/client/dma.ts`)
- **58 bytes** per DMA record (legacy layout; `ProfileFile.sizeofDmaRec = 58`)
- Fixed-size custom register block, optional AGA color table (256×32-bit), GFX resources, embedded screenshot (JPEG), CPU profile samples

Your reference UAE already exports that grid correctly and packs each internal `dma_rec` into the 58-byte layout the extension parses. **Emulator and extension are one designed pair** — that is why “everything worked” with the original stack.

---

## What WinUAE-DBG is

WinUAE-DBG = **WinUAE (upstream, evolving)** + **Barto patches** (GDB server, `monitor profile`, debug resources, etc.).

The mismatch is not “the extension is wrong for WinUAE in general” — it is that **WinUAE’s internal DMA model diverged** from what the profile writer assumed when it was written against your UAE fork.

---

## Root cause 1: DMA is no longer a flat 2D array

### Internal WinUAE model (`debug.cpp`)

- **`dma_record_data`**: cyclic ring buffer (`NR_DMA_REC_MAX` ≈ 1000×300 entries). `get_dma_records()` returns this pointer.
- **`dma_record_lines[y]`**: per-scanline entry into the ring; this is the correct source for a **scanline × horizontal-position** view.

Recording advances `dma_record_cycle` and wraps; data is **not** laid out as `out[y * width + x]` in `dma_record_data`.

### What breaks the extension

The extension always indexes DMA as:

```ts
dmaRecords[y * NR_DMA_REC_HPOS + x]   // 227 × 313
```

If the profile file is filled by:

- dumping the cyclic buffer with wrong stride (e.g. treating it as `y * 256 + x`), or  
- `fwrite` of `sizeof(struct dma_rec)` rows without legacy packing,

then the file has the **expected byte count** but **wrong semantics** → horizontal bands in Blitter view, garbage Copper/Screen replay, etc.

### Fix in WinUAE-DBG (emulator — required)

1. **`export_dma_records_profile(out, 227, 313)`** (`debug.cpp`)  
   Walks `dma_record_lines[y]` for each scanline and fills `out[y * out_w + x]`.

2. **`pack_dma_rec_for_profile()`** (`od-win32/barto_gdbserver.cpp`)  
   Maps modern `struct dma_rec` → **`profile_dma_rec_barto58`** (58 bytes, `static_assert`).

3. Profile stop path writes `dmarec_size = 58`, `dmarec_count = 227*313`, then packed rows — not raw `sizeof(dma_rec)`.

Relevant symbols:

- `include/debug.h`: `export_dma_records_profile()`, `get_dma_records()` comment  
- `od-win32/barto_gdbserver.cpp`: profiler stop, screenshot via `screenshot_prepare(monid, drawbuffer)`

**This restores your original contract; it is not an extension-side workaround.**

---

## Root cause 2: `struct dma_rec` grew

WinUAE-DBG `struct dma_rec` (see `include/debug.h`) includes 64-bit `dat`, `agnus_evt`, Denise events, etc. — much larger than 58 bytes.

The extension reader (`profile.ts`) expects fixed offsets, e.g.:

- `reg` @ 0, `dat` @ 2, `evt` @ 16, `evt2` @ 20, …, `end` @ 57  

Writing the full C struct changes size and field order → silent mis-parse.

**Fix:** explicit `pack_dma_rec_for_profile()`; never `fwrite(sizeof(dma_rec), …)` into `.amigaprofile`.

---

## Profile screenshot vs Screen tab

| Feature | Data source |
|---------|-------------|
| Timeline thumbnail in profile UI | Embedded JPEG from profiler (`screenshot_prepare` on Amiga drawbuffer) |
| **Screen** tab (Graphics Debugger) | **JavaScript Denise replay** from DMA + chipmem + custom regs — **not** the JPEG |

WinUAE-DBG screenshot fixes (24/32 bpp, drawbuffer path) affect the **thumbnail** only. Screen quality depends almost entirely on **correct DMA + memory** in the profile.

---

## Extension fork changes (dvdjg — optional / UX)

Fork branch: `winuae-dbg-screen-profile` on [dvdjg/vscode-amiga-debug](https://github.com/dvdjg/vscode-amiga-debug).

These are **not** required to fix Blitter DMA corruption (that is emulator export). They address Screen tab behaviour:

| Change | File | Issue |
|--------|------|--------|
| Seed palette before DMA replay | `src/client/screen.ts` — `initDenisePalette()` | Original code: `colors = new Uint32Array(256)` never initialized from `agaColors` / prior DMA → grayscale or blank Screen on some AGA profiles |
| `useMemo` depends on `time` | `src/client/debugger/screen.tsx` | In **Live** mode, scrubbing timeline did not redraw (dependency was `state.freeze !== -1 ? time : 0`) |
| Partial DMA replay in Live | `src/client/screen.ts` | CRT-style sweep while scrubbing (replay up to `CpuCyclesToDmaCycles(time)+1`) |

Upstream vscode-amiga-debug may already behave acceptably with **your** UAE + profiles; these issues showed up prominently with WinUAE-DBG once DMA export was fixed.

Temporary workaround before the fork: `WinUAE-DBG/scripts/patch-amiga-debug-screen-view.js` (patches minified `dist/client.js`).

---

## Suggested verification (for Bartman or us)

1. Capture `.amigaprofile` with **reference UAE** → open in **stock** vscode-amiga-debug 1.8.x: Blitter + Screen should be fine (modulo known Live/timeline quirks).
2. Same game/binary, profile with **WinUAE-DBG without** `export_dma_records_profile`: Blitter stripes / corrupt columns typically return.
3. Same with **WinUAE-DBG with** export + pack58: Blitter/Copper should match reference; Screen may still need fork/patch for palette/Live sweep.

---

## Architecture (ASCII)

```
Bartman stack (designed pair):
  [Reference UAE] --227×313×58B DMA--> [vscode-amiga-debug]  OK

WinUAE-DBG before fix:
  [WinUAE cyclic dma_record_data] --wrong export--> [extension]  CORRUPT

WinUAE-DBG after fix:
  [export_dma_records_profile + pack58] --contract--> [extension]  OK (Blitter)
  [optional fork: Screen palette / Live]            --> better Screen UX
```

---

## Questions for upstream discussion

1. **Profile export API:** Would you prefer a shared helper in reference UAE / docs for “how to fill 227×313 from `dma_record_lines`” so WinUAE ports do not regress?
2. **Screen tab:** Are you open to PRs for `initDenisePalette()` and `useMemo(..., [..., time, ...])` in vscode-amiga-debug, or do you consider Live replay out of scope?
3. **DMA record layout:** Is the 58-byte layout still the long-term format, or planned migration to a versioned chunk?

---

## References (WinUAE-DBG tree)

| Topic | Location |
|-------|----------|
| Export grid from scanlines | `debug.cpp` — `export_dma_records_profile()` |
| Cyclic buffer vs lines | `debug.cpp` — `dma_record_data`, `dma_record_lines`, `get_dma_records()` |
| Profile write on stop | `od-win32/barto_gdbserver.cpp` — `pack_dma_rec_for_profile`, profiler stop |
| 58-byte layout | `od-win32/barto_gdbserver.cpp` — `profile_dma_rec_barto58` |
| Extension parser | vscode-amiga-debug `src/backend/profile.ts`, `src/client/dma.ts` |
| Fork Screen patches | dvdjg/vscode-amiga-debug branch `winuae-dbg-screen-profile`, `WINUAE-DBG.md` |

---

*Maintained by WinUAE-DBG contributors for coordination with vscode-amiga-debug upstream.*
