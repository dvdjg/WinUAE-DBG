# WinUAE-DBG v2.1 — Extensiones de monitor: status / watch / protect / rewind

WinUAE-DBG amplía el servidor GDB con comandos `monitor` (paquete `qRcmd`)
inspirados en [engine9000](https://github.com/alpine9000/engine9000-public).
Reutilizan la infraestructura de `memwatch` de WinUAE (que ya etiqueta el
origen del acceso: CPU, blitter, copper, DMA) para ofrecer watchpoints con
predicados y protect/cheat, además de telemetría y rewind.

Ruta de implementación: `od-win32/barto_gdbserver.cpp`
(`monitor_status_command`, `monitor_watch_command`, `monitor_protect_command`,
`monitor_rewind_command` y `gdb_watch_hit`).

## Guía rápida de cuándo usar

| Necesidad | Comando / tool |
|---|---|
| ¿Corre? frame, warp, contadores | `monitor status` / `winuae_emulator_status` |
| ¿Quién toca X? (CPU/copper/blitter/DMA) | `monitor watch <addr> w src=<origen>` + `watch last` / `winuae_watchpoint_set_ext` |
| Congelar o forzar memoria | `monitor protect <addr> block\|set=…` / `winuae_protect` |
| Registrar eventos host | `monitor trace on` → `%TEMP%\winuae-gdb.log` / `winuae_trace` |
| Inspeccionar un estado pasado | `monitor rewind` + canal lateral / `winuae_rewind` + `winuae_side_read` |
| Telemetría del propio programa (trazas in-Amiga) | periférico `0xB70000` / `winuae_debugperiph` |
| Perfil de ciclos por segmento | checkpoints (`0xB70020`) + `monitor debugperiph checkpoints` |

Guía "cuándo usar" completa para la IA:
`Amiga-Cpp/docs/debugging/DEBUG-WINUAE-V2-GUIDE.md`.

## Índice de comandos

| Comando | Descripción |
|---|---|
| `monitor status` | Telemetría del emulador (ciclos, frame, vpos/hpos, warp, contadores) |
| `monitor watch` | Watchpoints con predicados y filtro por origen (e9k-style) |
| `monitor protect` | Protect/cheat: bloquear escrituras o forzar valores |
| `monitor train` | Romper cuando una escritura cambia un valor `from→to` en cualquier dirección + ignore list (e9k-style) |
| `monitor rewind` | Rebobinar un frame (usa el rewind nativo de WinUAE) |

---

## `monitor status`

Devuelve telemetría del emulador en texto plano (una clave por línea):

```
cycles=0xe790a70
frame=1709
vpos=0
hpos=16
warp=0
cpu_cycle_exact=1
blitter_cycle_exact=1
baseText=0x0
sizeText=0x0
breakpoints=0
watchpoints=0
protects=0
rewind=on
rewind_buffersize_mb=…
```

- `cycles`: contador de ciclos de CPU (`get_cycles() / cpucycleunit`).
- `frame`: número de frame (`vsync_counter`).
- `vpos`/`hpos`: posición del haz de CRT.
- `warp`: 1 si la emulación está en modo warp (`turbo_emulation`).
- `baseText`/`sizeText`: base del código ELF cargado (para relocalización).
- `breakpoints`/`watchpoints`/`protects`: contadores activos.
- `rewind`/`rewind_buffersize_mb`: estado del sistema de rewind.

Útil para que una IA verifique rápidamente que el emulador corre, en qué
frame está, y cuántos watchpoints/protects hay configurados.

---

## `monitor watch`

Watchpoints de alto nivel con predicados y filtro por origen de acceso.
El motor es el `memwatch` de WinUAE, que ya distingue el origen:
CPU (`MW_MASK_CPU_*`), blitter (`MW_MASK_BLITTER_*`), copper (`MW_MASK_COPPER`),
DMA (bitplanes/sprites/audio/disco).

### Sintaxis

```
monitor watch list
monitor watch clear
monitor watch del <idx>
monitor watch last
monitor watch <addr> [r|w|rw] [size=8|16|32] [mask=0x…] [val=0x…|value=0x…]
                     [old=|diff=|neq=] [src=cpu|cpud|cpudw|cpudr|copper|blitter|dma|all]
                     [reg=0x…] [pc=0x…] [nobreak] [reportonly]
```

### Opciones

| Opción | Efecto |
|---|---|
| `r` / `w` / `rw` | Acceso que dispara (read/write/read+write). Default `rw`. |
| `size=8/16/32` | Tamaño del watchpoint en bits. Default `32`. |
| `mask=0x…` | Máscara de comparación de valor (`val_mask`). |
| `val=0x…` / `value=0x…` | Sólo dispara si el valor coincide (`val_enabled`). |
| `old=` / `diff=` / `neq=` | Sólo dispara si el valor cambia (`mustchange`). |
| `src=…` | Filtro por origen del acceso. Default `all`. |
| `reg=0x…` | Sólo dispara si el número de registro DMA coincide. |
| `pc=0x…` | Sólo dispara si el PC coincide (por defecto no se filtra). |
| `nobreak` | No detener la emulación (sólo registrar). |
| `reportonly` | Igual que `nobreak` (sin romper). |

Valores de `src=`:

| src | access_mask |
|---|---|
| `cpu` | instrucción + datos CPU (lectura y escritura) |
| `cpud` | datos CPU (lectura + escritura) |
| `cpudw` | sólo escrituras de datos CPU |
| `cpudr` | sólo lecturas de datos CPU |
| `copper` | sólo accesos del Copper |
| `blitter` | sólo accesos del Blitter (A/B/C/D) |
| `dma` | todo excepto CPU (blitter, copper, bitplanes, sprites, audio, disco) |
| `bpl` | todos los bitplanes |
| `spr` | todos los sprites |
| `audio` | los 4 canales de audio |
| `disk` | sólo el DMA de disco |
| `bpl0`…`bpl7` | bitplane individual |
| `spr0`…`spr7` | sprite individual |
| `audio0`…`audio3` | canal de audio individual |
| `all` | cualquier acceso |

### `monitor watch last`

Devuelve el detalle del último watchpoint que detuvo la emulación
(dirección, r/w, tamaño, origen, valor y PC). El stop reply de GDB sigue
siendo el estándar `T05watch:<addr>;`; el detalle extra viaja por aquí para
no romper el parseo de GDB.

```
addr=0x00050000 rwi=w size=2 src=cpu val=0xffffbeef pc=0x00040000
```

### Ejemplos

```
monitor watch 0xdff180 w size=16 src=copper        # Copper escribe un registro custom
monitor watch 0x20000 w size=32 src=blitter val=0x0  # Blitter escribe 0 en un buffer
monitor watch 0x40000 rw size=16 src=cpud          # CPU accede a RAM
monitor watch last
monitor watch clear
```

---

## `monitor protect`

Cheat/protect de memoria. Usa el campo `frozen` de `memwatch`, por lo que
**las escrituras del propio depurador (GDB `M`) NO se ven afectadas cuando
el emulador está pausado**; el protect actúa sobre los accesos del programa
emulado (CPU, blitter, copper, DMA) mientras corre, que es el caso de uso real.

### Sintaxis

```
monitor protect list
monitor protect clear
monitor protect del <addr> [size=8|16|32]
monitor protect <addr> block [size=8|16|32] [src=…]
monitor protect <addr> set=0x… [size=8|16|32] [src=…]
```

- `block`: impide escrituras a `<addr>` (la escritura se descarta).
- `set=0x…`: fuerza el valor en `<addr>` (cada escritura se sustituye por ese valor).

`src=` acepta los mismos valores que en `monitor watch` (default `all`).
Por defecto el protect se aplica a escrituras (`rwi=w`).

### Ejemplos

```
monitor protect 0x20000 block size=32          # congela un buffer contra la CPU
monitor protect 0xdff096 set=0x0000 size=16    # fuerza DMACON a 0
monitor protect 0x40000 set=0x12345678 size=32 src=cpudw
monitor protect list
monitor protect clear
```

### Notas sobre escrituras de 32 bits

El 68000 en modo cycle-exact escribe un `long` como **dos escrituras de
16 bits** (`mem_access_delay_word_write`). El `frozen`/force de memwatch
está pensado para el tamaño completo del watchpoint; con escrituras
descompuestas la fuerza puede aplicarse sólo a la palabra interceptada.
Para resultados predecibles, usar `size` igual al tamaño real de escritura
del programa (normalmente `size=16` para `MOVE.W`/registros custom y
`size=32` cuando el programa escribe longs como un todo).

---

## `monitor train`

Estilo e9k: romper cuando una **escritura** cambia un valor de `from` a `to` en
**cualquier dirección**. Útil para "entrenar" un cheat (p. ej. encontrar dónde
cambia vidas de 3→2) sin saber la dirección.

```
monitor train <from> <to> [size=8|16|32]   # instala el watch any-address
monitor train ignore                        # añade la última dirección disparada a la ignore list
monitor train clear                         # vacía la ignore list
monitor train                               # muestra la ignore list
```

- Instala un watchpoint con `any_addr` (casa cualquier dirección), `old=<from>`
  y `val=<to>`: solo rompe si el valor anterior era `from` y el nuevo es `to`.
- `train ignore` anade la dirección del último hit a una ignore-list; no vuelve
  a romper ahí (se puede repetir para recorrer varias direcciones).
- `train clear` vacía la ignore-list.

Nota: para casar "cualquier dirección" el memwatch remapea el espacio completo
de bancos RAM (límite `MEMWATCH_STORE_SLOTS`); solo se cubren los bancos que
caben en los slots de memwatch.

Verificación: `mcp-winuae-emu/scripts/verify-train.mjs` (5/5).

---

## `monitor rewind`

Rebobina un frame usando el rewind nativo de WinUAE (buffer de estados).

```
monitor rewind            # rebobinar un frame
monitor rewind start      # activar la captura de estados (grabación de input en memoria)
monitor rewind stop       # desactivar la captura
monitor rewind status     # estado: input_record, statecapturerate, buffersize_mb
```

**Requisito**: el rewind de WinUAE captura estados como parte de la **grabación
de input**. `monitor rewind start` la activa en memoria (mismo camino que el
botón "Record" de la GUI); sin ella, `monitor rewind` responde
`E01 rewind capture not active`.

Respuestas típicas:

- `OK rewind scheduled (takes effect on next continue/frame)`: el estado
  `STATE_DOREWIND` se procesa en el siguiente `savestate_check`.
- `E01 no rewind state available`: aún no hay estados capturados (espera al
  menos `statecapturerate` hsyncs tras `rewind start`).
- `E01 rewind disabled (statecapturerate=0)`: rewind desactivado en config.

### ⚠️ Estado del restore (arreglado, con limitación)

El restore del rewind crasheaba el emulador con GDB conectado. **Corregido**:
la causa eran dos bugs preexistentes del **formato de rewind** (save/restore
asimétricos), ambos arreglados en este fork:

1. **`save_custom_agacolors()`** devolvía `NULL` sin fijar `*len` en configs
   no-AGA, y la captura avanzaba `p` con un `len` obsoleto (hueco en el
   registro). Fix en `savestate_capture` (escribe 256×4 ceros cuando no hay
   colores AGA). (`savestate.cpp`)
2. **`restore_blitter_new()`** leía 3 bytes por iteración del pipe, pero
   `save_blitter_new()` escribe 5 (falta el `cycle_line_pipe` u16) → desfase de
   8 bytes que hacía fallar el magic `0x1235` y corrompía la longitud de
   chipmem (crash en `savestate_rewind`, `savestate.cpp:1512`). Fix en
   `blitter.cpp`.

Tras el fix, el rewind **restaura sin crashear**, pero la emulación queda
**congelada**: el CPU no avanza ciclos después del restore (verificado por el
canal lateral: `state`/`cycles` constantes). El servidor GDB además deja de
serviciar paquetes y **reconectar no lo recupera**.

**El canal lateral (2346) sí permite inspeccionar el snapshot restaurado**:
`state`, `regs` y `mem` siguen funcionando tras el restore. Es el patrón útil
para la IA: *rewind para inspeccionar un estado pasado* (leer registros y
memoria de un momento anterior), aunque la ejecución no continúa desde ahí.
No funciona: *rewind y seguir ejecutando*.

En `mcp-winuae-emu` esto se expone como **`winuae_side_read`**
(`state` / `regs` / `mem <addr> <len>` / `runstatus <addr>`), independiente de
GDB, para leer el snapshot tras un restore.

Uso recomendado:

- `monitor rewind start` (captura) es seguro y verificable, pero la captura
  depende del timing de `hsync_counter % statecapturerate` (250 por defecto):
  puede tardar varios segundos en haber un estado, y en ocasiones ninguno a los
  7s. Si `monitor rewind` responde `E01 no rewind state available`, esperar
  más (o repetir tras correr unos segundos).
- `monitor rewind` (restore): congela la emulación pero deja el snapshot
  legible por canal lateral (`state`/`regs`/`mem`) — útil para análisis.
- Para "volver atrás y continuar": savestate completo a archivo o `monitor
  reset` (con `debugging_trigger`).

---

## Uso desde el MCP

El servidor `mcp-winuae-emu` expone estas funciones como herramientas:
`winuae_status`, `winuae_watchpoint_set_ext`/`winuae_watchpoint_clear_ext`,
`winuae_watchpoint_last`, `winuae_protect_*` y `winuae_rewind`.
Ver `mcp-winuae-emu` (`tools/`).

---

## `monitor debugperiph` — periférico de depuración en memoria

Port de los "Amiga Debug Peripherals" de engine9000. Se mapea un banco de
memoria de 64K en **0xB70000** (región libre en A500; engine9000 usa 0xFC0000,
pero en WinUAE esa zona es ROM) para que el **programa emulado se
auto-instrumente**:

| Dirección | Acceso | Uso |
|---|---|---|
| `0xB70000` | write byte | carácter a la consola de depuración (flushea con `0`, `\n` o `\r`; sale por GDB O + `%TEMP%\winuae-gdb.log` con prefijo `DBGPERIPH:`) |
| `0xB70004` | write long | solicita un breakpoint en esa dirección |
| `0xB70008` | write long | base de sección `.text` |
| `0xB7000C` | write long | base de sección `.data` |
| `0xB70010` | write long | base de sección `.bss` |
| `0xB70020` | write long | slot de checkpoint (0-63); se registra ciclos+frame |
| `0xB7E900..E924` | read long | debug args 0-9 (`monitor debugperiph arg <n> <valor>`) |
| `0xB7E928` | read long | contador de ciclos de CPU (`get_cycles()/cpucycleunit`) |

Las escrituras de longs aceptan byte/word/long (MOVE.L del 68000 cycle-exact se
descompone en dos words; GDB M escribe byte a byte). El banco se mapea
automáticamente al primer `vsync_pre` cuando el gdbserver está activo.

### `monitor debugperiph` — subcomandos

```
monitor debugperiph                    # estado: mapped/base/console/args/checkpoints/counters
monitor debugperiph arg <n> <valor>    # fija el debug arg n (0-9)
monitor debugperiph console            # devuelve el buffer de consola pendiente
monitor debugperiph checkpoints        # vuelca los checkpoints (stats + descripcion)
monitor debugperiph checkpoints reset  # limpia todos los checkpoints/stats
monitor debugperiph counters           # vuelca contadores nombre/valor
monitor debugperiph flush              # flushea la consola
```

`checkpoints` muestra por slot: `cycles`, `frame`, `count` y el **profiler de
segmentos** `seg_avg/seg_min/seg_max` (delta de ciclos desde el checkpoint
anterior, atribuido al slot actual) y `scan_avg/scan_min/scan_max` (scanline
`vpos` en cada write). Es el patrón e9k para medir el coste de un tramo:
`checkpoint(10)` antes, `checkpoint(11)` después → `seg` de slot 11 = coste.

### Mapa de registros completo (base 0xB70000)

| Offset | Acceso | Uso |
|---|---|---|
| `+0x00` | write byte | carácter a consola (flushea con 0/`\n`/`\r`) |
| `+0x04` | write long | breakpoint en esa dirección |
| `+0x08`/`0C`/`10` | write long | bases `.text/.data/.bss` |
| `+0x14`/`18`/`1C` | write long | **commit de sección**: base / type (`0`=text,`1`=data,`2`=bss) / size |
| `+0x20` | write long | slot de checkpoint (0-63); registra ciclos+frame |
| `+0x24` | write long | escribir `0xDEAD` sale del debugger (break en el PC actual) |
| `+0x28` | write long | cualquier write → solicita smoke/profiling (hook; orquesta el host) |
| `+0x100` | write long | **descripción de checkpoint** (`uint32_t[64]`; `description_ptr` en `+0x100 + slot*4`) |
| `+0x200` | write long | **nombre de contador** (`uint32_t[64]`; `name_ptr` en `+0x200 + slot*4`) |
| `+0x300` | write long | **valor de contador** (`uint32_t[64]`; en `+0x300 + slot*4`) |
| `0xB7E900..E924` | read long | debug args 0-9 |
| `0xB7E928` | read long | contador de ciclos de CPU |

Las descripciones y nombres son punteros a strings NUL en la memoria del
programa; `debugperiph checkpoints`/`counters` los resuelven leyendo esa memoria.

### Ejemplo (programa 68k bare-metal)

```
        MOVE.W  #$2700,SR                  ; modo supervisor, IRQ off
        MOVE.B  D0,(0xB70000).L            ; carácter a consola
        MOVE.L  #$20000,(0xB70004).L       ; breakpoint en 0x20000
        MOVE.L  (0xB7E928).L,D0            ; leer ciclos de CPU
        ; commit de sección .text (base 0xC000, type 0, size 0x1000)
        MOVE.L  #$C000,(0xB70014).L
        MOVE.L  #0,(0xB70018).L
        MOVE.L  #$1000,(0xB7001C).L
        ; descripción del checkpoint 2
        MOVE.L  #label,(0xB70108).L        ; +0x100 + 2*4
        MOVE.L  #2,(0xB70020).L            ; dispara el checkpoint
        ; contador "uploads" = 3
        MOVE.L  #name,(0xB70200).L
        MOVE.L  #3,(0xB70300).L
```

### Verificación

`mcp-winuae-emu/scripts/verify-debug-peripheral.mjs` (7/7): mapeo, consola,
contador de ciclos, debug args, checkpoint, breakpoint vía periférico y flujo
end-to-end. `mcp-winuae-emu/scripts/verify-debug-peripheral-ext.mjs` (6/6):
commit de secciones, descripción de checkpoint, contadores, `0xDEAD` y hook de
smoke. `verify-visual-copper.mjs` confirma con ollama local (qwen3-vl) que la
renderización sigue funcionando tras mapear el periférico (pantalla magenta del
cobre).

---

## `monitor trace` — sistema de trazas

El servidor GDB escribe toda su actividad en un **log persistente**
(`%TEMP%\winuae-gdb.log`, abierto automáticamente al arrancar; también se puede
controlar con `monitor logfile`). Además, un sistema de trazas de alto nivel
registra los eventos de watch/protect/rewind:

```
monitor trace on|off|status
```

- **`status`** (default): `trace=on|off logfile=open|closed`.
- Activado **por defecto** al conectar. Cuando está `on`, cada evento se
  registra con prefijo `TRACE`:

```
TRACE watch hit addr=0x00050001 rwi=2 size=1 src=cpu val=0x000000ef pc=0x00fe9c48
TRACE watch set [0] addr=0x00050000 size=2 rwi=2 src=cpu val=- mustchange=0
TRACE protect set [1] addr=0x00050000 mode=block size=2 src=dma
TRACE rewind scheduled frame=2011
```

Útil para tener un registro de "qué pasó" durante una temporada mientras se
valida el emulador (watchpoints que se disparan, protects aplicados, rewinds).
El log captura también cada comando `monitor` recibido, los handshakes GDB y
los avisos de depuración.

---

## Roadmap del port desde engine9000

Estado del trabajo de traer features de
[engine9000](https://github.com/alpine9000/engine9000-public) a WinUAE-DBG.

### Hecho (v2.1, verificado)

- `monitor status` (telemetría: ciclos, frame, vpos/hpos, warp, baseText, nº de bp/wp/protects).
- `monitor watch` (predicados `val/mask/old/diff`, size 8/16/32, `src=` de origen, list/del/clear/last).
- `monitor protect` (`block` / `set=valor`).
- `monitor rewind` (`start`/`stop`/`status`; restore arreglado, congela la emulación — sólo inspección por canal lateral).
- `monitor trace` (eventos de watch/protect/rewind → `%TEMP%\winuae-gdb.log`).
- **Amiga Debug Peripherals** parcial (base `0xB70000`; e9k usa `0xFC0000`):
  consola (`+0x00`), breakpoint (`+0x04`), bases `.text/.data/.bss` (`+0x08/0C/10`),
  checkpoint (`+0x20`), debug args (`0xB7E900..E924`), ciclos (`0xB7E928`).
- MCP tools (`winuae_*`) + canal lateral (2346) + baterías de verificación + docs.
- Uso real del periférico en la demo `101_ehb_tile_scroll_driver`
  (`engine/include/eng/debug/peripheral.hpp`, checkpoints 0/10/11, `TILE_CHANGE`).

### Pendiente (priorizado, para hilos nuevos)

**1. Periférico Amiga completo (1:1 con e9k)** — ✅ **hecho** (v2.2):
- Commitar secciones: `+0x14` base, `+0x18` type (0=text,1=data,2=bss), `+0x1C` size. ✅
- Descripciones de checkpoint: `+0x100` (`uint32_t[64]`, `description_ptr` por slot). ✅
- Contadores con nombre/valor: `+0x200` names (`uint32_t[64]`) y `+0x300` values (`uint32_t[64]`). ✅
- `+0x24` escribir `0xDEAD` → salir del debugger. ✅
- `+0x28` cualquier write → solicitar smoke test / profiling (hook; orquesta el host). ✅
- Verificación: `verify-debug-peripheral-ext.mjs` (6/6) + sin regresión en la batería original (7/7).

**2. Checkpoint profiler de e9k** — ✅ **hecho** (v2.2):
- Stats `seg_avg/seg_min/seg_max` por checkpoint (delta de ciclos desde el
  checkpoint anterior) + `scan_avg/scan_min/scan_max` (scanline `vpos`).
- `debugperiph checkpoints reset` limpia todos los checkpoints/stats.
- Verificación: `verify-debug-peripheral-ext.mjs` T7 (7/7 total).

**3. Timeline/rewind avanzado**: `loop` entre frames, `diff` de memoria entre
dos frames, frame-step/reverse. **BLOQUEADO** por limitaciones del rewind de
WinUAE (verificado 2026-08):
- `savestate_capture` no captura estados en flujo headless: la captura está
  atada a la máquina de input-recording del GUI (`savestate_capture_request`
  solo se llama al transitar a re-record; `inprec_open` no la llama) y
  `savestate_capture` retorna si hay filesystems montados (`nr_units() > 0`).
- El restore congela la emulación (documentado); no hay "continuar" tras rewind.
- Prerequisito para desbloquear: (a) que `monitor rewind start` llame a
  `savestate_capture_request` y fuerce la captura, (b) manejar el freeze del
  restore (o leer memoria de los `staterecord` sin restaurar), (c) tolerar
  filesystems montados. Es una sesión dedicada al subsistema de savestate.

**4. Comandos de consola**:
- `train` (transición de valor + ignore list) — ✅ **hecho** (v2.2):
  `monitor train <from> <to> [size]`, `train ignore`, `train clear`. Añade
  `any_addr` + predicado `old` al núcleo de memwatch. Verificación:
  `verify-train.mjs` (5/5) + sin regresión (baterías 7/7, 7/7, 19/19).
- `print` de expresiones DWARF — pendiente.
- `base` explícito — pendiente.

**5. Automatización**:
- **Sampler profiler de hotspots** — ✅ **hecho** (host-side): `Amiga-Cpp/tools/profile/hotspots.mjs`
  muestrea el PC por canal lateral, resuelve el `.map` y emite el informe de
  hotspots (e9k-style). Ejemplo real en demo 101: `wait_vblank` ~29%,
  `rebuild_copper` ~28%, `memset` ~17%.
- Smoke test (grabar/reproducir escenarios comparando frames+audio) — pendiente.

### Nota operativa

- Usar siempre el build **x86** (`winuae-gdb.exe`); el x64 tiene un bug de boot
  congelado preexistente (ver "Nota sobre el build x64" abajo).

---

## Verificación

`mcp-winuae-emu/scripts/verify-monitor-extensions.mjs` ejecuta una batería
end-to-end: status, watch add/list/del, protect block/set, rewind y un hit de
watchpoint real con el CPU corriendo.

```
node scripts/verify-monitor-extensions.mjs
```

## Nota sobre el build x64 (preexistente)

El build **x64** de WinUAE-DBG tiene un problema **preexistente** (no
causado por estas extensiones): durante el boot con el cliente GDB conectado,
el servidor acepta la conexión TCP pero **no responde al handshake**
(`qSupported` hace timeout) y en ocasiones genera un crash (excepción
`0xC0000005`) en la región JIT (`0x4002xxxx`, rutas de `compemu_support`).

**Investigación realizada**:
- Confirmado preexistente: con `git stash` (sin las extensiones) el x64
  tampoco conecta.
- Desactivar JIT (`jit_enable=no`, `cpu_compatible=true`) **no** arregla el
  handshake, ni con `debugging_trigger`.
- **Diagnóstico (determinante)**: en el build x64 el emulador está **congelado
  desde el boot** — el CPU no avanza ciclos (verificado por canal lateral:
  `state`/`cycles` constante, `debuggerState=inited`), y `vsync_pre()` nunca se
  ejecuta. El proceso consume CPU (bucle en el intérprete sin avanzar ciclos).
  El servidor GDB **no** es el problema: es víctima (nunca recibe servicio
  porque la emulación no llega a vsync). Probado con CPU software (sin
  cycle-exact, sin JIT) y sigue congelado.

**Conclusiones**:
- El bug es de **emulación del build x64** (el intérprete 68000 queda en un
  bucle sin avanzar el contador de ciclos al arrancar), **no** del servidor
  GDB ni del JIT.
- El crash intermitente (`0xC0000005` en `0x4002xxxx`) es consecuencia del
  mismo estado corrupto (PC del 68000 apuntando a la región del buffer JIT).
- **Fix**: requiere adjuntar un debugger (Windbg/CDB) al proceso
  `winuae-gdb-x64.exe` al arrancar y romper para localizar el bucle del
  intérprete. No es corregible sin una sesión de depuración del proceso x64.

**Mientras tanto**: usar el build **x86** (`winuae-gdb.exe`, el que usa por
defecto `mcp-winuae-emu`), que funciona correctamente.
