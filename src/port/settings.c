#include "dolphin.h"
#include "game/disp.h"
#include "game/pad.h"

#ifdef EMSCRIPTEN
#include <emscripten/em_macros.h>

// See disp.h -- this is what Hu3DCameraCreate() (hsfman.c) feeds into new
// cameras' aspect field, and what the handful of other direct HU_DISP_ASPECT
// call sites (hsfdraw.c, hsfex.c, board/main.c) now read instead of the
// compile-time macro.
static f32 sDispAspect = HU_DISP_ASPECT;

EMSCRIPTEN_KEEPALIVE f32 HuDispAspectGet(void)
{
    return sDispAspect;
}

EMSCRIPTEN_KEEPALIVE void HuDispAspectSetWide(s32 wide)
{
    sDispAspect = wide ? (16.0f / 9.0f) : HU_DISP_ASPECT;
}

// Thin flat-argument wrappers around PADSetKeyButtonBinding/PADSetKeyAxisBinding
// for the settings UI to call from JS -- calling those directly would mean
// getting struct-by-value wasm ABI flattening right from JS, which is more
// fragile than just exporting a couple of plain-int functions.
EMSCRIPTEN_KEEPALIVE BOOL SettingsSetKeyButtonBinding(u32 port, u16 padButton, s32 scancode)
{
    PADKeyButtonBinding binding;
    binding.scancode = scancode;
    binding.padButton = padButton;
    return PADSetKeyButtonBinding(port, binding);
}

EMSCRIPTEN_KEEPALIVE BOOL SettingsSetKeyAxisBinding(u32 port, u16 padAxis, s32 scancode)
{
    PADKeyAxisBinding binding;
    binding.scancode = scancode;
    binding.padAxis = padAxis;
    binding.influence = 0;
    return PADSetKeyAxisBinding(port, binding);
}
#endif
