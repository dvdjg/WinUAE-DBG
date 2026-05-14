# Historial de Cambios en el Sistema de Depuración

Este documento registra los cambios en el sistema de depuración Amiga **desde la línea BartmanAbyss** (servidor GDB en WinUAE + extensión **vscode-amiga-debug** + GDB m68k fork) y, en concreto, las **capas añadidas en este fork (WinUAE-DBG)**.

**Atribución resumida:** la integración GDB↔WinUAE («**Barto**», `barto_gdbserver.cpp`) es de **BartmanAbyss** y colaboradores en ese fork (p. ej. aparición frecuente de **Bernhard Wodok** / **bwodok** en `git log` del archivo). Las correcciones de **relocalización**, gran parte de los **comandos monitor** recientes, **MCP**, **screenshots** en el stub y el trabajo en **ratón absoluto** / build están en commits de **david.jurado**. No hay autor «Alex» en ese historial; si se confunde con **axewater**, es el upstream del fork en GitHub, no una segunda implementación del servidor. Detalle narrativo: [BARTMAN-VSCODE-Y-EVOLUCION.md](BARTMAN-VSCODE-Y-EVOLUCION.md#atribucion-bartman-vs-fork).

---

## Estado Original (Extensión Bartman v1.7.9)

### WinUAE-DBG Original

El código original de `barto_gdbserver.cpp` manejaba:

```cpp
// Handler qOffsets original
} else if(request.substr(0, 8) == "qOffsets") {
    std::string response;
    for(int i = 0; i < numSections; i++) {
        if(!response.empty())
            response += ';';
        char hex[32];
        sprintf(hex, "%x", sectionBases[i]);
        response += hex;
    }
    SendPacket(response);
    
    // baseText se calculaba aquí
    baseText = sectionBases[0];  // Asumiendo CODE es primero
}

// Handler Z0 original
} else if(request.substr(0, 2) == "Z0") {
    uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
    // Sin relocalización - usaba dirección tal cual llegaba
    bpn.value1 = adr;
}
```

### VS Code Extension Original

**mi2.ts original**:
- Obtenía secciones con `info file`
- NO consultaba `qOffsets`
- Emitía secciones sin loadOffset

**symbols.ts original**:
- `relocate(sections)`: Buscaba secciones por nombre
- Sin método `relocateWithOffset`

**amigaDebug.ts original**:
- Usaba `symbolTable.relocate(sections)` directamente

---

## Cambios Realizados (2026-02-22)

### Fase 1: Intento con `info sections`

**Cambio**: Intentar usar `info sections` en lugar de `info file`.

**Resultado**: ❌ Fallido - El GDB de Bartman no soporta este comando.

**Revertido**: Sí, inmediatamente.

### Fase 2: Intento con `monitor offset`

**Cambio**: Añadir comando `monitor offset` a WinUAE que devuelva baseText.

```cpp
// En barto_gdbserver.cpp
} else if(request.substr(0, 14) == "qRcmd,6f6666736574") { // "offset" hex
    char response[64];
    sprintf(response, "baseText=%x\n", baseText);
    SendPacket(hexEncode(response));
}
```

**Resultado**: ❌ Parcialmente fallido - La salida de `monitor` no era capturada correctamente por `sendUserInput` en mi2.ts.

**Revertido**: Código dejado pero no usado.

### Fase 3: Uso de `maintenance packet qOffsets`

**Cambio en mi2.ts**:

```typescript
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
```

**Resultado**: ✅ Funciona - loadOffset se calcula correctamente.

### Fase 4: Relocalización Diferida en WinUAE

**Problema identificado**: Los breakpoints se establecían ANTES de que `qOffsets` calculara `baseText`, resultando en direcciones ELF sin relocar.

**Cambio en barto_gdbserver.cpp**:

```cpp
// Nueva constante
constexpr uaecptr ELF_TEXT_BASE = 0x400;

// Almacenamiento de direcciones ELF
std::vector<uaecptr> breakpoint_elf_addresses;

// Nueva función de relocalización
void relocate_breakpoints() {
    if(baseText < ELF_TEXT_BASE) return;
    uaecptr loadOffset = baseText - ELF_TEXT_BASE;
    for(size_t i = 0; i < breakpoint_elf_addresses.size(); i++) {
        uaecptr elfAddr = breakpoint_elf_addresses[i];
        if(elfAddr >= ELF_TEXT_BASE && elfAddr < ELF_TEXT_BASE + 0x100000) {
            uaecptr relocatedAddr = elfAddr + loadOffset;
            for(auto& bpn : bpnodes) {
                if(bpn.enabled && bpn.value1 == elfAddr) {
                    bpn.value1 = relocatedAddr;
                    break;
                }
            }
        }
    }
}

// Modificación del handler Z0
} else if(request.substr(0, 2) == "Z0") {
    uaecptr adr = strtoul(...);
    uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
    uaecptr relocatedAdr = adr;
    if(loadOffset > 0 && adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000) {
        relocatedAdr = adr + loadOffset;
    }
    breakpoint_elf_addresses.push_back(adr);
    bpn.value1 = relocatedAdr;
}

// Llamada en qOffsets
} else if(request.substr(0, 8) == "qOffsets") {
    // ... cálculo de baseText ...
    relocate_breakpoints();  // NUEVO
}
```

**Resultado**: ✅ Los breakpoints se relocalizan correctamente cuando baseText está disponible.

### Fase 5: Mejora del SymbolTable

**Problema identificado**: `symbolTable.relocate(sections)` era frágil porque dependía de coincidencia exacta de nombres de secciones.

**Nuevo método en symbols.ts**:

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

**Cambio en mi2.ts**:

```typescript
// Aplicar loadOffset a las secciones para compatibilidad con relocate()
if (loadOffset > 0) {
    for (const section of sections) {
        section.address += loadOffset;
    }
}

// Emitir secciones (ya relocadas) Y loadOffset (para relocateWithOffset)
this.emit("sections-loaded", sections, loadOffset);
```

**Nota**: `mi2.ts` ahora aplica el offset a las secciones antes de emitirlas, proporcionando dos mecanismos de relocalización:
1. Las `sections` emitidas ya tienen direcciones relocadas (para uso con `relocate()`)
2. El `loadOffset` se emite por separado (para uso con `relocateWithOffset()`)

Esto proporciona compatibilidad hacia atrás y redundancia.

**Cambio en amigaDebug.ts**:

```typescript
this.miDebugger.once('sections-loaded', (sections: Section[], loadOffset?: number) => {
    if(loadOffset && loadOffset > 0) {
        this.symbolTable.relocateWithOffset(loadOffset);
    } else {
        this.symbolTable.relocate(sections);
    }
});
```

**Resultado**: ✅ La relocalización es más robusta y no depende de nombres de secciones.

---

## Resumen de Cambios por Archivo

### barto_gdbserver.cpp

| Línea (aprox) | Cambio | Motivo |
|---------------|--------|--------|
| ~100 | `constexpr uaecptr ELF_TEXT_BASE = 0x400;` | Constante para cálculos |
| ~110 | `std::vector<uaecptr> breakpoint_elf_addresses;` | Almacén de direcciones ELF |
| ~500 | `void relocate_breakpoints()` | Relocalización diferida |
| ~700 | Handler Z0 modificado | Almacenar y relocalizar |
| ~750 | Handler z0 modificado | Limpiar direcciones ELF |
| ~800 | Handler qOffsets modificado | Llamar relocate_breakpoints() |

### mi2.ts

| Método | Cambio | Motivo |
|--------|--------|--------|
| `connect()` | Añadido código para obtener loadOffset de qOffsets | Calcular relocalización |
| `connect()` | Emitir loadOffset en evento sections-loaded | Pasar offset a amigaDebug |

### symbols.ts

| Método | Cambio | Motivo |
|--------|--------|--------|
| `relocateWithOffset()` | Nuevo método | Relocalización más robusta |
| `relocate()` | Añadido logging | Diagnóstico |
| `getFunctionAtAddress()` | Añadido logging | Diagnóstico |

### amigaDebug.ts

| Método | Cambio | Motivo |
|--------|--------|--------|
| Handler `sections-loaded` | Usar loadOffset si disponible | Preferir nuevo método de relocalización |

---

## Código Original vs Código Modificado

### Handler Z0 - Comparación

**Original**:
```cpp
} else if(request.substr(0, 2) == "Z0") {
    auto comma = request.find(',', strlen("Z0"));
    if(comma != std::string::npos) {
        uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
        // Buscar bpnode libre y asignar
        for(auto& bpn : bpnodes) {
            if(bpn.enabled) continue;
            bpn.value1 = adr;  // Sin relocalización
            bpn.enabled = true;
            // ...
            break;
        }
        SendPacket("OK");
    }
}
```

**Modificado**:
```cpp
} else if(request.substr(0, 2) == "Z0") {
    auto comma = request.find(',', strlen("Z0"));
    if(comma != std::string::npos) {
        uaecptr adr = strtoul(request.data() + strlen("Z0,"), nullptr, 16);
        
        // NUEVO: Calcular loadOffset si baseText disponible
        uaecptr loadOffset = (baseText >= ELF_TEXT_BASE) ? (baseText - ELF_TEXT_BASE) : 0;
        uaecptr relocatedAdr = adr;
        if(loadOffset > 0 && adr >= ELF_TEXT_BASE && adr < ELF_TEXT_BASE + 0x100000) {
            relocatedAdr = adr + loadOffset;
        }
        
        // NUEVO: Almacenar dirección ELF para relocalización diferida
        breakpoint_elf_addresses.push_back(adr);
        
        for(auto& bpn : bpnodes) {
            if(bpn.enabled) continue;
            bpn.value1 = relocatedAdr;  // Usar dirección relocada
            bpn.enabled = true;
            // ...
            break;
        }
        SendPacket("OK");
    }
}
```

---

## Notas para Futuras Modificaciones

1. **Si se modifica el toolchain amiga-gcc**: Verificar que `ELF_TEXT_BASE` siga siendo `0x400`.

2. **Si se añaden nuevos tipos de breakpoints (watchpoints)**: Aplicar la misma lógica de relocalización diferida.

3. **Si se cambia el formato de qOffsets**: Actualizar el regex en mi2.ts y la lógica de parseo.

4. **Para debug de los cambios**: Buscar mensajes `barto_log` en la salida de debug y mensajes `MI2:` en la consola de VS Code.

---

*Actualizado: 2026-02-22; atribución Bartman vs fork local: 2026-05-14*
