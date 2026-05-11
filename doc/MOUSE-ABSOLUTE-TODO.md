# Ratón absoluto: problemas conocidos y trabajo pendiente

Este documento recopila todo lo observado durante el desarrollo del modo ratón absoluto (`win32_absolute_mouse`) para retomarlo en el futuro. **Las correcciones intentadas empeoraron el comportamiento y fueron revertidas.**

---

## 1. Síntomas reportados

| Síntoma | Descripción | Estado |
|---------|-------------|--------|
| Deriva del cursor hacia la derecha | A la izquierda mapea bien; hacia la derecha el cursor del Amiga va más allá del de Windows (desajuste progresivo). | Sin resolver |
| Captura al pasar el ratón | La primera vez que el ratón entra en WinUAE lo captura sin pulsar botón; queda atrapado hasta F12 o botón central. | Sin resolver |
| Ratón atrapado en línea superior | Tras activar `win32_absolute_mouse` + `absolute_mouse=mousehack`: al iniciar, el cursor queda atrapado en una línea horizontal (solo mueve izq-der). Hay que liberar con F12/central para operar. | Sin resolver |
| Ambos botones no funcionan | Demo de Cursor-Amiga-C sale con `engine_mouse_left() && engine_mouse_right()`; ese evento nunca llega aunque se pulsen ambos botones. | Sin resolver |
| No adapta al tamaño de ventana | Tras redimensionar la ventana, el mapeo del cursor deja de ser correcto. | Sin resolver |
| Mousehack no visible en menú | No se encuentra "Mousehack" explícito; en .uae: `absolute_mouse=mousehack`. En GUI: Input → Game ports → Port 1 → Tablet/Tablet mode. | Documentado |

---

## 2. Cambios intentados (revertidos)

### 2.1 Escalado con inwidth/inheight

**Archivo:** `od-win32/win32.cpp` (bloque WM_MOUSEMOVE, win32_absolute_mouse)

**Qué se hizo:** Usar `inwidth`/`inheight` en lugar de `outwidth`/`outheight` para reducir la deriva por distinta relación de aspecto ventana vs pantalla Amiga.

**Resultado:** Empeoró. Revertido a `outwidth`/`outheight`.

### 2.2 Actualizar amigawinclip_rect en WM_SIZE

**Archivo:** `od-win32/win32.cpp` (case WM_SIZE)

**Qué se hizo:** Llamar `updatewinrect()` y `updatemouseclip()` cuando se recibe WM_SIZE (hAmigaWnd o hMainWnd).

**Hipótesis del fallo:** WM_SIZE puede llegar antes de que la ventana tenga tamaño real. Si `amigawinclip_rect` se actualiza con un rectángulo inválido (ej. altura ≈ 0), el siguiente `ClipCursor()` deja el cursor atrapado en una línea.

**Resultado:** Revertido.

### 2.3 Evitar enablecapture en recapture con win32_absolute_mouse

**Archivo:** `od-win32/win32.cpp` (WM_MOUSEMOVE, bloque recapture)

**Qué se hizo:** Condición `!currprefs.win32_absolute_mouse` antes de `enablecapture()` en el camino de recapture.

**Resultado:** Revertido (se mantuvo la lógica original).

### 2.4 Enviar estado completo de botones (GetAsyncKeyState)

**Archivo:** `od-win32/win32.cpp` (WM_LBUTTONDOWN, WM_RBUTTONDOWN)

**Qué se hizo:** En lugar de solo `setmousebuttonstate(m, 0, 1)` en LBUTTONDOWN, enviar el estado completo con `GetAsyncKeyState(VK_LBUTTON/VK_RBUTTON/VK_MBUTTON)` para que ambos botones se detecten al pulsarlos a la vez.

**Resultado:** No mejoró; posiblemente empeoró. Revertido a lógica simple por botón.

### 2.5 Añadir opciones al .uae de depuración

**Archivo:** `Cursor-Amiga-C/.vscode/mcp-amiga-debug.uae`

**Qué se hizo:** `win32.absolute_mouse=yes` y `absolute_mouse=mousehack`.

**Resultado:** Revertido porque activaba captura al inicio y cursor atrapado en la línea superior.

---

## 3. Hipótesis para futura investigación

### 3.1 Captura al iniciar / cursor en línea superior

