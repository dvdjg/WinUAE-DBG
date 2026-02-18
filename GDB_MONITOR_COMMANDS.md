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

### reset

Restore the savestate at process entry. Requires `debugging_trigger` to be set in the config.

- **Example**: `monitor reset`

### profile \<num_frames\> \<unwind_file\> \<out_file\>

CPU profiling (advanced). Runs for N frames, uses unwind info, writes profile output.

## Usage from GDB

```gdb
(gdb) monitor screenshot C:/temp/screen.png
(gdb) monitor disasm 0x40000 30
(gdb) monitor input key 0x44 1
(gdb) monitor input key 0x44 0
```

## Usage from MCP (mcp-winuae-emu)

The MCP server calls these via `winuae_screenshot`, `winuae_disassemble_full`, `winuae_input_key`, `winuae_input_event`, `winuae_input_joy`, and `winuae_input_mouse`.

## Audio, Disk, and Future Extensions

- **Audio capture**: Not implemented yet. Paula audio ($DFF0A0–$DFF0D8) can be read via memory access for inspection.
- **Disk sectors**: Direct disk sector access is not exposed in monitor commands. Use memory-mapped floppy access or filesystem tools instead.
- **Bitmap extraction**: Screenshot command captures the rendered display. Raw chip RAM bitmap decoding would require reading BPLCON0, BPL1PTH, etc. and decoding the bitplane format; not implemented as a monitor command.
