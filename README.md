# WinUAE-DBG – Compilación en Windows

Fork de WinUAE con servidor GDB y comandos monitor extendidos para depuración remota de programas Amiga. Compatible con [mcp-winuae-emu](https://github.com/axewater/mcp-winuae-emu) (MCP server para Cursor/Claude).

## Requisitos del sistema

- Windows 7 32-bit/64-bit o superior.

## Requisitos de compilación

### 1. Visual Studio 2017, 2019, 2022 o 2026

El proyecto usa *Platform Toolset* **v145** (VS2022). Compatible con VS2026, VS2022, VS2019 y VS2017.

- **VS2026** requiere `VCTargetsPath` (ver línea de comandos). **VS2022/2019/2017**: usar MSBuild de la ruta correspondiente.
- Instala la carga de trabajo **“Desarrollo para el escritorio con C++”**.
- Opcional: “Soporte para C++ de Windows XP” si necesitas compatibilidad con XP.
- Instala "Desarrollo para el escritorio con C++" y un **Windows 10 SDK** (10.0.17763.0 o superior).

### 2. Windows Driver Kit (WDK)

Versión 16299 (1709) o más reciente.

- https://docs.microsoft.com/en-us/windows-hardware/drivers/other-wdk-downloads

### 3. Librerías e includes de WinUAE

Descarga **winuaeinclibs.zip** y descomprímelo de forma que queden:

- `c:\dev\include`
- `c:\dev\lib` (y `c:\dev\lib\x64` para compilaciones 64-bit)

Enlace: https://download.abime.net/winuae/files/b/winuaeinclibs.zip

### 4. Código fuente

- Clona o descarga el código (p. ej. desde GitHub).
- Descarga **aros.rom.cpp.zip** y descomprímelo **dentro del directorio raíz del código fuente** de WinUAE.

  https://download.abime.net/winuae/files/b/aros.rom.cpp.zip

### 5. NASM (ensamblador)

Instala NASM y asegúrate de que esté en el **PATH** del sistema (o en la sesión donde ejecutes la compilación).

- https://www.nasm.us/

Si NASM está en `C:\Program Files\NASM`, puedes añadirlo al PATH solo para la compilación:

```powershell
$env:PATH = "C:\Program Files\NASM;$env:PATH"
```

---

## Compilación desde Visual Studio

1. Abre la solución:  
   `<directorio_fuente>\od-win32\winuae_msvc15\winuae_msvc.sln`  
   (Si aparece un aviso de “Unsupported”, puedes aceptar y continuar.)

2. **Si es la primera vez o quieres regenerar código** (build limpio), compila antes los proyectos auxiliares en **Release | Win32** y en este orden:
   - build68k  
   - genlinetoscr  
   - genblitter  
   - gencpu  
   - gencomp  
   - prowizard  
   - unpackers  

   (Clic derecho en cada proyecto → “Compilar”.)

3. Elige la configuración final:
   - **FullRelease** (optimizado) o **Test** (debug).
   - Plataforma **Win32** (32-bit) o **x64** (64-bit).

4. Establece el proyecto **winuae** como proyecto de inicio y compila (F7 o Compilar → Compilar solución).

Los ejecutables se generan en:

- `<directorio_fuente>\bin\`  
  Por defecto el ejecutable se llama **winuae-gdb.exe** (tanto en 32 como en 64 bits; cada compilación sobrescribe la anterior si usas el mismo directorio).

---

## Compilación desde línea de comandos (MSBuild)

Con NASM en el PATH y desde el directorio raíz del código fuente:

**64-bit Release (VS2026):**

```powershell
$env:PATH = "C:\Program Files\NASM;$env:PATH"
$env:VCTargetsPath = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\"
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "od-win32\winuae_msvc15\winuae_msvc.vcxproj" `
  /p:Configuration=Release /p:Platform=x64 /t:build /m
```

**64-bit Release (VS2022):**

```powershell
$env:PATH = "C:\Program Files\NASM;$env:PATH"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "od-win32\winuae_msvc15\winuae_msvc.sln" `
  /p:Configuration=Release /p:Platform=x64 /t:build /m
```

**64-bit Release (VS2019):**

```powershell
$env:PATH = "C:\Program Files\NASM;$env:PATH"
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "od-win32\winuae_msvc15\winuae_msvc.sln" `
  /p:Configuration=Release /p:Platform=x64 /t:build /m
```

Ajusta la ruta de `MSBuild.exe` según tu instalación (Community/Professional/Enterprise/BuildTools).

---

## Resumen de cambios en la solución

En esta rama/copia del proyecto se ha ajustado lo siguiente para facilitar la compilación con versiones recientes de Visual Studio y en distintas máquinas:

- **Platform Toolset**: actualizado a **v145** cuando se usa Visual Studio 2026; con VS2022 se usa **v143** (puedes cambiar el toolset en Propiedades del proyecto si hace falta).
- **Directorio de salida**: la salida ya no apunta a rutas fijas de otro desarrollador; se usa `$(SolutionDir)..\..\bin\`, es decir, la carpeta **bin** en la raíz del código fuente.
- No ha sido necesario renombrar archivos `.cpp` a `.c` ni adaptar cabeceras para compilar en 32 o 64 bits.

Si tras desplegar estos cambios usas **VS2022**, y el proyecto da error de toolset, en las propiedades del proyecto (todas las configuraciones) cambia *Conjunto de herramientas de la plataforma* a **v143**.

---

## Servidor GDB y comandos monitor

El ejecutable **winuae-gdb.exe** incluye un servidor GDB (puerto 2345) para depuración remota de programas Amiga. También admite comandos monitor extendidos vía `qRcmd`:

- **screenshot** – capturar la pantalla a PNG
- **disasm** – desensamblar m68k en una dirección
- **input key/event** – simular teclado (scancodes o event IDs)
- **input joy** – simular joystick/gamepad (direcciones y botones)
- **input mouse** – simular ratón (movimiento relativo/absoluto y botones)
- **reset** – restaurar savestate en la entrada del proceso
- **profile** – perfilado de CPU (avanzado)

Ver [GDB_MONITOR_COMMANDS.md](GDB_MONITOR_COMMANDS.md) para detalles y ejemplos.

---

## MCP (mcp-winuae-emu)

Para depurar Amiga desde Cursor o Claude mediante MCP:

1. **Compila** WinUAE-DBG y obtén `bin\winuae-gdb.exe`.
2. **Clona** [mcp-winuae-emu](https://github.com/axewater/mcp-winuae-emu) en un directorio hermano (p. ej. `AI\mcp-winuae-emu` junto a `AI\WinUAE-DBG`).
3. **Construye** el MCP: `cd mcp-winuae-emu && npm install && npm run build`.
4. **Configura** el MCP en Cursor: el proyecto incluye `.cursor/mcp.json`. Edita las rutas en `args` (path al MCP) y en `env` (`WINUAE_PATH`, `WINUAE_CONFIG`) según tu instalación.
5. **Reinicia** Cursor para cargar el servidor MCP.

El MCP expone herramientas como `winuae_connect`, `winuae_load`, `winuae_screenshot`, `winuae_input_key`, `winuae_input_joy`, `winuae_input_mouse`, etc.
