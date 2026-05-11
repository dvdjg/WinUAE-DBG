# Documentación WinUAE-DBG

Toda la documentación técnica del fork vive en esta carpeta **`doc/`** (incluye depuración, ratón, historial de cambios e integración con VS Code y MCP).

## Índice de documentos

| Documento | Descripción |
|-----------|-------------|
| [BARTMAN-VSCODE-Y-EVOLUCION.md](BARTMAN-VSCODE-Y-EVOLUCION.md) | Aportes de BartmanAbyss (vscode-amiga-debug + GDB en WinUAE), evolución del fork, ratón, MCP e IA |
| [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md) | Arquitectura: componentes, DAP / MI / RSP, flujos |
| [DEBUGGING-STRATEGY.md](DEBUGGING-STRATEGY.md) | Estrategia de depuración (ratón, flags, checklist, `setmousestate`) |
| [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md) | Cambios desde el código original de Bartman (relocalización, extensión) |
| [RELOCATION-FIX.md](RELOCATION-FIX.md) | Problema y solución de relocalización ELF ↔ memoria Amiga |
| [MOUSE-SYSTEMS.md](MOUSE-SYSTEMS.md) | Sistemas de ratón en el host |
| [MOUSE-ABSOLUTE-TODO.md](MOUSE-ABSOLUTE-TODO.md) | Notas y pendientes del modo ratón absoluto |

## Resumen del sistema de depuración

El sistema permite depurar C/C++ para Amiga en el emulador, con VS Code (o Cursor) y GDB hacia el stub en WinUAE.

```
VS Code Extension ←→ GDB (fork Bartman) ←→ WinUAE-DBG (GDB server en barto_gdbserver)
      DAP                 MI                        RSP (TCP)
```

**Problema resuelto en el fork:** el GDB de Bartman no relocalizaba bien símbolos; la solución combina relocalización diferida en `barto_gdbserver.cpp` y ajustes en la extensión (`loadOffset`, `relocateWithOffset`). Detalle en [RELOCATION-FIX.md](RELOCATION-FIX.md) y [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md).

## Para desarrolladores

### Compilar WinUAE-DBG

```bash
cd WinUAE-DBG
./build.bat   # o Visual Studio
```

### Compilar la extensión VS Code (clon aparte)

```bash
cd vscode-amiga-debug
npm install
npm run compile
```

### Diagnóstico

1. **WinUAE**: mensajes `GDBSERVER:` / `barto_log` según configuración.
2. **Extensión**: `MI2:`, `SymbolTable.`, `amigaDebug:` en la consola de depuración.
3. **GDB**: `info breakpoints`, `maintenance packet qOffsets`, `info file`.
