# Arquitectura del Sistema de Depuración Amiga

## Visión General

El sistema de depuración Amiga consiste en tres componentes principales que trabajan juntos para permitir la depuración de código C/C++ ejecutándose en un emulador Amiga.

```
┌──────────────────────────────────────────────────────────────────────┐
│                         VS Code / Cursor                             │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │              Extension: bartmanabyss.amiga-debug               │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌───────────────────────┐ │  │
│  │  │ amigaDebug.ts│  │   mi2.ts     │  │    symbols.ts         │ │  │
│  │  │  (DAP Adapter│  │ (GDB MI      │  │  (SymbolTable,        │ │  │
│  │  │   Protocol)  │  │  Protocol)   │  │   Relocation)         │ │  │
│  │  └──────┬───────┘  └──────┬───────┘  └───────────────────────┘ │  │
│  └─────────┼─────────────────┼────────────────────────────────────┘  │
└────────────┼─────────────────┼───────────────────────────────────────┘
             │ DAP             │ MI (Machine Interface)
             ▼                 ▼
┌──────────────────────────────────────────────────────────────────────┐
│                    GDB (m68k-amiga-elf-gdb.exe)                      │
│                    Fork de Bartman con soporte Amiga                 │
│  - Carga ejecutables ELF para m68k                                   │
│  - Maneja formato especial de qOffsets                               │
│  - Comunicación RSP con target remoto                                │
└──────────────────────────────────────────────────────────────────────┘
             │ RSP (Remote Serial Protocol)
             │ TCP :2345
             ▼
┌──────────────────────────────────────────────────────────────────────┐
│                         WinUAE-DBG                                   │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                    barto_gdbserver.cpp                         │  │
│  │  - Servidor GDB embebido                                       │  │
│  │  - Maneja comandos RSP (g, G, m, M, Z0, z0, qOffsets, etc.)   │  │
│  │  - Acceso a memoria y registros del CPU emulado               │  │
│  │  - Detección de programa via debugging_trigger                 │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                              │                                       │
│                              ▼                                       │
│  ┌────────────────────────────────────────────────────────────────┐  │
│  │                    Emulador Amiga                              │  │
│  │  - CPU M68000 emulado                                          │  │
│  │  - Memoria Chip/Fast RAM                                       │  │
│  │  - Sistema operativo AmigaOS / AROS                            │  │
│  └────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 1. Componentes Detallados

### 1.1 VS Code Extension (amiga-debug)

#### amigaDebug.ts
- **Función**: Implementa el Debug Adapter Protocol (DAP) de VS Code
- **Responsabilidades**:
  - Traducir solicitudes DAP a comandos GDB MI
  - Manejar breakpoints, stepping, variables, stack traces
  - Coordinar la carga del ejecutable y la conexión a WinUAE
  - Mantener el SymbolTable para mapeo de direcciones a código fuente

#### mi2.ts
- **Función**: Comunicación con GDB usando Machine Interface (MI)
- **Responsabilidades**:
  - Spawn del proceso GDB
  - Envío de comandos MI (-break-insert, -exec-continue, etc.)
  - Parseo de respuestas MI
  - Obtención de secciones (`info file`) y loadOffset (`qOffsets`)

#### symbols.ts
- **Función**: Manejo de la tabla de símbolos del ejecutable
- **Responsabilidades**:
  - Cargar símbolos usando `objdump`
  - Mapear direcciones a funciones/archivos/líneas
  - Aplicar relocalización cuando el programa se carga en memoria

### 1.2 GDB (m68k-amiga-elf-gdb.exe)

Fork de binutils-gdb modificado por Bartman para:
- Soporte de ejecutables ELF para arquitectura m68k
- Formato especial de `qOffsets` que WinUAE envía
- Integración con el toolchain amiga-gcc

**Limitaciones conocidas**:
- No aplica correctamente la relocalización de qOffsets a símbolos internos
- Los breakpoints se envían con direcciones ELF sin relocar
- El mapeo de stack traces no funciona con direcciones relocadas

### 1.3 WinUAE-DBG (GDB Server)

#### barto_gdbserver.cpp
- **Función**: Servidor GDB embebido en el emulador
- **Responsabilidades**:
  - Escuchar conexiones TCP en puerto 2345
  - Manejar protocolo RSP
  - Acceder a registros y memoria del CPU emulado
  - Detectar carga del programa objetivo
  - Gestionar breakpoints

---

## 2. Protocolos de Comunicación

### 2.1 Debug Adapter Protocol (DAP)

Comunicación entre VS Code y la extensión. Mensajes JSON bidireccionales.

```json
// Ejemplo: Solicitud de stack trace
{
  "type": "request",
  "command": "stackTrace",
  "arguments": {
    "threadId": 1,
    "startFrame": 0,
    "levels": 20
  }
}
```

### 2.2 GDB Machine Interface (MI)

Comunicación entre extensión y GDB. Formato texto estructurado.

```
// Comando
5-stack-list-frames --thread 1 0 5

// Respuesta
5^done,stack=[
  frame={level="0",addr="0x00c44f50",func="??",arch="m68k:68000"},
  frame={level="1",addr="0x00f906b2",func="??",arch="m68k:68000"}
]
```

### 2.3 Remote Serial Protocol (RSP)

Comunicación entre GDB y WinUAE. Paquetes binarios/hex con checksum.

```
// Formato general
$<data>#<checksum>

