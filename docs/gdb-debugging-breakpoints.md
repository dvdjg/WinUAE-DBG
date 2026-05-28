# GDB debugging: breakpoints and step (WinUAE-DBG)

## Symptoms

- **Pause** works: source lines, variables OK.
- **Step** (F10/F11) does nothing or runs forever.
- **Breakpoints** in `main.c` never hit.

## Root causes (fixed in WinUAE-DBG)

### 0. Extension/emulator must be deployed as a pair

WinUAE-DBG and the modified `vscode-amiga-debug` adapter share the same
relocation assumptions (`qOffsets`, deferred process entry, `Z0` relocation).
Do not update only `winuae-gdb.exe` while leaving an old
`dist/extension.js` installed in VS Code/Cursor.

Known-good deployment after rebuilding the adapter:

```powershell
cd C:\Users\David\Documents\Programa\Amiga\vscode-amiga-debug
npm run compile
copy dist\extension.js %USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-1.8.2\dist\extension.js
copy dist\extension.js %USERPROFILE%\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2\dist\extension.js
```

The adapter should **not** force `set breakpoint always-inserted on`; normal
GDB insertion on `continue` is enough and avoids inserting source breakpoints
before the target/process relocation state is stable.

### 1. Step (`vCont;s`)

`deactivate_debugger()` clears `debugging`. The CPU only calls `debug()` when `debugging != 0` (`newcpu.cpp` → `check_debugger`).  
Step set `trace_mode = TRACE_SKIP_INS` but left `debugging = 0`, so trace never ran.

**Fix:** after `vCont;s`, set `debugging = -1` and `set_special(SPCFLAG_BRK)` (same idea as `vCont;c` with breakpoints).

### 2. Breakpoint address relocation (`Z0`)

GDB sends **ELF** addresses. The program runs in **chip RAM** (`baseText` from `qOffsets`).

Problems:

- MCP mode used `baseText=0`, `sizeText=0x7fffffff` → every address looked “already loaded” and was **not** relocated.
- `-Ttext=0` projects: symbols below `0x400` were not in “ELF range” and were used verbatim (wrong PC).

**Fixes:**

- Do not fake loaded range until real `qOffsets`.
- Relocate `adr < 0x400` as `adr + baseText` when using `-Ttext=0`.
- Prefer `-Ttext=0x400` in Makefile (matches `ELF_TEXT_BASE` in gdbserver).

### 3. Build flags (Amiga-C)

`-Ofast -flto -fwhole-program` makes line ↔ address mapping unreliable.

Use:

```bash
make debug
```

or `CFLAGS_OPT=-O0 LTO=0` (see Cursor-Amiga-C `compile (debug)` task).

## Checklist

1. Rebuild **WinUAE-DBG** after gdbserver changes.
2. Rebuild and deploy the modified **vscode-amiga-debug** adapter to both
   Cursor and VS Code if both are installed.
3. Rebuild program: `make debug` or launch config **AROS (debug, breakpoints fiables)**.
4. Start debug session; wait until program is running (GDB `qOffsets` / `debug-ready`).
5. Set breakpoints, then **Continue** (not only Pause).
6. Optional log: `monitor breakpoints` in GDB console — verify PC addresses in chip RAM, not `0x400`…
7. Full local smoke test:

```powershell
cd C:\Users\David\Documents\Programa\Amiga\mcp-winuae-emu
node scripts\test-amiga-c-mi-source-debug.mjs
$env:AMIGA_DEBUG_EXT="$env:USERPROFILE\.vscode\extensions\bartmanabyss.amiga-debug-1.8.2"
node scripts\test-amiga-c-mi-source-debug.mjs
```

## Related

- [amiga-debug-profile-compatibility.md](./amiga-debug-profile-compatibility.md) — profiler / `.amigaprofile` (separate from GDB break/step).
