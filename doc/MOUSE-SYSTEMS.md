# Sistemas de gestión del ratón en WinUAE-DBG

Este documento describe el funcionamiento del sistema nativo de gestión del ratón, el nuevo sistema alternativo (ratón absoluto) y cómo se virtualiza el hardware del ratón para que el Amiga reciba los datos como si provinieran de un ratón físico real.

> **Problemas conocidos y trabajo pendiente:** Ver [MOUSE-ABSOLUTE-TODO.md](MOUSE-ABSOLUTE-TODO.md) para síntomas, intentos de corrección (revertidos) e hipótesis para futura investigación.

---

## 1. Sistema nativo (relativo)

### Fuentes de entrada

El ratón de Windows puede llegar por dos vías:

- **WM_MOUSEMOVE / WM_*BUTTONDOWN**: mensajes de la ventana de Amiga (`od-win32/win32.cpp`).
- **RawInput (WM_INPUT) / DirectInput**: capa de bajo nivel en `od-win32/dinput.cpp` cuando está habilitada.

### Flujo del sistema nativo

1. **Coordenadas relativas**  
   El sistema nativo trabaja con *deltas* (desplazamientos), no con posiciones absolutas:
   - `mx`, `my` del `lParam` de WM_MOUSEMOVE se interpretan respecto al *centro* del área de pantalla Amiga (`amigawinclip_rect`).
   - Se calcula:  
     `mx = mx - centro_x`, `my = my - centro_y`.

2. **Warpeo del cursor**  
   Cuando el cursor se aleja del centro, se llama a `setcursor()`:
   - Usa `SetCursorPos()` para volver a centrar el cursor.
   - Así se pueden seguir generando deltas sin que el cursor llegue al borde de la pantalla.

3. **Captura del ratón**  
   - Se activa en `WM_LBUTTONDOWN` (clic izquierdo) sobre la ventana de Amiga.
   - `SetCapture()` captura los eventos del ratón.
   - `ClipCursor()` restringe el cursor al área `amigawinclip_rect`.

4. **Envío al emulador**  
   Se usa `setmousestate(mouse, axis, data, 0)` con `isabs=0`:
   - `data` = delta (entero con signo).
   - Se aplica `input_mouse_speed` a la magnitud del movimiento.

### Modo tablet / mousehack

Si `input_tablet >= TABLET_MOUSEHACK` está activo, el sistema usa coordenadas absolutas (`isabs=1`) en lugar de relativas. En ese caso las coordenadas provienen directamente de `lParam` (coordenadas cliente de la ventana).

---

## 2. Nuevo sistema: ratón absoluto (`win32_absolute_mouse`)

Opciones: `-absolute_mouse` / `-noabsolute_mouse`, o `absolute_mouse=yes|no` en el .uae.

**Importante**: Cuando este modo está activo, RawInput y DirectInput no envían datos para los ejes X/Y (0, 1) del ratón, para evitar que WM_MOUSEMOVE y esas rutas alimenten el mouse a la vez y provoquen movimiento errático del cursor.

### Objetivo

Hacer que la posición del cursor de Windows se corresponda punto a punto con la posición del cursor en la pantalla del Amiga, sin warpeo ni deltas.

### Mapeo de coordenadas

1. **Coordenadas de pantalla**  
   Las coordenadas cliente de WM_MOUSEMOVE (`mx`, `my`) se convierten a coordenadas de pantalla con `ClientToScreen()`.

2. **Restricción al área Amiga**  
   Si el cursor está fuera de `amigawinclip_rect`, se toma el píxel del borde más cercano.

3. **Escalado a resolución nativa del Amiga**  
   Para que el cursor del Amiga coincida 1:1 con el de Windows:
   - `ax = (sx - clip.left) * outwidth / clip_width`
   - `ay = (sy - clip.top) * outheight / clip_height`
   - `outwidth`/`outheight` vienen de `adisplays[monid].gfxvidinfo.outbuffer`.

4. **Mousehack**  
   En configuraciones nuevas se activa `input_tablet = TABLET_MOUSEHACK` cuando `win32_absolute_mouse` está activo. Para configs existentes:
   - En el .uae: `absolute_mouse=mousehack` (opción `absolute_mouse`, valor `mousehack`).
   - En la GUI de WinUAE: **Input** → **Game ports** → **Port 1** (o el puerto del ratón) → **Tablet** / **Tablet mode** → seleccionar **Mousehack**. No aparece como "Mousehack" explícito en el menú principal de Input.

5. **Envío**  
   `setmousestate(mouse, 0, ax, 1)` y `setmousestate(mouse, 1, ay, 1)` con `isabs=1`.

### Captura del ratón

- **Activación**: captura al pulsar *cualquier* botón (izquierdo, derecho, central, X1, X2) mientras el cursor está sobre la pantalla Amiga.
- **Botones**: se envía `setmousebuttonstate` tanto al capturar (primer clic) como en clics posteriores, para que los botones funcionen en Intuition/Gadgets.
- **Foco**: se llama a `SetActiveWindow()` y `SetFocus()` para dar foco a WinUAE.
- **Restricción**: `ClipCursor()` y `SetCapture()` impiden que el cursor salga del área Amiga.
- **Liberación**: se libera la captura cuando *todos* los botones están sueltos (`GetAsyncKeyState()` de VK_LBUTTON, VK_RBUTTON, etc.).

### Diferencias con el sistema nativo

| Aspecto           | Sistema nativo     | Ratón absoluto              |
|------------------|--------------------|-----------------------------|
| Tipo de datos    | Deltas relativos   | Coordenadas absolutas       |
| Warpeo           | Sí (setcursor)     | No                          |
| Captura          | Solo botón izq.    | Cualquier botón             |
| Liberación       | Varios mecanismos  | Al soltar todos los botones |

