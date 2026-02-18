# WinUAE – Compilación en Windows

## Requisitos del sistema

- Windows 7 32-bit/64-bit o superior.

## Requisitos de compilación

### 1. Visual Studio 2022 o posterior

Se recomienda **Visual Studio 2022** (o Visual Studio 2025/2026). El proyecto está configurado con el *Platform Toolset* **v143** (VS2022); en instalaciones más recientes puede usarse **v145**.

- **Visual Studio 2017 no es adecuado**: el código usa C++ y extensiones que requieren un compilador más reciente; la compatibilidad con C99 de VS2017 es limitada y pueden aparecer errores.
- Instala la carga de trabajo **“Desarrollo para el escritorio con C++”**.
- Opcional: “Soporte para C++ de Windows XP” si necesitas compatibilidad con XP.
- Asegúrate de tener un **Windows 10 SDK** (p. ej. 10.0.17763.0 o superior) y UCRT.

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

**32-bit (FullRelease):**

```powershell
$env:PATH = "C:\Program Files\NASM;$env:PATH"   # si NASM no está en el PATH
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "od-win32\winuae_msvc15\winuae_msvc.sln" `
  /p:Configuration=FullRelease /p:Platform=Win32 /t:build /m
```

**64-bit (FullRelease):**

```powershell
$env:PATH = "C:\Program Files\NASM;$env:PATH"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "od-win32\winuae_msvc15\winuae_msvc.sln" `
  /p:Configuration=FullRelease /p:Platform=x64 /t:build /m
```

Ajusta la ruta de `MSBuild.exe` si usas otra edición o versión de Visual Studio (por ejemplo, `...\2022\Professional\...` o `...\18\Community\...` para VS 2026).

---

## Resumen de cambios en la solución

En esta rama/copia del proyecto se ha ajustado lo siguiente para facilitar la compilación con versiones recientes de Visual Studio y en distintas máquinas:

- **Platform Toolset**: actualizado a **v145** cuando se usa Visual Studio 2026; con VS2022 se usa **v143** (puedes cambiar el toolset en Propiedades del proyecto si hace falta).
- **Directorio de salida**: la salida ya no apunta a rutas fijas de otro desarrollador; se usa `$(SolutionDir)..\..\bin\`, es decir, la carpeta **bin** en la raíz del código fuente.
- No ha sido necesario renombrar archivos `.cpp` a `.c` ni adaptar cabeceras para compilar en 32 o 64 bits.

Si tras desplegar estos cambios usas **VS2022**, y el proyecto da error de toolset, en las propiedades del proyecto (todas las configuraciones) cambia *Conjunto de herramientas de la plataforma* a **v143**.
