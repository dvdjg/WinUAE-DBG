# Corrección del Sistema de Relocalización para Depuración Amiga

## Resumen Ejecutivo

Este documento describe el problema de relocalización de direcciones en el sistema de depuración Amiga (WinUAE + GDB + VS Code) y las correcciones implementadas para solucionarlo.

**Síntoma principal**: El depurador mostraba desensamblado en lugar de código fuente C, y los breakpoints no se activaban.

**Causa raíz**: Las direcciones de los símbolos y breakpoints no se relocalizaban correctamente entre las direcciones ELF (compilación) y las direcciones de memoria Amiga (ejecución).

---

## 1. Arquitectura Original de Bartman

### 1.1 Componentes del Sistema

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   VS Code       │     │   GDB Bartman   │     │   WinUAE-DBG    │
│   Extension     │◄───►│ m68k-amiga-elf  │◄───►│   GDB Server    │
│ (amiga-debug)   │ MI  │     -gdb.exe    │ RSP │                 │
└─────────────────┘     └─────────────────┘     └─────────────────┘
        │                       │                       │
        ▼                       ▼                       ▼
   SymbolTable            Símbolos ELF           Memoria Amiga
   (objdump)              (sin relocar)          (relocada)
```

### 1.2 El Problema de las Direcciones

Cuando se compila un programa Amiga con `amiga-gcc`:

1. **Direcciones ELF**: El ejecutable ELF tiene secciones con direcciones base fijas:
   - `.text` comienza en `0x400`
   - `.data` comienza en `0x6000` (típicamente)
   - etc.

2. **Direcciones Amiga**: Cuando el programa se carga en memoria Amiga (Chip RAM), el sistema operativo lo coloca en una dirección dinámica:
   - `.text` puede estar en `0x00C44F50`
   - `.data` puede estar en `0x00C42458`
   - etc.

3. **Load Offset**: La diferencia entre la dirección real y la dirección ELF:
   ```
   loadOffset = baseText - ELF_TEXT_BASE
   loadOffset = 0x00C44F50 - 0x400 = 0x00C44B50
   ```

### 1.3 Flujo Original de Bartman

```
1. VS Code lanza GDB con el ejecutable ELF
2. GDB carga símbolos con direcciones ELF (0x400, etc.)
3. GDB conecta a WinUAE via "target remote :2345"
4. GDB envía qOffsets a WinUAE
5. WinUAE responde con direcciones de hunks Amiga (0x00C44F50;...)
6. GDB (modificado por Bartman) DEBERÍA relocalizar sus símbolos internos
7. VS Code establece breakpoints via GDB
8. GDB envía Z0 con direcciones (¿ELF o relocadas?)
```

### 1.4 Intención de Bartman

Bartman modificó GDB (`binutils-gdb` fork) para:
- Manejar el formato especial de `qOffsets` que WinUAE envía
- Relocalizar internamente los símbolos cuando recibe qOffsets
- Enviar breakpoints con direcciones ya relocadas a WinUAE

**El problema**: El GDB modificado de Bartman NO aplica correctamente la relocalización a:
- Su tabla de símbolos interna (por eso `info breakpoints` muestra direcciones ELF)
- Los comandos `Z0` que envía a WinUAE (envía direcciones ELF sin relocar)
- El mapeo de direcciones para el stack trace

---

## 2. Diagnóstico del Problema

### 2.1 Evidencia del Problema

**Comando GDB `info breakpoints`**:
```
Num  Type        Address    What
1    breakpoint  0x000004c0 in main at main.c:15  ← Dirección ELF, NO relocada
2    breakpoint  0x00000542 in main at main.c:28
3    breakpoint  0x00000560 in main at main.c:33
```

**Comando GDB `maintenance packet qOffsets`**:
```
received: "00c44f50;00c42458;00003af8;..."  ← WinUAE envía direcciones correctas
```

**Stack trace de GDB**:
```
#0  0x00c44f50 in ?? ()  ← GDB no mapea la dirección a función
#1  0x00f906b2 in ?? ()
```

### 2.2 Secuencia de Eventos Problemática

```
Tiempo  Evento                              Problema
─────────────────────────────────────────────────────────────────────
T1      GDB conecta a WinUAE                -
T2      GDB envía Z0,4c0,1 (breakpoint)     WinUAE recibe dirección ELF
T3      WinUAE almacena BP en 0x4c0         Dirección incorrecta
T4      GDB envía qOffsets                  -
T5      WinUAE calcula baseText=0x00C44F50  Ahora sabe la dirección real
T6      Programa se ejecuta                 -
T7      PC llega a 0x00C45010 (main:15)     BP en 0x4c0 NO coincide
T8      Breakpoint NO se activa             ¡ERROR!
```

---

## 3. Solución Implementada

### 3.1 Cambios en WinUAE-DBG (`barto_gdbserver.cpp`)

#### 3.1.1 Constante ELF_TEXT_BASE

```cpp
// ELF .text section base address for amiga-gcc toolchain
constexpr uaecptr ELF_TEXT_BASE = 0x400;
```

Esta constante define la dirección base esperada de la sección `.text` en ejecutables ELF de amiga-gcc.

#### 3.1.2 Almacenamiento de Direcciones ELF de Breakpoints

```cpp
// Store original ELF addresses for breakpoints (for deferred relocation)
std::vector<uaecptr> breakpoint_elf_addresses;
```

Vector que almacena las direcciones ELF originales de cada breakpoint para poder relocalizarlos después.

#### 3.1.3 Función de Relocalización Diferida

```cpp
void relocate_breakpoints() {
    if(baseText < ELF_TEXT_BASE) return;  // baseText no válido aún
    
    uaecptr loadOffset = baseText - ELF_TEXT_BASE;
    
    for(size_t i = 0; i < breakpoint_elf_addresses.size(); i++) {
        uaecptr elfAddr = breakpoint_elf_addresses[i];
        
        // Solo relocalizar direcciones que parecen ser ELF
        if(elfAddr >= ELF_TEXT_BASE && elfAddr < ELF_TEXT_BASE + 0x100000) {
            uaecptr relocatedAddr = elfAddr + loadOffset;
            
            // Buscar y actualizar el bpnode correspondiente
            for(auto& bpn : bpnodes) {
                if(bpn.enabled && bpn.value1 == elfAddr) {
                    bpn.value1 = relocatedAddr;
                    break;
                }
            }
        }
    }
}
```

Esta función se llama cuando `qOffsets` calcula `baseText`, relocalizando todos los breakpoints pendientes.

#### 3.1.4 Handler Z0 Modificado

```cpp
} else if(request.substr(0, 2) == "Z0") { // set software breakpoint
    uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
    
    // Calcular loadOffset si baseText ya es conocido
    uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
    uaecptr relocatedAdr = adr;
    
    if(loadOffset > 0 && adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000) {
        // baseText ya conocido, relocalizar ahora
        relocatedAdr = adr + loadOffset;
    }
    
    // Almacenar dirección ELF para posible relocalización diferida
    breakpoint_elf_addresses.push_back(adr);
    
    // Usar dirección relocada (o ELF si baseText no conocido aún)
    bpn.value1 = relocatedAdr;
    // ...
}
```

#### 3.1.5 Llamada a relocate_breakpoints() en qOffsets

```cpp
} else if(request.substr(0, 8) == "qOffsets") {
    // ... código que calcula baseText ...
    
    // Después de calcular baseText, relocalizar breakpoints pendientes
    relocate_breakpoints();
}
```

### 3.2 Cambios en VS Code Extension

#### 3.2.1 mi2.ts - Obtención de loadOffset

```typescript
public connect(cwd: string, executable: string, commands: string[]): Promise<void> {
    // ...
    Promise.all(promises).then(async () => {
        if(executable !== '') {
            const sections = await this.getSections();
            
            const ELF_TEXT_BASE = 0x400;
            let loadOffset = 0;
            
            try {
                // Obtener loadOffset de qOffsets
                const qOffsetsNode = await this.sendUserInput('maintenance packet qOffsets');
                if (qOffsetsNode && qOffsetsNode.output) {
                    const text = qOffsetsNode.output.join('');
                    const match = /received:\s*"([0-9a-fA-F]+)/.exec(text);
                    if (match) {
                        const textBase = parseInt(match[1], 16);
                        loadOffset = textBase - ELF_TEXT_BASE;
                    }
                }
            } catch (e) {
                // qOffsets no disponible
            }
            
            // Emitir secciones Y loadOffset
            this.emit("sections-loaded", sections, loadOffset);
        }
        // ...
    });
}
```

#### 3.2.2 symbols.ts - Método relocateWithOffset

```typescript
public relocateWithOffset(loadOffset: number) {
    // Aplicar offset a todas las secciones ALLOC
    for(const section of this.sections) {
        if(section.flags?.find((v) => v === "ALLOC") && section.size > 0) {
            section.address = section.vma + loadOffset;
        }
    }
    
    // Actualizar bases de símbolos
    this.symbols.forEach((symbol) => {
        const section = this.sections.find((s) => s.name === symbol.section);
        if(section) {
            symbol.base = section.address;
        }
    });
}
```

**¿Por qué este método en lugar de `relocate(sections)`?**

El método original `relocate(sections)` dependía de que los nombres de secciones de `info file` coincidieran exactamente con los de `objdump --section-headers`. En la práctica, esto fallaba porque:

1. `info file` devuelve secciones duplicadas (Remote target + Local exec file)
2. Algunos nombres pueden tener espacios o caracteres diferentes
3. El orden de las secciones puede variar

`relocateWithOffset()` es más robusto porque:
- Aplica el mismo offset a TODAS las secciones ALLOC
- No depende de coincidencia de nombres
- Funciona correctamente con el toolchain amiga-gcc estándar

#### 3.2.3 amigaDebug.ts - Uso de loadOffset

```typescript
this.miDebugger.once('sections-loaded', (sections: Section[], loadOffset?: number) => {
    if(sections.length > 0) {
        if(loadOffset && loadOffset > 0) {
            // Método más confiable: usar loadOffset directamente
            this.symbolTable.relocateWithOffset(loadOffset);
        } else {
            // Fallback al método original
            this.symbolTable.relocate(sections);
        }
        this.started = true;
        this.sendResponse(response);
    }
});
```

---

## 4. Flujo Corregido

```
Tiempo  Evento                              Estado
─────────────────────────────────────────────────────────────────────
T1      GDB conecta a WinUAE                baseText=0
T2      GDB envía Z0,4c0,1 (breakpoint)     WinUAE almacena ELF addr 0x4c0
                                            breakpoint_elf_addresses=[0x4c0]
