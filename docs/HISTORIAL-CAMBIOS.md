# Historial de cambios – WinUAE-DBG

Documentación de todo lo que se ha hecho en este proyecto desde que se clonó/ bifurcó desde el upstream, para no olvidar el contexto ni las correcciones aplicadas.

---

## Origen del proyecto

- **Upstream:** [BartmanAbyss/WinUAE](https://github.com/BartmanAbyss/WinUAE) (fork de WinUAE con servidor GDB para depuración Amiga).
- **Extensión VS Code:** [BartmanAbyss/vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug) (release 1.7.9 usa GDB 17 y GCC 14.2).
- Este repositorio (WinUAE-DBG) es un fork/clon donde se han añadido:
  - Comandos monitor GDB extendidos (screenshot, disasm, input, disco, etc.).
  - Integración con [mcp-winuae-emu](https://github.com/axewater/mcp-winuae-emu) (MCP para Cursor/Claude).
  - Corrección del bug de breakpoints que impedía que los breakpoints funcionaran con programas relocalizados por LoadSeg.
  - Script de compilación `build.bat` y documentación de compilación.

---

## Resumen de commits locales (respecto a upstream)

Hay **13 commits** por delante de `upstream/master`. Resumen por temas:

| Commit      | Descripción breve |
|------------|--------------------|
| `57570aea` | **Fix MCP:** desactivar debugger durante emulación y restaurar `exception_debugging` para single-step/continue. |
| `3a0f920d` | Extensiones MCP: manipulación de registros y control de warp mode. |
| `3027b281` | Refactor de `build.bat` (plataforma Win32/x64) y mejoras en el servidor GDB. |
| `125a93c7` | **Añadido `build.bat`** para compilación por línea de comandos y actualización del README. |
| `daeffa38` | Simplificación: eliminación de herramientas proxy. |
| `7790d039` | Comandos monitor: gráficos, audio y depuración de bajo nivel. |
| `1dd84f2e` | Comandos de gestión de imágenes de disco (insertar/extraer) vía GDB. |
| `fa3815b2` | README y configuración MCP para WinUAE-DBG. |
| `7a419c32` | Configuración de VS Code para C/C++. |
| `7a7daf01` | Comandos de joystick y ratón en el servidor GDB. |
| `4643374d` | Comandos monitor GDB para depuración mejorada. |
| `3e719eef` | README y proyectos para compatibilidad con VS 2022. |
| `fa28ff85` | Comandos monitor `screenshot` y `disasm`. |

Más atrás en el historial hay merges de upstream y correcciones de gráficos/sonido (resolución, HAM/sprite, etc.).

---

## Bug crítico: breakpoints no se detenían

### Síntoma

Al depurar con la extensión amiga-debug (F5), el programa no se detenía en los breakpoints puestos en el código C (p. ej. `main.c:15`).

### Causa: código original de BartmanAbyss

En el **upstream** (BartmanAbyss/WinUAE), en `od-win32/barto_gdbserver.cpp`:

1. Se asigna `processname` desde `debugging_trigger` (p. ej. `:a.exe`) al iniciar el GDB server.
2. Cuando se detecta la carga del proceso, en el mismo flujo se ejecutaba:
   ```cpp
   processptr = 0;
   xfree(processname);
   processname = nullptr;
   ```
   Eso **borraba** `processname` antes de usarlo.
3. En `state::connected` se calcula `baseText` (dirección de carga del código) solo si `processname` no es null:
   ```cpp
   if(!baseText && processname) { ... baseText = segList + 4; ... }
   ```
   Como `processname` era null, **nunca se calculaba `baseText`**.
4. Además, el código original comparaba el PC con la dirección del breakpoint **sin relocalizar**:
   ```cpp
   if(bpn.enabled && bpn.type == BREAKPOINT_REG_PC && bpn.value1 == pc)
   ```
   GDB envía direcciones en espacio ELF (p. ej. `0x4C0`). En Amiga, LoadSeg() carga el código en otra dirección (p. ej. `0x1FD28`). Sin `baseText` y sin `loadOffset`, la comparación nunca coincidía.

Conclusión: **el fallo no lo introdujeron nuestras modificaciones; el código original del fork ya tenía este bug.** Nuestra corrección lo arregla.

### Corrección aplicada en este repo

- **No resetear `processname`** en el bloque que se ejecuta al detectar el trigger (donde antes se copiaba erróneamente la lógica de `debug.cpp@process_breakpoint()`).
- **Usar relocalización** en la comprobación de breakpoints:
  ```cpp
  uaecptr loadOffset = (baseText >= 0x400) ? (baseText - 0x400) : 0;
  uaecptr bpAddr = bpn.value1 + loadOffset;
  if(bpn.enabled && bpn.type == BREAKPOINT_REG_PC && bpAddr == pc) { ... }
  ```

Documentación detallada del bug y de la corrección: ver en el proyecto **Cursor-Amiga-C** el archivo `doc/bug-breakpoints-no-funcionan.md`.

### Despliegue de la corrección

1. Recompilar WinUAE-DBG (p. ej. con `build.bat` o desde Visual Studio).
2. Copiar el ejecutable generado a la extensión amiga-debug:
   ```batch
   copy bin\winuae-gdb.exe "%USERPROFILE%\.cursor\extensions\bartmanabyss.amiga-debug-1.7.9\bin\win32\"
   ```
   (Ajustar versión de la extensión si es distinta.)

---

## Logs de diagnóstico en el GDB server

Para depurar problemas de breakpoints o de flujo, se añadieron en `barto_gdbserver.cpp` logs con prefijo `GDBSERVER: DEBUG`:

- Al iniciar con `debugging_trigger`: valor de `processname`.
- Al detectar el trigger y guardar estado: confirmación de que `processname` no es null.
- En `state::connected`: PC actual, `baseText`, `processname`.
- Al recibir `Z0` (set breakpoint): dirección recibida, `baseText`, `loadOffset`.
- Al comprobar breakpoints: para cada BP, `value1`, `loadOffset`, `bpAddr`, PC y si hay match.

Esos mensajes salen por:

- **OutputDebugString** (visible con DebugView o con un depurador de Windows).
- **Canal GDB** como salida tipo `O` (si la extensión/GDB muestran la consola de depuración).

Para compilación con estos logs no hace falta opción especial; están siempre en el código. Si en el futuro se quiere reducir ruido, se puede envolver en `#ifdef BARTO_GDB_DEBUG` o similar.

---

## Script `build.bat`

- **No fue creado en una sesión reciente:** ya existía en el repo (commit "Add build.bat script for command line compilation").
- **Ubicación:** raíz del proyecto, `./build.bat`.
- **Función:** compilar WinUAE-DBG desde línea de comandos sin abrir Visual Studio.
- **Por defecto:** usa **Visual Studio 18 (2026)** en `C:\Program Files\Microsoft Visual Studio\18\Community`.
- **Si usas otra instalación** (p. ej. VS 2022 BuildTools):
  ```batch
  set VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
  build.bat
  ```
  o para x64:
  ```batch
  set VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
  build.bat x64
  ```
- **Requisitos:** NASM en PATH o en `C:\Program Files\NASM`, y las librerías/includes de WinUAE (ver README.md).

---

## Comandos monitor GDB (resumen)

Documentación completa en [GDB_MONITOR_COMMANDS.md](../GDB_MONITOR_COMMANDS.md). Resumen de lo añadido o usado en este fork:

- **screenshot** – captura de pantalla a PNG.
- **disasm** – desensamblado m68k.
- **input key / input event / input joy / input mouse** – teclado, joystick, ratón.
- **reset** – restaurar savestate al inicio del proceso.
- **profile** – perfilado de CPU.
- Gestión de discos (insertar/extraer imágenes) vía comandos monitor.
- Extensiones MCP: registros, warp mode, etc.

---

## MCP (mcp-winuae-emu)

- El MCP se conecta al mismo servidor GDB que usa la extensión amiga-debug (puerto 2345).
- Puede arrancar WinUAE con una config `.uae` o conectarse a una sesión ya iniciada por el usuario (F5 en VS Code).
- Configuración de ejemplo en el proyecto Cursor-Amiga-C: `.cursor/mcp.json` con rutas a WinUAE y al ejecutable del MCP.

---

## Referencias rápidas

| Tema              | Dónde está |
|-------------------|------------|
| Compilación       | [README.md](../README.md), `build.bat` |
| Comandos GDB      | [GDB_MONITOR_COMMANDS.md](../GDB_MONITOR_COMMANDS.md) |
| Bug breakpoints   | Cursor-Amiga-C: `doc/bug-breakpoints-no-funcionan.md` |
| Este historial    | `docs/HISTORIAL-CAMBIOS.md` |

---

*Última actualización: febrero 2025.*