// Ejemplos
$g#67                           // Leer todos los registros
$m00c44f50,10#xx                // Leer memoria
$Z0,4c0,1#xx                    // Establecer breakpoint
$qOffsets#xx                    // Obtener offsets de secciones
```

---

## 3. Flujo de Depuración

### 3.1 Inicio de Sesión

```
1. Usuario presiona F5 en VS Code
2. Extension lee launch.json, obtiene programa a depurar
3. Extension crea SymbolTable usando objdump
4. Extension lanza WinUAE con configuración apropiada
5. Extension lanza GDB con ejecutable ELF
6. GDB conecta a WinUAE (target remote :2345)
7. Extension obtiene secciones de GDB (info file)
8. Extension obtiene loadOffset de qOffsets
9. Extension aplica relocalización al SymbolTable
10. Extension envía breakpoints a GDB
11. Extension envía continue a GDB
```

### 3.2 Hit de Breakpoint

```
1. CPU emulado alcanza dirección de breakpoint
2. WinUAE detiene emulación
3. WinUAE notifica a GDB (stop reply: S05)
4. GDB notifica a extension (async record: *stopped)
5. Extension solicita stack trace a GDB
6. Extension mapea direcciones a código fuente usando SymbolTable
7. Extension envía stack frames a VS Code
8. VS Code muestra código fuente y resalta línea actual
```

### 3.3 Stepping

```
1. Usuario presiona F10 (Step Over) o F11 (Step Into)
2. VS Code envía solicitud next/stepIn a extension
3. Extension envía comando -exec-next/-exec-step a GDB
4. GDB envía vCont;s a WinUAE
5. WinUAE ejecuta una instrucción y detiene
6. Flujo continúa como en "Hit de Breakpoint"
```

---

## 4. Sistema de Direcciones

### 4.1 Direcciones ELF (Compilación)

```
Sección     Dirección ELF    Tamaño
────────────────────────────────────
.text       0x00000400       0x35E2
.rodata     0x000039E2       0x1C7C
.data       0x00006000       0x01A6
.bss        0x00014D24       0x048C
```

### 4.2 Direcciones Amiga (Ejecución)

```
Hunk        Dirección Amiga  Contenido
────────────────────────────────────────
CODE        0x00C44F50       .text
DATA        0x00C42458       .data + .bss
```

### 4.3 Cálculo de Relocalización

```
ELF_TEXT_BASE = 0x400
baseText = 0x00C44F50  (de qOffsets)
loadOffset = baseText - ELF_TEXT_BASE = 0x00C44B50

Para convertir dirección ELF a Amiga:
  amigaAddr = elfAddr + loadOffset
  0x00C45010 = 0x4C0 + 0x00C44B50

Para convertir dirección Amiga a ELF:
  elfAddr = amigaAddr - loadOffset
  0x4C0 = 0x00C45010 - 0x00C44B50
```

---

## 5. Comandos RSP Importantes

### 5.1 qOffsets

**Solicitud**: `$qOffsets#xx`

**Respuesta estándar GDB**: `Text=xxxx;Data=yyyy;Bss=zzzz`

**Respuesta WinUAE (formato Bartman)**: `$00c44f50;00c42458;00003af8;...`

El formato de Bartman es una lista de direcciones base de hunks separadas por `;`. El primer valor es la base del hunk de código (baseText).

### 5.2 Z0/z0 (Breakpoints)

**Establecer**: `$Z0,<addr>,<kind>#xx`
- `addr`: Dirección del breakpoint (hex)
- `kind`: Tipo de breakpoint (1 para software BP)

**Eliminar**: `$z0,<addr>,<kind>#xx`

**Problema**: GDB envía `addr` como dirección ELF sin relocar.

### 5.3 g/G (Registros)

**Leer todos**: `$g#67`
**Escribir todos**: `$G<data>#xx`

Respuesta es hex de todos los registros: D0-D7, A0-A7, SR, PC.

### 5.4 m/M (Memoria)

**Leer**: `$m<addr>,<length>#xx`
**Escribir**: `$M<addr>,<length>:<data>#xx`

---

## 6. Archivos de Configuración

### 6.1 launch.json

```json
{
  "type": "amiga",
  "request": "launch",
  "name": "AROS",
  "program": "${workspaceFolder}/${config:amiga.program}"
}
```

### 6.2 default.uae

Configuración de WinUAE incluyendo:
```ini
debugging_features=gdbserver
debugging_trigger=:a.exe
gfx_api=direct3d11
```

`debugging_trigger` especifica el nombre del programa que WinUAE debe detectar para activar el servidor GDB y calcular baseText.

---

## 7. Diagnóstico y Depuración

### 7.1 Logs de GDB

Habilitar logs detallados:
```
set debug remote 1
set debug infrun 1
```

### 7.2 Logs de WinUAE

Los mensajes `barto_log()` van a:
- `OutputDebugString` (visible en Visual Studio debugger)
- `output()` (enviado a GDB como console output)

### 7.3 Comandos Útiles de GDB

```gdb
info breakpoints          # Ver breakpoints y sus direcciones
info file                 # Ver secciones del ejecutable
maintenance packet qOffsets  # Enviar qOffsets manualmente
info address main         # Ver dirección de símbolo
```

---

## 8. Problemas Conocidos y Workarounds

### 8.1 Breakpoints No Se Activan

**Causa**: Direcciones no relocadas.
**Solución**: Implementar relocalización diferida en WinUAE (ver RELOCATION-FIX.md).

### 8.2 Stack Trace Muestra Desensamblado

**Causa**: GDB no mapea direcciones relocadas a símbolos.
**Solución**: La extensión usa su propio SymbolTable con direcciones relocadas.

### 8.3 Variables No Se Muestran

**Causa**: GDB no puede leer memoria con direcciones ELF.
**Solución**: Pendiente - requiere relocalización de direcciones de variables.

---

*Documento creado: 2026-02-22*