T3      Programa Amiga se carga             -
T4      GDB envía qOffsets                  -
T5      WinUAE calcula baseText=0x00C44F50  loadOffset=0x00C44B50
T6      relocate_breakpoints() se ejecuta   BP 0x4c0 → 0x00C45010
T7      Programa se ejecuta                 -
T8      PC llega a 0x00C45010 (main:15)     BP en 0x00C45010 COINCIDE
T9      Breakpoint SE ACTIVA                ¡ÉXITO!
```

---

## 5. Archivos Modificados

### 5.1 WinUAE-DBG

| Archivo | Cambios |
|---------|---------|
| `od-win32/barto_gdbserver.cpp` | Añadido ELF_TEXT_BASE, breakpoint_elf_addresses, relocate_breakpoints(), modificados handlers Z0/z0/qOffsets |

### 5.2 vscode-amiga-debug

| Archivo | Cambios |
|---------|---------|
| `src/backend/mi2.ts` | Modificado connect() para obtener loadOffset de qOffsets y emitirlo |
| `src/backend/symbols.ts` | Añadido método relocateWithOffset() |
| `src/amigaDebug.ts` | Modificado handler sections-loaded para usar loadOffset |

---

## 6. Posibles Mejoras Futuras

### 6.1 En WinUAE-DBG

1. **Soporte para múltiples hunks**: Actualmente asumimos que todos los hunks se relocalizan con el mismo offset. Si el programa tiene DATA y BSS en direcciones no contiguas, podría necesitarse relocalización por hunk.

2. **Limpieza de breakpoints**: Cuando una sesión de depuración termina, `breakpoint_elf_addresses` debería limpiarse.

3. **Watchpoints (Z2, Z3, Z4)**: Si GDB también envía watchpoints con direcciones ELF, necesitarían el mismo tratamiento.

### 6.2 En vscode-amiga-debug

1. **Mejor manejo de errores**: Si qOffsets falla o devuelve datos inválidos, mostrar mensaje al usuario.

2. **Cache de loadOffset**: Si el programa se reinicia sin recargar, el loadOffset podría reutilizarse.

3. **Soporte para múltiples programas**: Si se depuran varios ejecutables simultáneamente, cada uno necesita su propio loadOffset.

### 6.3 En GDB (binutils-gdb fork)

La solución ideal sería corregir el GDB de Bartman para que:
1. Aplique correctamente la relocalización de qOffsets a su tabla de símbolos
2. Envíe direcciones ya relocalizadas en comandos Z0
3. Mapee correctamente direcciones del stack trace a símbolos

Esto eliminaría la necesidad de las correcciones en WinUAE y la extensión VS Code.

---

## 7. Cómo Probar

1. Compilar WinUAE-DBG: `./build.bat`
2. Compilar extensión: `npm run compile` en vscode-amiga-debug
3. Copiar extensión: `cp dist/extension.js ~/.cursor/extensions/bartmanabyss.amiga-debug-1.7.9/dist/`
4. Reiniciar Cursor
5. Establecer breakpoint en main.c
6. Lanzar depuración (F5)
7. Verificar que el breakpoint se activa y muestra código fuente

---

## 8. Referencias

- [GDB Remote Serial Protocol](https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html)
- [qOffsets packet](https://sourceware.org/gdb/current/onlinedocs/gdb/General-Query-Packets.html#qOffsets)
- [Bartman's amiga-debug](https://github.com/BartmanAbyss/vscode-amiga-debug)
- [Bartman's binutils-gdb fork](https://github.com/BartmanAbyss/binutils-gdb)

---

*Documento creado: 2026-02-22*
*Última actualización: 2026-02-22*