- **Causa posible:** Con `win32_absolute_mouse` + `absolute_mouse=mousehack`, algo dispara la captura antes de que la ventana esté completamente inicializada.
- **amigawinclip_rect:** Si se usa un rectángulo con altura 0 o muy pequeña en `ClipCursor()`, el cursor solo puede moverse horizontalmente.
- **Dónde mirar:**
  - Origen de la captura al arranque (quizá `setmouseactive(1)` por WM_SETFOCUS u otro evento).
  - Momento de la primera `updatemouseclip()` / `ClipCursor()`.
  - Validez de `amigawinclip_rect` cuando se llama a `ClipCursor()` (sobre todo left, right, top, bottom).
  - Diferencia entre `use_gui=no` y con GUI (estructura de ventanas y orden de mensajes).

### 3.2 Deriva hacia la derecha

- **Causa posible:** La relación de aspecto de la ventana no coincide con la pantalla Amiga. Letterboxing/pillarboxing harían que el área visible no ocupe toda la ventana.
- **Dónde mirar:**
  - Si `amigawinclip_rect` cubre toda la ventana o solo la zona de dibujo real.
  - Si existe un rectángulo de “display visible” distinto (en `win32gfx.cpp` hay lógica de centrado).
  - Probar `inwidth`/`inheight` vs `outwidth`/`outheight` con logging para ver cuál se ajusta mejor al área dibujada.

### 3.3 Ambos botones simultáneos

- **Contexto:** Cursor-Amiga-C usa `input.device` (IND_ADDHANDLER). La salida de la demo depende de `engine_mouse_left() && engine_mouse_right()`.
- **Causas posibles:**
  - Los mensajes WM_LBUTTONDOWN y WM_RBUTTONDOWN llegan en instantes distintos; el Amiga puede hacer polling entre ambos.
  - `input.device` podría esperar ambos botones en un mismo evento o en una misma “vuelta” de polling.
  - El dispositivo de ratón usado en `setmousebuttonstate()` (dinput_winmouse vs 0) podría no ser el mismo que lee `input.device`.
- **Dónde mirar:**
  - `input_devices.c` en Cursor-Amiga-C para ver cómo se lee el estado de botones.
  - `setmousebuttonstate` en WinUAE y su mapeo a input.device / POTGOR.
  - Si hay una forma de enviar un “batch” de botones en un solo ciclo de input.

### 3.4 Redimensionado de ventana

- **Causa posible:** `amigawinclip_rect` no se actualiza correctamente tras WM_SIZE, o hay desfase temporal.
- **Dónde mirar:**
  - WM_WINDOWPOSCHANGED ya llama a `updatewinrect()` y `updatemouseclip()`. Comprobar que WM_SIZE no se procese antes con valores incorrectos.
  - Considerar actualizar el clip solo cuando el tamaño sea válido (w>0, h>0) y la ventana esté completamente visible.

---

## 4. Archivos relevantes

| Archivo | Secciones clave |
|---------|-----------------|
| `od-win32/win32.cpp` | WM_MOUSEMOVE (línea ~2617), WM_*BUTTONDOWN (~2289), recapture (~2653), updatewinrect, updatemouseclip |
| `od-win32/win32.cpp` | setmouseactive, enablecapture, disablecapture |
| `od-win32/win32gfx.cpp` | amigawinclip_rect, amigawin_rect |
| `inputdevice.cpp` | setmousestate (isabs=1), setmousebuttonstate |
| `od-win32/dinput.cpp` | Bloqueo de RawInput/DirectInput cuando win32_absolute_mouse |
| `Cursor-Amiga-C/engine/src/input_devices.c` | engine_input_devices_mouse_left/right |
| `Cursor-Amiga-C/engine/src/input.c` | engine_mouse_left/right |
| [MOUSE-SYSTEMS.md](MOUSE-SYSTEMS.md) | Arquitectura general del sistema de ratón |

---

## 5. Estado actual del código (post‑reversión)

- Modo ratón absoluto **no** está activado en `mcp-amiga-debug.uae`.
- Lógica en `win32.cpp` vuelta a la versión anterior.
- El script `scripts/test-mouse-absolute.ps1` sigue sirviendo para pruebas manuales con `-absolute_mouse` / `-noabsolute_mouse`.

---

## 6. Cómo probar sin romper el flujo normal

1. Usar `mcp-amiga-debug.uae` sin `win32.absolute_mouse` ni `absolute_mouse=mousehack`.
2. Probar el modo absoluto solo con parámetros de línea de comandos:
   ```
   winuae-gdb.exe -absolute_mouse config.uae
   ```
3. Para depurar eventos de ratón: `-winmouselog` en la línea de comandos.
