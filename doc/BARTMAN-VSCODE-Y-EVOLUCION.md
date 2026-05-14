# BartmanAbyss, vscode-amiga-debug y evolución de WinUAE-DBG

Este documento resume **qué hizo BartmanAbyss** para integrar WinUAE en [vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug), **qué se ha ido añadiendo después** en este fork ([WinUAE-DBG](https://github.com/axewater/WinUAE)), incluye las **mejoras locales en ratón y depuración** documentadas en [DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md), y relaciona el uso con **IA a bajo nivel** mediante [mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu) (cliente RSP sobre el mismo servidor GDB del emulador).

Para detalle técnico de relocalización y cambios en la extensión, ver también [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md), [RELOCATION-FIX.md](RELOCATION-FIX.md) y [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md).

---

## Atribución: Bartman («Barto»), upstream y extensiones encima {#atribucion-bartman-vs-fork}

Esta sección fija **quién implementó qué** respecto al depurador remoto (GDB RSP dentro de WinUAE) y evita confusiones con nombres parecidos.

### Nombres y repositorios

| Nombre / alias | Rol en este contexto |
|----------------|----------------------|
| **BartmanAbyss** | Autor público del ecosistema **vscode-amiga-debug** + fork **WinUAE** con servidor GDB + fork **binutils-gdb** para Amiga; en el código WinUAE aparece como **«Barto»** (`barto_gdbserver.cpp`, `WINUAEEXTRA` «Barto's GDBServer Edition» en `win32.h`). |
| **Bernhard Wodok** / **bwodok** | Commits frecuentes en el mismo árbol de depuración (historial compartido con el fork de Bartman sobre WinUAE). |
| **david.jurado** | Commits de este fork **WinUAE-DBG** encima de Bartman: relocalización diferida, monitor, MCP, ratón, build, etc. (ver [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md) y tabla siguiente). |
| **«Alex»** | **No** figura como autor en `git log` sobre `od-win32/barto_gdbserver.cpp`. Si se refiere al usuario GitHub **axewater** (línea del fork **WinUAE-DBG** en GitHub), es la **organización / upstream del fork** del documento, no una segunda implementación del servidor GDB distinta de Bartman. |

Los comentarios de ejemplo al inicio de `barto_gdbserver.cpp` (rutas con usuario **`Chuck`**, plantillas `amiga-debug`) son **notas de depuración local** del proyecto original de la extensión; no definen la autoría del protocolo.

### Qué añadió Bartman al WinUAE para conectar con GDB (idea general)

Sin pretender listar cada paquete RSP: la **extensión** respecto a un WinUAE stock es que el emulador actúa como **target remoto GDB** (socket TCP, RSP, `qOffsets`, memoria, breakpoints, paso, integración con el bucle de emulación) y expone **comandos monitor** vía `qRcmd` (perfilado, savestates, recursos, logs, etc.). Eso es lo que permite a **vscode-amiga-debug** (y a cualquier cliente RSP, p. ej. MCP) depurar código Amiga.

### Qué extensiones lleva **este** repositorio encima (fork local)

Además de merges de WinUAE upstream y de la línea Bartman, el historial de git bajo `barto_gdbserver.cpp` atribuye a **david.jurado** trabajo como:

- **Relocalización y GDB**: breakpoints diferidos / `relocate_breakpoints`, mejoras en manejo de `qOffsets` y paquetes relacionados (véase [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md), [RELOCATION-FIX.md](RELOCATION-FIX.md)).
- **Monitor**: p. ej. **`memcfg`**, utilidades **findproc / findcli**, logging **`barto_log`**, comandos de audio/gráficos/disks/disasm (según commits listados en el log).
- **MCP**: corrección cuando el depurador queda activo de forma que bloquea la emulación; extensiones orientadas a **registros** y **warp**.
- **Capturas**: mejoras en el flujo de **screenshots** desde el stub.
- **Ratón en host** (parte WinUAE, no solo stub): modo absoluto, `setmousestate`, etc. ([DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md)).

Para una lista commit-a-commit del lado **vscode-amiga-debug** (TypeScript: `mi2.ts`, `symbols.ts`, …), usar el mismo rango de fechas en el clon de la extensión; aquí el foco es el **binario WinUAE** (`winuae-gdb.exe`) y `barto_gdbserver.cpp`.

---

## 1. Qué aportó BartmanAbyss al ecosistema

### 1.1 Extensión Visual Studio Code

La extensión **vscode-amiga-debug** conecta el IDE con un **GDB para m68k** (`m68k-amiga-elf-gdb`) y el **stub remoto** que vive dentro del emulador. El flujo conceptual es:

- **DAP** (Debug Adapter Protocol) en VS Code ↔ **MI** de GDB ↔ **RSP** (Remote Serial Protocol) TCP hacia WinUAE.

La extensión se encarga de lanzar/gestionar GDB, cargar símbolos (p. ej. vía `objdump`), breakpoints, stepping y mapeo fuente–ensamblado.

### 1.2 Fork de WinUAE con servidor GDB embebido

En el fork de Bartman, WinUAE incorpora `od-win32/barto_gdbserver.cpp` (espacio de nombres `barto_gdbserver`), activado con la opción de configuración **`debugging_features`** que incluye el bit **`gdbserver`**. El servidor:

- Abre un **socket TCP** (puerto habitual **2345**) y habla **GDB RSP** con el cliente (GDB oficial o herramientas como MCP).
- Expone **registros m68k** (D0–D7, A0–A7, SR, PC), lectura/escritura de memoria, breakpoints software (`Z0`/`z0`) y el paquete **`qOffsets`**, necesario para comunicar al cliente las **bases de carga** del programa AmigaDOS y alinear direcciones ELF con la memoria real.
- Se integra en el ciclo del emulador (p. ej. **`vsync`** desde `win32.cpp`) para mantener coherencia entre ejecución y estado de depuración.
- Con el tiempo Bartman añadió **perfilado** (frame profiler, capturas), **comandos monitor** vía `qRcmd`, manejo de **savestates/quickrestore** para depuración con disparador por proceso, **registros custom**, mejoras de **ack** en RSP, protección contra **inicializaciones múltiples**, etc. (gran parte visible en el historial de `barto_gdbserver.cpp`).

En resumen: **WinUAE deja de ser solo “ventana + CPU”** y pasa a ser también **target GDB remoto**, que es exactamente lo que vscode-amiga-debug necesita para depurar código Amiga como si fuera un dispositivo real.

---

## 2. Evolución en WinUAE-DBG (este fork)

Los commits recientes del árbol local agrupan varias líneas de trabajo **encima** del núcleo de Bartman:

| Área | Contenido típico |
|------|-------------------|
| **Build y proyecto** | `build.bat`, compatibilidad VS2022/2026, README de compilación. |
| **Relocalización y breakpoints** | `qOffsets`, **`ELF_TEXT_BASE`**, lista de direcciones ELF, **`relocate_breakpoints()`** para cuando los breakpoints se piden antes de conocer `baseText`. Documentado en [RELOCATION-FIX.md](RELOCATION-FIX.md) y [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md). |
| **Monitor / depuración baja** | Comandos extendidos: capturas, **disasm**, audio/gráficos de ayuda, **gestión de discos**, **`memcfg`** (mapa de bancos), utilidades **findproc / findcli**, logging **`barto_log`**. |
| **Integración MCP** | Ajustes para que el uso intensivo del stub por **mcp-winuae-emu** no deje el depurador interno en un estado que impida seguir emulando (p. ej. desactivar depurador preservando `exception_debugging` donde aplica); extensiones para **registros** y **warp**. |
| **Ratón (host)** | Modo **ratón absoluto** (`win32_absolute_mouse`), evitar **doble fuente** (WM_MOUSEMOVE + RawInput/DirectInput), corrección en **`setmousestate`** cuando `isabs=1` (no resetear `old_axis` de forma que convierta mal la posición en delta). |
| **Screenshots en stub** | Mejoras en el manejo de capturas desde `barto_gdbserver` para flujos MCP/monitor. |

La documentación reunida en **`doc/`** sirve para no perder el hilo entre **upstream Toni Wilen**, **capa Bartman** y **cambios del fork** (índice en [README.md](README.md)).

---

## 3. Mejoras locales: ratón y estrategia de depuración

El archivo **[DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md)** describe la metodología para depurar problemas de **entrada (ratón)** y usar flags como **`-winmouselog`**, **`-norawinput_mouse`**, **`-nowindowsmouse`**, y correlación entre coordenadas **cliente** vs **pantalla** (`ClientToScreen`, `amigawinclip_rect`).

Ideas clave que encajan con los cambios en código:

- Varias rutas que llaman **`setmousestate`** para los mismos ejes pueden **sumar deltas** y descontrolar el cursor.
- Con ratón absoluto, hay que **silenciar** RawInput/DirectInput en ejes 0/1 cuando corresponda, y **no romper** el cálculo delta/absoluto en `inputdevice.cpp`.

Para prueba manual/script hay referencia a `scripts\test-mouse-absolute.ps1` y a **[MOUSE-SYSTEMS.md](MOUSE-SYSTEMS.md)**.

---

## 4. IA a bajo nivel: mcp-winuae-emu y este WinUAE

[mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu) **no sustituye** a GDB en el IDE: **reutiliza el mismo servidor RSP** del WinUAE fork. Expone herramientas MCP (`winuae_connect`, lectura/escritura memoria, registros, breakpoints, snapshot de máquina, screenshot, input, etc.) implementadas como cliente del protocolo descrito en `mcp-winuae-emu` (`gdb-protocol.ts`).

Implicaciones:

- **Ventaja**: Una IA puede **pausar**, **inspeccionar memoria/registros/custom**, **inyectar entrada** y **automatizar** flujos sin pasar por la UI de VS Code, siempre que el stub y la configuración (ROM, `.uae`, puerto GDB) estén correctos.
- **Condición**: Este repositorio debe producir un **`winuae-gdb.exe`** coherente con las extensiones del stub (monitor, `memcfg`, fixes MCP).
- **Límites conocidos** (también citados en el README de MCP): CIA por GDB; desensamblado básico vs `monitor disasm`; un solo cliente TCP; escritura memoria según build (`M`/`X`, ack mode); cargas Hunk/fixas en algunos escenarios aún frágiles.

El **`qOffsets`** inicial opcional en el cliente MCP está alineado con la estrategia de **relocalización** descrita en la documentación del fork.

---

## 5. Errores u oportunidades de mejora

Hallazgos útiles para roadmap (sin priorizar aquí trabajo concreto):

1. **Consistencia documental**: Mantener un solo árbol en **`doc/`** y el índice [README.md](README.md) reduce enlaces rotos y duplicados.
2. **Relocalización**: Cualquier cambio en **ELF_TEXT_BASE** (p. ej. toolchain) obliga a revisar breakpoint relocation y regex/`qOffsets` en clientes ([HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md)).
3. **Ratón**: Nuevos modos “exclusivos” deben revisar **todas** las rutas hacia `setmousestate` y el comportamiento **`isabs`** ([DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md)).
4. **MCP + sesiones largas**: El README de MCP recomienda a veces **WinUAE ya lanzado** + `winuae_connect_existing` para máxima fiabilidad entre turnos; cerrar la brecha en **sesión reutilizable** seguiría mejorando la UX para IA.
5. **Postmortem y señales**: El propio MCP documenta mapeos heurísticos de señales en 68000; mejorar mensajes de **razón de parada** en el stub beneficiaría automatización y menos ambigüedad.
6. **Escritura memoria / ack**: Builds sin **`M`** o sensibles al modo no-ack siguen siendo un punto de fricción; documentado en MCP como variable de entorno y alternativas.

---

## 6. Referencias rápidas

| Recurso | Rol |
|---------|-----|
| [BartmanAbyss/vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug) | Extensión VS Code / depuración Amiga |
| [BartmanAbyss/WinUAE](https://github.com/BartmanAbyss/WinUAE) | Fork upstream conceptual (servidor GDB en WinUAE) |
| [dvdjg/mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu) | MCP sobre RSP para agentes |
| `od-win32/barto_gdbserver.cpp` | Implementación del servidor GDB en este árbol |
| [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md) | Diagrama DAP → MI → RSP |
| [DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md) | Ratón, flags, checklist |

---

*Documento orientado a contexto para humanos e IA que trabajen sobre WinUAE-DBG y MCP.*
