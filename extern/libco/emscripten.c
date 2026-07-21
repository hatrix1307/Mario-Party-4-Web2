/*
  libco backend for Emscripten/WebAssembly.

  wasm has no manipulable machine stack pointer, so the generic sjlj.c
  backend (which forges a jmp_buf stack pointer) silently fails to switch
  contexts under Emscripten. This backend uses Emscripten's own fiber API
  (emscripten/fiber.h), which implements real stack switching on top of
  Asyncify and is the supported way to do cooperative context switching
  on this target.
*/

#define LIBCO_C
#include "libco.h"
#include "settings.h"

#include <emscripten/fiber.h>
#include <emscripten/emscripten.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CO_ASYNCIFY_STACK_SIZE (128 * 1024)
/* Callers size their stack requests for native calling conventions (e.g.
   HuPrcCreate's 16KB default); wasm's calling convention and Asyncify's
   unwind/rewind machinery need more headroom than that per call frame. */
#define CO_MIN_C_STACK_SIZE (256 * 1024)

typedef struct {
  emscripten_fiber_t fiber;
  void (*coentry)(void);
  void* c_stack;
  void* asyncify_stack;
} cothread_struct;

static thread_local cothread_struct co_primary;
static thread_local cothread_struct* co_running = 0;
static thread_local int co_primary_ready = 0;

static void co_ensure_primary(void) {
  if (!co_primary_ready) {
    co_primary.asyncify_stack = LIBCO_MALLOC(CO_ASYNCIFY_STACK_SIZE);
    LIBCO_ASSERT(co_primary.asyncify_stack);
    emscripten_fiber_init_from_current_context(&co_primary.fiber, co_primary.asyncify_stack, CO_ASYNCIFY_STACK_SIZE);
    co_running = &co_primary;
    co_primary_ready = 1;
  }
}

EMSCRIPTEN_KEEPALIVE
void co_entrypoint(void* arg) {
  cothread_struct* thread = (cothread_struct*)arg;
  thread->coentry();
  /* libco threads are not expected to return; matches other backends' behavior of
     leaving execution undefined if they do. */
  LIBCO_ASSERT(0 && "co_entrypoint: coroutine function returned");
  for (;;) { }
}

cothread_t co_active(void) {
  co_ensure_primary();
  return (cothread_t)co_running;
}

cothread_t co_derive(void* memory, unsigned int size, void (*coentry)(void)) {
  (void)memory;
  (void)size;
  (void)coentry;
  LIBCO_ASSERT(0 && "co_derive is not supported by the emscripten libco backend");
  return 0;
}

cothread_t co_create(unsigned int size, void (*coentry)(void)) {
  co_ensure_primary();

  cothread_struct* thread = (cothread_struct*)LIBCO_MALLOC(sizeof(cothread_struct));
  if (!thread) {
    return 0;
  }

  if (size < CO_MIN_C_STACK_SIZE) {
    size = CO_MIN_C_STACK_SIZE;
  }

  thread->coentry = coentry;
  thread->c_stack = LIBCO_MALLOC(size);
  thread->asyncify_stack = LIBCO_MALLOC(CO_ASYNCIFY_STACK_SIZE);
  if (!thread->c_stack || !thread->asyncify_stack) {
    LIBCO_FREE(thread->c_stack);
    LIBCO_FREE(thread->asyncify_stack);
    LIBCO_FREE(thread);
    return 0;
  }

  emscripten_fiber_init(&thread->fiber, co_entrypoint, thread, thread->c_stack, size, thread->asyncify_stack,
                         CO_ASYNCIFY_STACK_SIZE);

  return (cothread_t)thread;
}

void co_delete(cothread_t handle) {
  cothread_struct* thread = (cothread_struct*)handle;
  if (thread && thread != &co_primary) {
    LIBCO_FREE(thread->c_stack);
    LIBCO_FREE(thread->asyncify_stack);
    LIBCO_FREE(thread);
  }
}

void co_switch(cothread_t handle) {
  cothread_struct* old_thread = co_running;
  cothread_struct* new_thread = (cothread_struct*)handle;
  co_running = new_thread;
  emscripten_fiber_swap(&old_thread->fiber, &new_thread->fiber);
}

int co_serializable(void) {
  return 0;
}

#ifdef __cplusplus
}
#endif
