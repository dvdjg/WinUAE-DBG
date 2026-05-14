# BartmanAbyss, vscode-amiga-debug y evolución de WinUAE-DBG

Este documento resume **qué hizo BartmanAbyss** para integrar WinUAE en [vscode-amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug), **qué añadió axewater** (fork público + `monitor screenshot` / `monitor disasm` en **`fa28ff85`**), **qué añadió David** (commits **david.jurado**: relocalización, monitor posterior, MCP, ratón, build), y **cómo no romper el protocolo GDB/RSP** — además de la estrategia de ratón en [DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md) y el uso con **IA** vía [mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu).

Para detalle técnico de relocalización y cambios en la extensión, ver también [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md), [RELOCATION-FIX.md](RELOCATION-FIX.md) y [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md).

---

## Atribución: Bartman, axewater y David — y el protocolo GDB {#atribucion-bartman-vs-fork}

Objetivo: **saber qué tocó cada capa** para no mezclar responsabilidades ni **romper la compatibilidad** con clientes GDB (RSP sobre TCP, mismos paquetes que espera el **fork de GDB de Bartman** y herramientas como MCP).

### Vista rápida (tres capas)

| Capa | Persona / identidad | Qué aportó (resumen) |
|------|----------------------|----------------------|
| **1. Base GDB** | **BartmanAbyss** («**Barto**») y colaboradores en su fork (p. ej. **Bernhard Wodok** / **bwodok**) | Servidor RSP en WinUAE (`barto_gdbserver.cpp`), integración con el emulador, `qOffsets`, breakpoints estándar, memoria/registros, perfilado y **comandos monitor** ya previstos por Bartman vía `qRcmd`. Extensión **vscode-amiga-debug** y fork **binutils-gdb** Amiga. |
| **2. Fork público + monitor MCP (primer bloque)** | **axewater** (GitHub); en git ese commit está firmado **acehighness** (`wateraxe@gmail.com`) | Publicación del fork [axewater/WinUAE](https://github.com/axewater/WinUAE) (proyecto **WinUAE-DBG**), enlaces MCP bajo [axewater/mcp-winuae-emu](https://github.com/axewater/mcp-winuae-emu). Código: commit **`fa28ff8536078f1b26b6d390f8e57c02d1c14449`** — comandos **`monitor screenshot`** y **`monitor disasm`**. |
| **3. Evolución posterior en el mismo árbol** | **David** (autor git **david.jurado**) | Relocalización diferida y breakpoints (`relocate_breakpoints`, `Z0`/`qOffsets`), más comandos monitor (**`memcfg`**, findproc/findcli, audio/gráficos/discos, `barto_log`), fixes **MCP**, refinamiento de **screenshots** en el stub, cadena de log *«axewater fork»* (**`85a37798`**), **ratón absoluto** en host, **build** / VS. Ver [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md). |

Los comentarios de ejemplo al inicio de `barto_gdbserver.cpp` (rutas con usuario **`Chuck`**) son del entorno original de **vscode-amiga-debug**; no definen autoría del protocolo.

### 1. BartmanAbyss — lo que no se debe romper al tocar GDB

Es la **base de interoperabilidad** con cualquier cliente GDB estándar (y con el GDB fork de Bartman que usa la extensión):

- **Transporte y RSP**: conexión TCP, enmarcado de paquetes, `Ack`/`Nack` según el modo que ya implementó Bartman, lectura/escritura de memoria y registros, paso, señales, etc.
- **Paquetes que el IDE y GDB usan a diario**: entre otros, **`qOffsets`**, **`Z0`/`z0`**, y el flujo que enlaza el stub con el ciclo de emulación.
- **Extensión prevista por el ecosistema GDB**: comandos **monitor** encapsulados en **`qRcmd`** (hex de la cadena `monitor …`). Bartman ya añadió muchos; ampliar por **nuevos** `monitor foo` suele ser **seguro** si no reutilizas nombres ni cambias el formato de respuesta de los existentes sin actualizar clientes.

**Colaboradores en el mismo árbol histórico:** aparecen a menudo **Bernhard Wodok** / **bwodok** en `git log` de `barto_gdbserver.cpp`; cuentan como línea **Bartman / upstream del fork**, no como capa «David».

### 2. axewater — fork público y monitor orientado a MCP

- **GitHub:** usuario **axewater**, repo del emulador [axewater/WinUAE](https://github.com/axewater/WinUAE) (carpeta local **WinUAE-DBG**), README que apunta a MCP [axewater/mcp-winuae-emu](https://github.com/axewater/mcp-winuae-emu).
- **Commit atribuido en git a acehighness** (`wateraxe@gmail.com`, alineado con el nick axewater): **`fa28ff8536078f1b26b6d390f8e57c02d1c14449`** — *Add monitor screenshot and disasm commands to GDB server* (solo **`monitor screenshot`** y **`monitor disasm`** vía `qRcmd`). Incluye `Co-Authored-By: Claude Opus 4.6`.

### 3. David (david.jurado) — cambios posteriores en `barto_gdbserver` y WinUAE

Incluye, entre otros: **relocalización** (`ELF_TEXT_BASE`, `relocate_breakpoints`, ajustes en handlers que afectan a cómo GDB coloca breakpoints frente a `qOffsets`), comandos monitor adicionales (**`memcfg`**, findproc/findcli, discos, audio/gráficos, `barto_log`), **correcciones para MCP** (depurador vs emulación en marcha, registros, warp), evolución de **screenshot** en el stub, texto de log **WinUAE-DBG v2.0 (axewater fork)** en **`85a37798`**, y trabajo de **ratón absoluto** / proyecto (**`build.bat`**, VS). Detalle de relocalización: [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md), [RELOCATION-FIX.md](RELOCATION-FIX.md).

Para cambios en la **extensión** TypeScript (`mi2.ts`, `symbols.ts`, …), ver el repositorio clonado de **vscode-amiga-debug** y el mismo historial de fechas.

### Protocolo GDB / RSP: reglas para no romper nada

1. **No cambiar a la ligera** el significado o el formato de respuesta de **`qOffsets`**, **`Z0`/`z0`**, ni del flujo de **ack** de paquetes, sin revisar el **GDB fork de Bartman** y **vscode-amiga-debug** (y [mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu), que habla el mismo RSP).
2. **Nuevas funciones** de depuración: preferir **`monitor <comando>`** vía `qRcmd` (como Bartman ya hace) en lugar de inventar prefijos de paquete RSP «ad hoc» que GDB oficial no entienda.
3. **Registros custom / extensiones**: mantener coherencia con lo que ya documentó Bartman y con lo que el cliente envía (`p`/`P`, etc.).
4. Tras cambios en el stub, probar al menos: **conexión** `target remote :2345`, **breakpoints**, **step**, **`maintenance packet qOffsets`** y un **`monitor …`** representativo.

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

La **separación Bartman / axewater / David** y las **reglas del protocolo GDB** están arriba en [Atribución: Bartman, axewater y David — y el protocolo GDB](#atribucion-bartman-vs-fork). La tabla siguiente agrupa por **área técnica** (puede mezclar capas 2 y 3).

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

### Compatibilidad mcp-winuae-emu ↔ WinUAE-DBG

El cliente MCP ([repositorio](https://github.com/axewater/mcp-winuae-emu); otras copias bajo [dvdjg/mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu)) está pensado para **no sustituir** el protocolo GDB: solo habla **RSP** y **`qRcmd`** como cualquier otro cliente.

| Aspecto | En WinUAE-DBG (`barto_gdbserver`) | En MCP (`gdb-protocol.ts`, `index.ts`) | ¿Coherente? |
|---------|-----------------------------------|----------------------------------------|-------------|
| Conexión | TCP, mismo puerto por defecto | `Socket`, reintentos, `qSupported`, `QStartNoAckMode` opcional | Sí |
| Arranque y `baseText` | `qOffsets` actualiza bases y dispara relocalización de breakpoints | Tras conectar, si `initializeStopped` es true, envía **`qOffsets`** (comentario explícito: disparar cálculo en WinUAE-DBG) | Sí |
| Formato `qOffsets` | Respuesta tipo `Text=%x;Data=%x;Bss=%x;LoadOffset=%x;SizeText=%x` | `winuae_qoffsets` parsea `Text`/`Data`/`Bss`; el resto queda en `raw` si hace falta | Sí (tres primeros campos) |
| Monitor | `memcfg`, `findproc`, `screenshot`, `disasm`, … | `winuae_memory_map` → `monitor memcfg`; `winuae_findproc` → `findproc`; screenshot vía `monitor screenshot` o fallback ventana | Sí |
| Adjunto no intrusivo | Necesario durante arranque sin frenar el target | `force_break=false` / `initializeStopped=false` omite Ctrl+C inicial, `?` y **`qOffsets`** | Sí |

**Documentación ya rica en el MCP:** el `README.md` del servidor lista variables de entorno (`WINUAE_USE_ACK`, etc.), tabla de **comandos monitor** (`screenshot`, `disasm`, `memcfg`, …), flujo de capturas (`capture_mode`), límites (CIA, un cliente TCP, Hunk frágil) y scripts de prueba. Conviene mantener ese README como **fuente operativa** y este `doc/` como **contexto de autoría y protocolo** (sección de atribución arriba).

**Posibles mejoras documentales (opcionales):** (1) en el README del MCP, un subapartado **«Binario: releases axewater vs build WinUAE-DBG»** con enlace a este repo; (2) si el formato de `qOffsets` del stub cambiara, actualizar a la vez el regex en `winuae_qoffsets` y una línea en [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md); (3) anotar en MCP que `findproc` asume respuesta **hex** a `qRcmd` como el resto de comandos monitor del fork.

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
| [axewater/mcp-winuae-emu](https://github.com/axewater/mcp-winuae-emu) | MCP sobre RSP (también [dvdjg/mcp-winuae-emu](https://github.com/dvdjg/mcp-winuae-emu)); tabla compatibilidad ↔ WinUAE-DBG en §4 |
| `od-win32/barto_gdbserver.cpp` | Implementación del servidor GDB en este árbol |
| [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md) | Diagrama DAP → MI → RSP |
| [DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md) | Ratón, flags, checklist |

---

*Documento orientado a contexto para humanos e IA que trabajen sobre WinUAE-DBG y MCP.*
