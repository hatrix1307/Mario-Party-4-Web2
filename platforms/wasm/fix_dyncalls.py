#!/usr/bin/env python3
"""
Post-build fixup for a MAIN_MODULE + ASYNCIFY build.

Emscripten's JS glue generation decides, per call site, whether a given
dynCall_<sig> wrapper will be available at runtime by checking its own
build-time bookkeeping (WASM_EXPORTS, populated from what it explicitly
asked wasm-emscripten-finalize to export). Under MAIN_MODULE it doesn't
believe most dynCall_<sig> wrappers survive, so any code path that needs
one (browser DOM event callbacks, in this project's case) gets a
hardcoded abort() in its place:

  "Internal Error! Attempted to invoke wasm function pointer with
   signature '<sig>', but no such functions have gotten exported!"

In practice the wrapper *does* survive, because this project's extra raw
wasm-ld flags (--export-all --no-gc-sections, see CMakeLists.txt) keep
and export literally everything, including these synthesized dynCall_<sig>
stubs -- Emscripten's JS-generation step just has no visibility into
those flags, so it never learns they're actually there.

This only handles the "iiii" signature (emscripten/src/lib/libhtml5.js's
`{{{ makeDynCall('iipp', 'callbackfunc') }}}(eventTypeId, event, userData)`
pattern used by every DOM event registration: keydown/up, mouse, wheel,
focus, touch, gamepad, fullscreen/pointerlock/visibility change, device
orientation/motion, battery), which is the one actually observed
aborting at runtime (keyboard input). Emscripten's generated abort()
closure takes only the event's own args -- eventTypeId, event pointer,
userData -- and drops the callback function pointer itself, since
makeDynCall() normally bakes that in as dynCall_<sig>'s leading argument
(`dynCall_iiii(funcPtr, a1, a2, a3)`) and the abort() path never needed
to reference it. A blind "replace abort with dynCall_iiii" substitution
therefore calls with one argument short -- everything shifts down by one
and the wasm-level call_indirect ends up dispatching through the wrong
table slot, trapping with "function signature mismatch" instead of
delivering the event. This script reconstructs the correct funcPtr
argument instead of dropping it.

Every affected libhtml5.js call site uses the closure variable
`callbackfunc`, except the four canvas-resize ones, which use
`strategy.canvasResizedCallback` / `currentFullscreenStrategy.
canvasResizedCallback` -- recoverable from the trailing
`...CallbackUserData` argument already present at the call site.

Other signatures (v, vi, vii, viii, viiii, ii, iii, ...) are deliberately
left untouched: they're emitted by several *different* library files
(libeventloop.js, libdylink.js, libcore.js, ...) with different funcPtr
conventions apiece that haven't been individually verified here, and a
wrong guess produces the same kind of silent wrong-argument trap this
script exists to fix. Left as abort(), they fail loudly instead if any
of those paths turn out to be exercised.
"""
import re
import subprocess
import sys

def main():
    if len(sys.argv) != 4:
        print("usage: fix_dyncalls.py <wasm-dis> <partyboard.wasm> <partyboard.js>", file=sys.stderr)
        return 1
    wasm_dis, wasm_path, js_path = sys.argv[1:4]

    wat = subprocess.run([wasm_dis, wasm_path], capture_output=True, text=True, check=True).stdout
    exported_sigs = set(re.findall(r'\(export "dynCall_([a-z0-9]+)" ', wat))

    with open(js_path, "r", encoding="utf-8") as f:
        js = f.read()

    if "iiii" not in exported_sigs:
        print("fix_dyncalls: dynCall_iiii not exported, nothing to do")
        return 0

    # Captures the lambda's own params (a1, a2, a3) and the trailing args it
    # gets immediately invoked with, e.g. "(eventTypeId, keyEventData, userData)".
    # No nested parens appear in any observed call site's argument list.
    pattern = re.compile(
        r"\(\(a1, a2, a3\) => abort\("
        r"'Internal Error! Attempted to invoke wasm function pointer with "
        r"signature \"iiii\", but no such functions have gotten exported!'"
        r"\)\)\(([^()]+)\)"
    )

    count = 0

    def repl(m):
        nonlocal count
        args = m.group(1)
        last_arg = args.rsplit(",", 1)[-1].strip()
        if last_arg.endswith("CallbackUserData"):
            func_ptr = last_arg[: -len("UserData")]
        else:
            func_ptr = "callbackfunc"
        count += 1
        return "wasmExports['dynCall_iiii'](%s, %s)" % (func_ptr, args)

    new_js = pattern.sub(repl, js)

    if new_js != js:
        with open(js_path, "w", encoding="utf-8") as f:
            f.write(new_js)

    print("fix_dyncalls: patched %d dynCall_iiii call site(s)" % count)
    return 0

if __name__ == "__main__":
    sys.exit(main())
