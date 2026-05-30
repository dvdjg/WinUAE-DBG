# Historial de Cambios en el Sistema de Depuración

Este documento registra los cambios en el sistema de depuración Amiga **desde la línea BartmanAbyss** (servidor GDB en WinUAE + extensión **vscode-amiga-debug** + GDB m68k fork) y, en concreto, las **capas añadidas en este fork (WinUAE-DBG)**.

**Atribución en tres capas** (Bartman / axewater / David) y **reglas para no romper GDB/RSP**: [BARTMAN-VSCODE-Y-EVOLUCION.md](BARTMAN-VSCODE-Y-EVOLUCION.md#atribucion-bartman-vs-fork). Resumen: **Bartman** = base RSP + extensión + GDB fork; **axewater** = fork GitHub + commit **`fa28ff85`** (*monitor screenshot* / *monitor disasm*, autor git **acehighness**); **David** = **david.jurado** (relocalización, monitor posterior, MCP, ratón, build, log *axewater fork* en **`85a37798`**).

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

## 2026-05-29 - Canal lateral AMG para telemetria

Se ha añadido un primer canal lateral TCP local en `od-win32/barto_gdbserver.cpp`.

- Escucha en `127.0.0.1:2346`.
- El puerto se puede cambiar con `WINUAE_SIDE_CHANNEL_PORT`.
- No comparte el socket GDB principal, por lo que no compite con Cursor/VS Code.
- Responde JSON por linea a comandos de observacion: `hello`, `state`, `regs`,
  `mem <addr> <len>` y `runstatus <addr>`.
- `state` expone tambien `sections`, porque los simbolos de las demos pueden vivir
  en hunks distintos (`.text`, `.rodata`, `.data`, `.bss`).

Este MVP se usa desde `Amiga-C/tools/run/run-demo.mjs` para esperar
`g_amg_run_status` mientras el 68000 sigue ejecutando. La regresion de `Amiga-C`
`out/regression/20260529-120303/regression-report.md` valida que las demos `000`,
`010`, `020` y `030` alcanzan `side-channel READY` antes de la captura.

Pendiente: modos `observe/assist/takeover`, debug lock, auditoria de escrituras y
comandos laterales seguros para screenshot/profiler/input.

---

## 2026-05-30 - Modos seguros del canal lateral AMG

Se ha ampliado el canal lateral TCP local en `od-win32/barto_gdbserver.cpp` para
convivir con una sesion GDB activa sin robarle el socket.

- Modos: `observe`, `assist` y `takeover`.
- Debug lock: `lock status`, `lock acquire <owner> [mode]`, `lock release [owner]`.
- Acciones seguras encoladas: `screenshot`, `input` y `profile`.
- Consulta asincrona: `action status <id>`.
- Estado de profiler lateral: `profile-status`.
- `input` y `profile` exigen lock en modo `assist` o `takeover`.
- Las acciones con efectos laterales se ejecutan desde `vsync_pre()`, no desde el
  hilo TCP, para evitar llamadas a renderer/input/profiler desde un hilo externo.
- El perfil iniciado por canal lateral no envia `$OK` al cliente GDB; actualiza
  `profile-status` para no contaminar la sesion de Cursor/VS Code.

Bug corregido durante la validacion: el tokenizer del canal lateral consumia las
barras `\` de rutas Windows entrecomilladas. Ahora conserva `C:\...` correctamente
y solo trata `\"` y `\\` como escapes reales.

Validado desde `Amiga-C` con:

```powershell
node .\tools\debug\verify-side-channel-contract.mjs --settle-ms 9000
```

La prueba lanza `030_ehb_palette_zones`, mantiene GDB conectado, toma el lock
`assist`, inyecta raton emulado, escribe una captura PNG lateral y genera un perfil
de 1 frame.

Tambien se ha validado la convivencia con depuracion normal:

```powershell
.\tools\debug\verify-gdb-step-side-channel.ps1 -Steps 3
```

Esta prueba pone un breakpoint GDB en `amg_debug_ready_probe`, continua hasta
`T05swbreak`, hace step instruction varias veces y consulta el canal lateral
durante la ejecucion y en cada parada, verificando que PC/GDB y PC lateral
coinciden.

Primer takeover reversible:

- `poke <addr> <hex-bytes> [label]`: exige lock `takeover`, lee bytes previos,
  escribe hasta 256 bytes, verifica lectura posterior y crea una auditoria.
- `rollback <write-id>`: exige lock `takeover`, restaura los bytes previos y marca
  la auditoria como revertida.
- `audit writes` / `audit write <id>`: lista las escrituras laterales realizadas.

Validado desde `Amiga-C` con:

```powershell
.\tools\debug\verify-side-channel-takeover.ps1
```

La prueba escribe temporalmente `12345678` en `g_amg_run_status.detail`, verifica
por lectura de memoria, consulta la auditoria y restaura el valor original.

Pausa/reanudacion lateral:

- `pause`: exige lock `takeover`, se encola y se ejecuta desde `vsync_pre()`.
- `resume`: exige lock `takeover`, se ejecuta inmediatamente porque la cola de
  `vsync_pre()` puede no procesarse mientras la emulacion esta pausada.

Validado desde `Amiga-C` con:

```powershell
.\tools\debug\verify-side-channel-pause-resume.ps1
```

La prueba pausa, lee `g_amg_run_status` mientras el emulador esta detenido,
reanuda y permite que el runner complete su captura final.

Pendiente: snapshots/savestates y zona scratch para diagnostico 68k.

---

*Actualizado: 2026-02-22; atribución Bartman vs fork local: 2026-05-14; canal lateral AMG: 2026-05-30*
