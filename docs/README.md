# Documentación del Sistema de Depuración WinUAE-DBG

Esta carpeta contiene documentación técnica sobre el sistema de depuración Amiga basado en WinUAE.

## Documentos

| Documento | Descripción |
|-----------|-------------|
| [DEBUGGING-ARCHITECTURE.md](DEBUGGING-ARCHITECTURE.md) | Arquitectura general del sistema: componentes, protocolos, flujos de datos |
| [RELOCATION-FIX.md](RELOCATION-FIX.md) | Explicación detallada del problema de relocalización de direcciones y su solución |
| [HISTORIAL-CAMBIOS.md](HISTORIAL-CAMBIOS.md) | Registro de todos los cambios realizados desde el código original de Bartman |

## Resumen del Sistema

El sistema permite depurar código C/C++ compilado para Amiga ejecutándose en el emulador WinUAE, usando VS Code como IDE.

```
VS Code Extension ←→ GDB (Bartman fork) ←→ WinUAE-DBG (GDB Server)
      ↓                    ↓                       ↓
   DAP Protocol        MI Protocol            RSP Protocol
```

## Problema Principal Resuelto

El GDB modificado de Bartman no aplica correctamente la relocalización de símbolos. Esto causaba:
- Breakpoints que no se activaban (direcciones incorrectas)
- Stack traces mostrando desensamblado en lugar de código fuente
- Variables no disponibles

La solución implementada:
1. **WinUAE-DBG**: Relocalización diferida de breakpoints en `barto_gdbserver.cpp`
2. **VS Code Extension**: Relocalización del SymbolTable usando `loadOffset` en `symbols.ts`

## Para Desarrolladores

### Compilar WinUAE-DBG

```bash
cd WinUAE-DBG
./build.bat   # o usar Visual Studio
```

### Compilar Extensión VS Code

```bash
cd vscode-amiga-debug
npm install
npm run compile
```

### Instalar Extensión Modificada

```bash
cp dist/extension.js ~/.cursor/extensions/bartmanabyss.amiga-debug-1.7.9/dist/
# Reiniciar VS Code/Cursor
```

### Diagnóstico

1. **Logs de WinUAE**: Buscar mensajes `GDBSERVER:` en la consola de debug de Visual Studio
2. **Logs de la extensión**: Buscar mensajes `MI2:`, `SymbolTable.`, `amigaDebug:` en la consola de VS Code
3. **Comandos GDB útiles**:
   - `info breakpoints` - Ver direcciones de breakpoints
   - `maintenance packet qOffsets` - Ver direcciones de hunks
   - `info file` - Ver secciones del ELF

## Contacto

Para problemas o mejoras, consultar el historial de cambios y la documentación de arquitectura.