---

## 3. Virtualización del ratón para el Amiga

El Amiga espera un ratón físico que genera señales de cuadratura (fase A/B) para X e Y. El chip Paula lee esto vía JOY0DAT/JOY1DAT y POTGOR. La capa de input de UAE emula ese hardware.

### Arquitectura de la virtualización

```
Windows (eventos ratón)
        │
        ▼
┌───────────────────────────────────────┐
│  Capa host (win32.cpp / dinput.cpp)   │
│  setmousestate(mouse, axis, data,     │
│                isabs)                 │
└───────────────────┬───────────────────┘
                    │
                    ▼
┌───────────────────────────────────────┐
│  inputdevice.cpp: setmousestate()     │
│  - isabs=0: mouse_axis += data        │
│  - isabs=1: d = data - old;           │
│             mouse_axis += d           │
│  - Llama handle_input_event_extra()   │
│    con delta 'v' y flags              │
└───────────────────┬───────────────────┘
                    │
                    ▼
┌───────────────────────────────────────┐
│  handle_input_event2()                │
│  - Actualiza mouse_delta[joy][axis]   │
│    con el delta de movimiento         │
└───────────────────┬───────────────────┘
                    │
                    ▼
┌───────────────────────────────────────┐
│  inputdevice_hsync() → mouseupdate()  │
│  - getvelocity() lee mouse_delta      │
│  - mouse_x[port] += v1 (wrap 16384)   │
│  - mouse_y[port] += v2 (wrap 16384)   │
│  - joymousecounter(joy)               │
└───────────────────┬───────────────────┘
                    │
                    ▼
┌───────────────────────────────────────┐
│  joymousecounter()                    │
│  - Convierte mouse_x, mouse_y         │
│    a fases de cuadratura (2 bits/ej)  │
│  - Simula codificador incremental     │
└───────────────────┬───────────────────┘
                    │
                    ▼
┌───────────────────────────────────────┐
│  custom chip (custom.cpp)             │
│  - JOY0DAT ($DFF00A) = mouse_x|(y<<8) │
│  - JOY1DAT ($DFF00C)                  │
│  - POTGOR ($DFF016) = botones         │
└───────────────────┬───────────────────┘
                    │
                    ▼
┌───────────────────────────────────────┐
│  Amiga: input.device, Intuition       │
│  - Lee JOY0DAT/JOY1DAT en cada vsync  │
│  - Interpreta cuadratura → movimiento │
│  - Actualiza puntero en pantalla      │
└───────────────────────────────────────┘
```

### Conversión absoluto → relativo

Aunque el host envíe posiciones absolutas (`isabs=1`), internamente se convierten en deltas:

```c
// inputdevice.cpp - setmousestate(), isabs=1
d = (float)(data - *oldm_p);   // delta = nueva_pos - posición_anterior
*oldm_p = data;                // guardar posición actual
*mouse_p += (int)d;            // acumular en mouse_axis
```

Así, la capa de eventos recibe siempre deltas y el flujo hacia `mouse_delta`, `mouse_x`/`mouse_y` y `joymousecounter` es el mismo que en modo relativo.

### Formato de cuadratura (joymousecounter)

El ratón Amiga usa codificación por cuadratura para cada eje:

- 2 bits por eje (X e Y) representan la fase del encoder.
- Los 2 bits inferiores de `mouse_x` y `mouse_y` codifican esa fase (00, 01, 11, 10).
- `joymousecounter()` actualiza esas fases según el signo del movimiento (izq/der, arriba/abajo).
- La lectura de JOY0DAT/JOY1DAT devuelve el valor que el Amiga interpreta como estado del ratón físico.

### Botones del ratón

`setmousebuttonstate(mouse, button, state)` actualiza `joybutton[]`. El chip Paula lee estos estados vía POTGOR (y registros relacionados). El driver `input.device` del Amiga traduce estos bits en pulsaciones de botón (izquierdo, derecho, etc.).

### Mousehack (coordenadas absolutas en Workbench)

Si `input_tablet >= TABLET_MOUSEHACK` y hay un cliente mousehack en AmigaOS:

- `mousehack_helper()` recibe `lastmx`, `lastmy` y el estado de botones.
- El protocolo mousehack permite enviar posiciones absolutas directamente a aplicaciones compatibles (p. ej. Workbench), sin depender solo de la cuadratura.
- El ratón absoluto de WinUAE alimenta `lastmx`/`lastmy` cuando se usa `setmousestate(..., isabs=1)`.

---

## 4. Resumen

| Componente        | Función                                                                 |
|-------------------|-------------------------------------------------------------------------|
| **win32.cpp**     | Traduce WM_MOUSEMOVE y botones a `setmousestate` / `setmousebuttonstate`|
| **dinput.cpp**    | Traduce RawInput/DirectInput a las mismas funciones                     |
| **setmousestate** | Convierte absoluto→delta y alimenta el subsistema de eventos            |
| **mouse_delta**   | Almacena deltas pendientes para el siguiente frame                      |
| **mouse_x / mouse_y** | Contador de posición con wrap, usado para cuadratura                |
| **joymousecounter**   | Simula el encoder de cuadratura del ratón Amiga                     |
| **JOY0DAT/JOY1DAT**   | Registros del Paula que el Amiga lee como ratón físico             |

El Amiga ve exactamente el mismo protocolo que con un ratón real conectado al puerto del joystick/mouse: lectura periódica de JOY0DAT/JOY1DAT y POTGOR, interpretación de cuadratura para movimiento y de los bits de botón. La virtualización ocurre en la capa entre eventos Windows y estos registros.
