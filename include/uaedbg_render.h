/*
 * uaedbg_render.h - Toggles de render del debugger (canal lateral).
 *
 * Permiten apagar canales de DMA (planos/sprites/blitter/copper) y cegar un rango de
 * lineas del Copper (sus escrituras de registro se ignoran en ese rango), sin recompilar
 * la aplicacion. Los usa el comando `render` del canal lateral (barto_gdbserver.cpp) y los
 * consultan custom.cpp (handler de DMACON y escrituras del Copper).
 *
 * `uaedbg_render_off_mask`: bits DMA_* (DMA_SPRITE/DMA_BLITTER/DMA_COPPER/DMA_BITPLANE)
 *   que se fuerzan a 0 en cualquier escritura de DMACON (CPU o Copper): el canal no puede
 *   quedar encendido mientras el bit este en la mascara.
 * `uaedbg_render_cop_line_from/to`: rango inclusivo de lineas (vpos) en el que el Copper
 *   ejecuta su lista pero sus escrituras de registro se descartan ("copper cegado" en ese
 *   tramo). -1 en `from` = sin rango.
 */
#ifndef UAEDBG_RENDER_H
#define UAEDBG_RENDER_H

#include "sysconfig.h"
#include "sysdeps.h"

extern uae_u16 uaedbg_render_off_mask;
extern int uaedbg_render_cop_line_from;
extern int uaedbg_render_cop_line_to;

/* Aplica la mascara al registro DMACON vivo (llamar desde el hilo de emulacion).
   `newly_enabled`: bits que acaban de salir de la mascara; se re-habilitan en DMACON
   (si no, apagar p. ej. COPEN deja el Copper parado y no vuelve solo). */
void uaedbg_render_apply(uae_u16 newly_enabled);

#endif
