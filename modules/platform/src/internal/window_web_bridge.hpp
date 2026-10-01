#pragma once

#include <ludus/foundation/base/types.h>

#include <emscripten.h>

// Private foreign boundary; JS failures become explicit status. These bridges
// are embedded in the static archive, so installed consumers need no JS library.
// clang-format off
EM_JS(ludus::foundation::int32, LudusBrowserAttach,
      (ludus::foundation::uint32 token, const char* selector, ludus::foundation::uint32 limit,
       ludus::foundation::int32 capture), {
    let entry;
    try {
        const canvas = document.querySelector(UTF8ToString(selector));
        if (!(canvas instanceof HTMLCanvasElement)) return 0;
        const registry = Module.ludusBrowserWindows || (Module.ludusBrowserWindows = new Map());
        for (const value of registry.values()) if (value.canvas == canvas) return 0;
        entry = {canvas, limit, capture: !!capture, listeners: [], observer: null,
            originalTabIndex: canvas.getAttribute('tabindex'), alive: true, windowFocused: true, last: ""};
        registry.set(token, entry);
        const signal = (kind, a=0, b=0, x=0, y=0, z=0, w=0) => {
            if (entry.alive) _LudusBrowserEvent(token, kind, a, b, x, y, z, w);
        };
        const listen = (target, name, callback, options) => {
            const guarded = event => { if (entry.alive) callback(event); };
            target.addEventListener(name, guarded, options);
            entry.listeners.push([target, name, guarded, options]);
        };
        if (entry.originalTabIndex == null) canvas.setAttribute('tabindex', '0');
        entry.refresh = () => {
            if (!entry.alive) return;
            const attached = canvas.isConnected;
            const css = attached ? getComputedStyle(canvas) : null;
            const width = attached ? canvas.clientWidth : 0;
            const height = attached ? canvas.clientHeight : 0;
            const visible = attached && !document.hidden && width > 0 && height > 0 &&
                css.visibility != 'hidden' && css.visibility != 'collapse' && css.display != 'none';
            const ratio = Number.isFinite(devicePixelRatio) && devicePixelRatio > 0 ? devicePixelRatio : 1;
            const pixelWidth = visible ? Math.max(1, Math.min(entry.limit, Math.round(width * ratio))) : 0;
            const pixelHeight = visible ? Math.max(1, Math.min(entry.limit, Math.round(height * ratio))) : 0;
            entry.visible = visible;
            const focused = visible && entry.windowFocused && document.activeElement == canvas;
            const current = [attached, visible, focused, width, height, ratio, pixelWidth, pixelHeight].join(',');
            if (current == entry.last) return;
            entry.last = current;
            if (canvas.width != pixelWidth) canvas.width = pixelWidth;
            if (canvas.height != pixelHeight) canvas.height = pixelHeight;
            signal(0, pixelWidth, pixelHeight, width, height, ratio, attached ? 1 : 0);
            signal(2, visible ? 1 : 0);
            signal(1, focused ? 1 : 0);
        };
        const focus = () => entry.visible && entry.windowFocused && document.activeElement == canvas && canvas.isConnected && !document.hidden;
        const codes = ["Unknown", "KeyA", "KeyB", "KeyC", "KeyD", "KeyE", "KeyF", "KeyG", "KeyH", "KeyI", "KeyJ", "KeyK", "KeyL", "KeyM", "KeyN", "KeyO", "KeyP", "KeyQ", "KeyR", "KeyS", "KeyT", "KeyU", "KeyV", "KeyW", "KeyX", "KeyY", "KeyZ", "Digit0", "Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Space", "Enter", "Escape", "Tab", "Backspace", "ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight", "AltLeft", "AltRight", "MetaLeft", "MetaRight", "Delete", "Insert", "Home", "End", "PageUp", "PageDown"];
        const keyIndex = new Map(codes.map((code, index) => [code, index]));
        const key = (event, down) => {
            if (!focus()) return;
            const index = keyIndex.get(event.code) || 0;
            signal(down ? 3 : 4, index, event.repeat ? 1 : 0);
            // Keep browser shortcuts and Tab navigation. Consume ordinary game
            // controls only for the focused canvas and only when requested.
            if (entry.capture && index > 0 && event.code != 'Tab' &&
                !event.ctrlKey && !event.metaKey && !event.altKey) event.preventDefault();
        };
        const position = event => {
            const rect = canvas.getBoundingClientRect();
            return [rect.width > 0 ? (event.clientX - rect.left) * canvas.clientWidth / rect.width : 0,
                rect.height > 0 ? (event.clientY - rect.top) * canvas.clientHeight / rect.height : 0];
        };
        const pointer = (event, kind) => {
            // W3 represents one primary pointer; no claim of multitouch support.
            if (!event.isPrimary) return;
            if (kind == 6) {
                canvas.focus({preventScroll: true});
                try { canvas.setPointerCapture(event.pointerId); } catch (_) {}
                entry.refresh();
            }
            if (!focus()) return;
            const xy = position(event);
            signal(kind, event.buttons, 0, xy[0], xy[1]);
            if (entry.capture) event.preventDefault();
        };
        listen(canvas, 'focus', entry.refresh);
        listen(canvas, 'blur', () => { entry.last = ""; signal(1, 0); });
        listen(document, 'visibilitychange', entry.refresh);
        listen(window, 'blur', () => { entry.windowFocused = false; entry.last = ""; entry.refresh(); });
        listen(window, 'focus', () => { entry.windowFocused = true; entry.last = ""; entry.refresh(); });
        listen(window, 'resize', entry.refresh);
        listen(canvas, 'keydown', event => key(event, true));
        listen(canvas, 'keyup', event => key(event, false));
        listen(canvas, 'pointerdown', event => pointer(event, 6));
        listen(canvas, 'pointerup', event => pointer(event, 7));
        listen(canvas, 'pointermove', event => pointer(event, 5));
        listen(canvas, 'pointercancel', () => signal(10));
        listen(canvas, 'lostpointercapture', () => signal(10));
        listen(canvas, 'wheel', event => {
            if (!focus()) return;
            const scale = event.deltaMode == 1 ? 16 : (event.deltaMode == 2 ? canvas.clientHeight : 1);
            signal(8, 0, 0, event.deltaX * scale, event.deltaY * scale);
            if (entry.capture) event.preventDefault();
        }, {passive: false});
        entry.observer = new ResizeObserver(entry.refresh);
        entry.observer.observe(canvas);
        entry.refresh();
        return 1;
    } catch (_) {
        if (entry) {
            entry.alive = false;
            for (const item of entry.listeners) item[0].removeEventListener(item[1], item[2], item[3]);
            if (entry.observer) entry.observer.disconnect();
            if (entry.originalTabIndex == null) entry.canvas.removeAttribute('tabindex');
            if (Module.ludusBrowserWindows) Module.ludusBrowserWindows.delete(token);
        }
        return 0;
    }
});

EM_JS(void, LudusBrowserDetach, (ludus::foundation::uint32 token), {
    try {
    const registry = Module.ludusBrowserWindows;
    const entry = registry && registry.get(token);
    if (!entry) return;
    entry.alive = false; // queued observer callbacks cannot enter wasm afterward
    for (const item of entry.listeners) item[0].removeEventListener(item[1], item[2], item[3]);
    if (entry.observer) entry.observer.disconnect();
    if (entry.originalTabIndex == null && entry.canvas.getAttribute('tabindex') == '0') entry.canvas.removeAttribute('tabindex');
    registry.delete(token);
    } catch (_) { /* C++ token was invalidated before cleanup. */ }
});

EM_JS(ludus::foundation::int32, LudusBrowserRefresh,
      (ludus::foundation::uint32 token, ludus::foundation::uint32 limit), {
    try {
        const entry = Module.ludusBrowserWindows && Module.ludusBrowserWindows.get(token);
        if (!entry || !entry.alive) return 0;
        entry.limit = limit;
        entry.refresh();
        return 1;
    } catch (_) { return 0; }
});
// clang-format on
