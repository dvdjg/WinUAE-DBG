# Estrategia de depuración autónoma para desarrollos en WinUAE-DBG

Este documento describe cómo depurar de forma autónoma desarrollos en WinUAE-DBG, especialmente cambios que afectan a entrada (ratón, teclado), display o interacción con el hardware virtualizado del Amiga.

---

## 1. Herramientas disponibles

### Compilación
- **`build.bat`** (x86): `cmd //c build.bat`
- **`build.bat x64`**: versión 64 bits
- Salida: `bin\winuae-gdb.exe`

### Logs y flags de depuración
- **`-winmouselog`**: activa `log_winmouse`, escribe en consola cada WM_MOUSEMOVE y botones (mx, my, mouseposx/y, focus, etc.)
- **`-norawinput_mouse`**: desactiva RawInput para el ratón (útil para aislar fuentes)
- **`-nowindowsmouse`**: desactiva el ratón “Windows” (solo DirectInput/RawInput)
- **`win32_logfile=yes`** en .uae: genera `winuaelog.txt` con `write_log`

### MCP y extensión
- **MCP winuae-emu**: `winuae_connect`, `winuae_load`, `winuae_screenshot`, `winuae_input_mouse`, etc.
- **Extensión vscode-amiga-debug**: debugging con breakpoints y depurador

---

## 2. Depuración de entrada de ratón

### Problema: cursor que no coincide o se mueve solo

**Posible causa: varias fuentes de ratón a la vez**

El ratón puede alimentarse desde:
1. **WM_MOUSEMOVE** (win32.cpp) – coordenadas de ventana
2. **RawInput WM_INPUT** (dinput.cpp) – deltas relativos
3. **DirectInput GetDeviceData** (dinput.cpp) – deltas por dispositivo

Si varias rutas llaman a `setmousestate()` para los mismos ejes (0=X, 1=Y), los deltas se suman y el cursor se descontrola.

**Estrategia de aislamiento**

1. Probar con `-norawinput_mouse` para descartar RawInput.
2. Añadir en los puntos donde se llama `setmousestate` para ejes 0 y 1:
   ```c
   if (log_winmouse)
       write_log(_T("setmousestate src=X axis=%d data=%d isabs=%d\n"), axis, data, isabs);
   ```
   (sustituir X por un identificador de la fuente).
3. Confirmar que cuando se usa un modo “exclusivo” (p. ej. `win32_absolute_mouse`), las otras rutas no envían datos para esos ejes.

### Problema: coordenadas incorrectas

**Comprobar sistemas de coordenadas**

- `lParam` de WM_MOUSEMOVE: coordenadas **cliente** de la ventana.
- `amigawinclip_rect`: rectángulo en coordenadas de **pantalla** (GetWindowRect).
- Usar `ClientToScreen()` para pasar de cliente a pantalla antes de comparar con `amigawinclip_rect`.

**Verificación rápida**

```c
write_log(_T("clip=%d,%d-%d,%d pt=%d,%d -> ax=%d ay=%d\n"),
    clip->left, clip->top, clip->right, clip->bottom,
    pt.x, pt.y, ax, ay);
```

---

## 3. Flujo de depuración recomendado

1. **Reproducir** el fallo de forma consistente.
2. **Hipótesis**: identificar posibles fuentes (WM_*, RawInput, DirectInput).
3. **Aislar**: usar flags (`-norawinput_mouse`, etc.) para desactivar rutas y ver si el fallo desaparece.
4. **Instrumentar**: `write_log` o `log_winmouse` en los puntos críticos.
5. **Compilar y probar**: `build.bat` y ejecutar con los flags de log.
6. **Iterar** hasta localizar la ruta y el dato erróneo.

---

## 4. Checklist para cambios en ratón/input

- [ ] ¿Hay más de una ruta que llame a `setmousestate` para los mismos ejes?
- [ ] ¿Se debe deshabilitar RawInput/DirectInput cuando el nuevo modo esté activo?
- [ ] ¿Las coordenadas están en el sistema correcto (cliente vs pantalla)?
- [ ] ¿Se necesita escalado entre resolución de ventana y resolución Amiga?
- [ ] Probar con `-winmouselog` y revisar salida en consola.
- [ ] Probar con `-norawinput_mouse` para comprobar comportamiento sin RawInput.

---

## 5. Caso práctico: ratón absoluto

**Síntoma**: Cursor del Amiga se desplaza muy rápido y sale de la pantalla.

**Causa**: RawInput y WM_MOUSEMOVE enviaban datos a la vez. WM_MOUSEMOVE mandaba posiciones absolutas; RawInput enviaba deltas relativos. Al sumarse ambos, el cursor se descontrolaba.

**Solución 1**: En `dinput.cpp`, no enviar ejes 0 y 1 desde RawInput ni DirectInput cuando `currprefs.win32_absolute_mouse` está activo.

**Solución 2**: En `inputdevice.cpp` `setmousestate()`, no resetear `*oldm_p = 0` cuando `isabs=1`, para que la siguiente llamada compute `d = data - previous` correctamente. Antes se ponía `old_axis = 0` tras cada evento, provocando que `d = data - 0` = posición absoluta sumada como delta.

**Lección**: Con modos de ratón nuevos o exclusivos, hay que evitar que otras fuentes sigan alimentando los mismos ejes.

---

## 6. Test del ratón absoluto

Después de compilar, verificar manualmente:

1. `bin\winuae-gdb.exe` (default: absolute mouse)
2. Arrastrar el cursor de Windows sobre el área de pantalla del Amiga.
3. El cursor del Amiga debe coincidir 1:1 con el de Windows (sin desbordarse ni sumar offsets).

Si falla: usar `-winmouselog` para ver WM_MOUSEMOVE; `-noabsolute_mouse` para volver al modo relativo.

Script: `powershell -File scripts\test-mouse-absolute.ps1`

---

## 7. Referencias

- `doc/MOUSE-SYSTEMS.md`: descripción de los sistemas de ratón.
- `od-win32/win32.cpp`: WM_MOUSEMOVE, manejadores de botones.
- `od-win32/dinput.cpp`: RawInput (RIM_TYPEMOUSE), DirectInput (GetDeviceData).
- `inputdevice.cpp`: `setmousestate` (no resetear `old_axis` cuando `isabs=1`), `mouse_delta`, `joymousecounter`.
