#ifndef _GAME_DISP_H
#define _GAME_DISP_H

#include "types.h"

// Runtime override for the compile-time HU_DISP_ASPECT below, used by the
// 3D camera perspective code (Hu3DCameraCreate/Hu3DCameraPerspectiveSet
// call sites) so a widescreen setting can widen the actual camera FOV
// instead of just stretching a 4:3 image. HU_DISP_WIDTH/HU_DISP_CENTERX
// etc. below are deliberately left as compile-time constants -- they're
// used for 2D HUD/menu screen-space positioning, which doesn't have a
// runtime-widescreen-aware layout, so changing them would shift 2D
// elements around rather than widen the 3D view.
f32 HuDispAspectGet(void);
void HuDispAspectSetWide(s32 wide);

#define HU_DISP_WIDTH 576
#define HU_DISP_WIDTHF ((float)HU_DISP_WIDTH)
#define HU_DISP_HEIGHT 480
#define HU_DISP_HEIGHTF ((float)HU_DISP_HEIGHT)
#define HU_DISP_ASPECT (HU_DISP_WIDTHF/HU_DISP_HEIGHTF)

#define HU_DISP_CENTERXI (HU_DISP_WIDTH/2)
#define HU_DISP_CENTERX (HU_DISP_WIDTHF/2)
#define HU_DISP_CENTERYI (HU_DISP_HEIGHT/2)
#define HU_DISP_CENTERY (HU_DISP_HEIGHTF/2)

#define HU_FB_WIDTH 640
#define HU_FB_WIDTHF ((float)HU_FB_WIDTH)
#define HU_FB_HEIGHT 480
#define HU_FB_HEIGHTF ((float)HU_FB_HEIGHT)

#define HU_FB_CENTERXI (HU_FB_WIDTH/2)
#define HU_FB_CENTERX (HU_FB_WIDTHF/2)
#define HU_FB_CENTERYI (HU_FB_HEIGHT/2)
#define HU_FB_CENTERY (HU_FB_HEIGHTF/2)

#endif
