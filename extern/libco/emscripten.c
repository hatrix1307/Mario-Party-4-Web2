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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CO_ASYNCIFY_STACK_SIZE (256 * 1024)
/* Callers size their stack requests for native calling conventions (e.g.
   HuPrcCreate's 16KB default); wasm's calling convention and Asyncify's
   unwind/rewind machinery need more headroom than that per call frame. */
#define CO_MIN_C_STACK_SIZE (512 * 1024)

/* Set to 1 to trace fiber create/entry/switch to the console -- useful for
   debugging Asyncify+fiber resume issues (e.g. the "function signature
   mismatch" trap seen entering TutorialProcFunc/minigame setup from a child
   process created via HuPrcChildCreate: see the investigation notes in this
   session's history). create/entrypoint alone are low-frequency (once per
   process lifetime); CO_TRACE_SWITCH fires every co_switch (i.e. every
   process, every frame) and will flood the console -- only turn it on when
   actively narrowing down a specific repro. */
#ifndef CO_TRACE
#define CO_TRACE 1
#endif
#ifndef CO_TRACE_SWITCH
#define CO_TRACE_SWITCH 0
#endif

typedef struct {
  emscripten_fiber_t fiber;
  void (*coentry)(void);
  void* c_stack;      /* 16-byte-aligned pointer actually handed to emscripten_fiber_init */
  void* c_stack_raw;  /* the pointer malloc() actually returned, for freeing */
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
#if CO_TRACE
  printf("[co_entrypoint] thread=%p coentry=%p\n", (void*)thread, (void*)thread->coentry);
#endif
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
  /* emscripten_fiber_init() sets the fiber's initial stack pointer to
     exactly (c_stack + size) with no alignment rounding of its own (see
     system/lib/libc/emscripten_fiber.c) -- wasm's calling convention
     assumes SP stays 16-byte aligned throughout, but malloc() isn't
     guaranteed to return a 16-byte-aligned pointer, so an unlucky
     allocation leaves *every* stack pointer value computed for the rest of
     that fiber's life off by the same misalignment. That's silently
     harmless most of the time, but trips 16-byte-alignment ASSERTIONS
     checks the first time this fiber happens to reach one (e.g. EM_ASM's
     readEmAsmArgs: `assert(buf % 16 == 0)`). Round the size down and the
     base pointer up to 16 bytes so their sum -- the initial stack
     pointer -- lands on a 16-byte boundary regardless of malloc's own
     alignment.
   */
  size &= ~(unsigned int)15;

  thread->coentry = coentry;
  thread->c_stack_raw = LIBCO_MALLOC(size + 16);
  thread->c_stack = (void*)(((uintptr_t)thread->c_stack_raw + 15) & ~(uintptr_t)15);
  thread->asyncify_stack = LIBCO_MALLOC(CO_ASYNCIFY_STACK_SIZE);
  if (!thread->c_stack_raw || !thread->asyncify_stack) {
    LIBCO_FREE(thread->c_stack_raw);
    LIBCO_FREE(thread->asyncify_stack);
    LIBCO_FREE(thread);
    return 0;
  }

  emscripten_fiber_init(&thread->fiber, co_entrypoint, thread, thread->c_stack, size, thread->asyncify_stack,
                         CO_ASYNCIFY_STACK_SIZE);

#if CO_TRACE
  printf("[co_create] thread=%p coentry=%p size=%u active=%p\n", (void*)thread, (void*)coentry, size,
         (void*)co_running);
#endif
  return (cothread_t)thread;
}

void co_delete(cothread_t handle) {
  cothread_struct* thread = (cothread_struct*)handle;
  if (thread && thread != &co_primary) {
    LIBCO_FREE(thread->c_stack_raw);
    LIBCO_FREE(thread->asyncify_stack);
    LIBCO_FREE(thread);
  }
}

void co_switch(cothread_t handle) {
  cothread_struct* old_thread = co_running;
  cothread_struct* new_thread = (cothread_struct*)handle;
#if CO_TRACE_SWITCH
  printf("[co_switch] %p -> %p (new coentry=%p)\n", (void*)old_thread, (void*)new_thread,
         (void*)new_thread->coentry);
#endif
  co_running = new_thread;
  emscripten_fiber_swap(&old_thread->fiber, &new_thread->fiber);
#if CO_TRACE_SWITCH
  printf("[co_switch] returned to %p\n", (void*)old_thread);
#endif
}

int co_serializable(void) {
  return 0;
}

#ifdef __cplusplus
}
#endif
