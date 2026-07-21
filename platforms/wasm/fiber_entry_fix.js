// Emscripten's built-in Fibers.finishContextSwitch() invokes a newly-created
// fiber's entry point via {{{ makeDynCall('vp', 'entryPoint') }}}, which
// expands (under the DYNCALLS legacy mode that ASYNCIFY forces on) to a call
// to the wasm-exported `dynCall_vi` function (the 'p' pointer-arg signature
// character normalizes to 'i' on wasm32) -- but only if Emscripten's own
// build-time bookkeeping (WASM_EXPORTS, populated from what it explicitly
// asked wasm-emscripten-finalize to export) believes that symbol will be
// exported. Under MAIN_MODULE it doesn't believe that, so it emits a hard
// abort() in its place:
//   "Internal Error! Attempted to invoke wasm function pointer with
//    signature 'vi', but no such functions have gotten exported!"
//
// However, `dynCall_vi` genuinely *is* present in the compiled module --
// confirmed via `wasm-dis partyboard.wasm`, it shows up as a real export --
// because the extra raw `--export-all --no-gc-sections` linker flags (added
// separately to fix other MAIN_MODULE export-stripping issues, see
// CMakeLists.txt) preserve it too. Emscripten's JS-generation step just has
// no visibility into those flags, so it never learns the symbol survived.
//
// The fix: patch Fibers.finishContextSwitch so the entryPoint!==0 branch
// calls the real, already-Asyncify-instrumented `dynCall_vi` wasm export
// directly (via wasmExports, since it isn't in Module's own runtime-exports
// allowlist) instead of hitting the abort(). This is exactly what a stock
// non-MAIN_MODULE build does at this call site, so it already has correct
// Asyncify unwind/rewind bookkeeping (including for a nested fiber swap
// performed from inside the entry point itself, e.g. an immediate
// HuPrcVSleep) -- no manual Asyncify.state/exportCallStack surgery needed.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  var origFinish = Fibers.finishContextSwitch.bind(Fibers);
  Fibers.finishContextSwitch = function (newFiber) {
    var entryPoint = HEAPU32[((newFiber + 12) >> 2)];
    if (entryPoint !== 0) {
      var stack_base = HEAPU32[(newFiber >> 2)];
      var stack_max = HEAPU32[((newFiber + 4) >> 2)];
      _emscripten_stack_set_limits(stack_base, stack_max);
      stackRestore(HEAPU32[((newFiber + 8) >> 2)]);
      writeStackCookie();
      Asyncify.currData = null;
      HEAPU32[((newFiber + 12) >> 2)] = 0;
      var userData = HEAPU32[((newFiber + 16) >> 2)];
      wasmExports['dynCall_vi'](entryPoint, userData);
      return;
    }
    origFinish(newFiber);
  };
});
