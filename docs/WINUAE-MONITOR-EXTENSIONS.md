# WinUAE-DBG v2.1 — Extensiones de monitor: status / watch / protect / rewind

WinUAE-DBG amplía el servidor GDB con comandos `monitor` (paquete `qRcmd`)
inspirados en [engine9000](https://github.com/alpine9000/engine9000-public).
Reutilizan la infraestructura de `memwatch` de WinUAE (que ya etiqueta el
origen del acceso: CPU, blitter, copper, DMA) para ofrecer watchpoints con
predicados y protect/cheat, además de telemetría y rewind.

Ruta de implementación: `od-win32/barto_gdbserver.cpp`
(`monitor_status_command`, `monitor_watch_command`, `monitor_protect_command`,
`monitor_rewind_command` y `gdb_watch_hit`).

## Índice de comandos

| Comando | Descripción |
|---|---|
| `monitor status` | Telemetría del emulador (ciclos, frame, vpos/hpos, warp, contadores) |
| `monitor watch` | Watchpoints con predicados y filtro por origen (e9k-style) |
| `monitor protect` | Protect/cheat: bloquear escrituras o forzar valores |
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
