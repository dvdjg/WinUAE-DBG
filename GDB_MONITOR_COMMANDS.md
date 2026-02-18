# WinUAE GDB Server – Monitor Commands (qRcmd)

WinUAE-DBG and the Bartman GDB fork support extended debugging via the GDB `qRcmd` packet (monitor commands). These allow external tools (e.g. MCP server, GDB scripts) to control the emulator beyond standard GDB RSP.

## Available Commands

### screenshot \<filepath\>

Capture the current emulator display to a PNG file.

- **filepath**: Full host path (e.g. `C:\temp\screenshot.png`)
- **Returns**: Hex-encoded text with dimensions and path on success
- **Example**: `monitor screenshot C:\temp\frame.png`

### disasm \<addr\> [count]

Disassemble m68k instructions at the given address using WinUAE’s built-in disassembler.

- **addr**: Start address (hex, e.g. `0x40000`)
- **count**: Number of instructions (default 10, max 100)
- **Returns**: Hex-encoded disassembly text (address, opcode, mnemonic)
- **Example**: `monitor disasm 0x40000 20`

### input key \<scancode\> \<1|0\>

Simulate Amiga keyboard input by raw scancode.

- **scancode**: Amiga raw scancode 0x00–0x7F (e.g. 0x45 = Return, 0x44 = Space)
- **state**: 1 = key down, 0 = key up
- **Example**: `monitor input key 0x45 1` (press Return), `monitor input key 0x45 0` (release Return)

**Note**: Event ID is computed as `256 + (scancode & 0x7F)`, which matches common default keyboard mappings.

### input event \<event_id\> [state]

Send a raw WinUAE input event.

- **event_id**: WinUAE event ID (from config, e.g. `input.1.keyboard.0.button.N`)
- **state**: 1 = press, 0 = release, 2 = toggle (default 1)
- **Example**: `monitor input event 325 1`

Use `input event` when you need precise control over the event ID for non-default configs.

### input joy \<port\> \<dir|button\> \<1|0\>

Simulate joystick/gamepad input.

- **port**: 0 = joystick port 1, 1 = joystick port 2
- **dir|button**: `left`, `right`, `up`, `down`, `fire`/`b1`, `2nd`/`b2`, `3rd`/`b3`
- **state**: 1 = press, 0 = release
- **Example**: `monitor input joy 0 left 1`, `monitor input joy 0 fire 0`

### input mouse move \<dx\> \<dy\>

Apply relative mouse movement (deltas in pixels).

- **Example**: `monitor input mouse move 50 -20`

### input mouse abs \<x\> \<y\>

Set absolute mouse position.

- **Example**: `monitor input mouse abs 320 200`

### input mouse button \<0|1|2\> \<1|0\>

Simulate mouse button press/release.

- **button**: 0 = left, 1 = right, 2 = middle
- **Example**: `monitor input mouse button 0 1` (press left), `monitor input mouse button 0 0` (release)

### df0 / df1 / df2 / df3 insert \<path\>

Insert a disk image (ADF, ADZ, DMS, IPF, ZIP) into the given drive **without restarting** the emulator. Path is UTF-8; use host path (e.g. `C:\path\to\game.adf`).

- **path**: Host path to an image file (ADF Amiga Disk File, ADZ, DMS, IPF, or ZIP). ADF is the standard Amiga format.
- **Example**: `monitor df0 insert C:\games\game.adf`

### df0 / df1 / df2 / df3 eject

Eject the disk from the given drive **without restarting**.

- **Example**: `monitor df0 eject`

### reset

Restore the savestate at process entry. Requires `debugging_trigger` to be set in the config.

- **Example**: `monitor reset`

### profile \<num_frames\> \<unwind_file\> \<out_file\>

Frame profiler (same data as [vscode-amiga-debug](https://github.com/dvdjg/vscode-amiga-debug)): runs for N frames (1–100), optionally with an unwind table for symbol resolution, and writes a binary file containing:

- CPU samples (function-level)
- DMA records per scanline (CRT / beam position, blitter, bitplanes, sprites, etc.)
- Custom chip registers snapshot
- AGA color registers (if AGA)
- Blitter/bitmap resources
- Screenshot per frame (PNG single frame, JPG multi-frame)

The output file format is compatible with the vscode-amiga-debug Frame Profiler and Graphics Debugger, so you can open it there or parse it for autonomous analysis (e.g. MCP).

## Usage from GDB

```gdb
(gdb) monitor screenshot C:/temp/screen.png
(gdb) monitor disasm 0x40000 30
(gdb) monitor input key 0x44 1
(gdb) monitor input key 0x44 0
```

## Usage from MCP (mcp-winuae-emu)

The MCP server exposes monitor commands and a small set of **core tools**. Graphics/audio extraction and low-level debugging (e.g. [coppenheimer](https://github.com/losso3000/coppenheimer)-style toggles) are done with the same core tools — no redundant proxies:

- **winuae_custom_registers**: Returns all $DFF000–$DFF1FE registers with names. From the output you get BPLCON0, BPL1–6 PTH/PTL (bitplane pointers: 24-bit = high byte of PTH << 16 | PTL), AUD0–3 LCH/LCL/LEN/PER/VOL (Paula), DMACON, DIW/DDF, COLOR00–31, SPR0–7 PTH/PTL, COP1LCH/L. Use this to derive bitmap addresses and dimensions, then use **winuae_memory_read** to dump bitplane data (address from BPL1PTH/L, length = row_bytes × height × num_planes) or sample data (address from AUDxLC, length from AUDxLEN×2).
- **winuae_memory_read**: Read any address/length; use for bitplane dumps, sample dumps, or chunked reads to search for hex patterns in RAM.
- **winuae_memory_write**: Write hex bytes to any address. To toggle hardware: write 2 bytes (big-endian) to $DFF100 (BPLCON0) or $DFF096 (DMACON; bit 9=sprites, bit 8=bitplanes).
- **winuae_memory_dump**: Hex+ASCII dump of a region; use to inspect registers or search visually.
- **winuae_copper_disassemble**: Decode Copper list; use COP1LCH/L from custom_registers as the address.

With these, an AI can extract graphics/sound, search for patterns, and enable/disable bitplanes or sprites without extra MCP tools.

## Audio, Disk, and Future Extensions

- **Audio**: Read Paula via `winuae_custom_registers` (AUD0–3 at $DFF0A0–$DFF0D8), then `winuae_memory_read` at AUDxLC for sample data.
- **Bitmap extraction**: Get BPL pointers and DIW/DDF from `winuae_custom_registers`, then `winuae_memory_read` at BPL1 address with length row_bytes×height×num_planes; decode planar externally. Screenshot command still captures the rendered display.
- **Disk sectors**: Direct disk sector access is not exposed in monitor commands. Use memory-mapped floppy access or filesystem tools instead.
