// Minimal AI (Audio Interface) DMA hardware shim. There's no real AI DMA IRQ
// on this port -- msmSysInit() (src/msm/msmsys.c) unconditionally calls
// AIInit/AIRegisterDMACallback to register msmSysServer, which does periodic
// MSM-layer maintenance (fade-out completion, stream housekeeping) that on
// real hardware runs once per AI DMA interrupt. Since --no-gc-sections keeps
// that call site linked in regardless of ARAM/streaming being unimplemented,
// AIInit/AIRegisterDMACallback need to exist -- and to keep that periodic
// maintenance actually running, AITick() (called once per mixer quantum from
// extern/musyx/src/musyx/runtime/hw_pc.c) invokes the registered callback
// instead of leaving it a dead letter.
#include <dolphin/ai.h>
#include <stddef.h>

static void NoopCallback(void) {}

static AIDCallback sCallback = NoopCallback;

void AIInit(u8 *stack) {
    (void)stack;
}

void AIInitDMA(u32 start_addr, u32 length) {
    (void)start_addr;
    (void)length;
}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    // Callers (e.g. msmSysServer, src/msm/msmsys.c) chain to the callback
    // this returns unconditionally, with no NULL check -- always hand back a
    // callable so that chain can't end in a null function-pointer call.
    AIDCallback old = sCallback;
    sCallback = (callback != NULL) ? callback : NoopCallback;
    return old;
}

void AITick(void) {
    sCallback();
}
