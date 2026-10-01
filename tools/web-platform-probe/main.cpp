#include <ludus/foundation/base/core.h>
#include <ludus/foundation/base/pointer.hpp>
#include <ludus/foundation/logging/category.hpp>
#include <ludus/foundation/logging/log_format.hpp>
#include <ludus/platform/base/window.h>
#include <ludus/platform/browser/window.h>
#include <ludus/platform/native_window.h>

#include <string_view>

#include <emscripten.h>

namespace
{
using namespace ludus::foundation;
using namespace ludus::platform;
UniquePtr<Window> gWindow;
WindowManager gManager;
uint64 gEvents = 0;
uint64 gKeyDownEvents = 0;

bool Verify(bool condition, std::string_view message) noexcept
{
    if (!condition)
    {
        LUDUS_LOG_WARN(logging::LOG_TEMP, "W3 failure: {}", message);
    }
    return condition;
}

// Deterministic tests use ordinary DOM events on the real canvas in a browser,
// and a small explicit DOM fixture in Node. No private engine state is patched.
// clang-format off
EM_JS(void, Scenario, (int32 step), {
    const canvas = document.querySelector('#canvas');
    const key = (code, down) => {
        const event = new KeyboardEvent(down ? 'keydown' : 'keyup', {code, bubbles: true, cancelable: true});
        canvas.dispatchEvent(event);
        Module.lastPrevented = event.defaultPrevented;
    };
    if (step == 0) { canvas.focus(); key('KeyW', true); }
    if (step == 1) { document.querySelector('#outside').focus(); }
    if (step == 2) { canvas.style.width = '300px'; canvas.style.height = '150px'; }
    if (step == 3) { canvas.style.display = 'none'; }
    if (step == 4) { canvas.style.display = ""; canvas.focus(); key('Tab', true); }
    if (step == 5) {
        for (let i=0; i<160; ++i) key('KeyA', true);
        key('KeyA', false);
    }
    if (step == 6) {
        const event = new WheelEvent('wheel', {deltaX: 2, deltaY: 3, deltaMode: 1, cancelable: true});
        canvas.dispatchEvent(event);
    }
    if (step == 7) {
        canvas.dispatchEvent(new PointerEvent('pointerdown', {pointerId: 1, isPrimary: true,
            buttons: 1, clientX: 25, clientY: 30, cancelable: true}));
    }
    if (step == 8) { key('KeyW', true); canvas.dispatchEvent(new PointerEvent('pointercancel')); }
    if (step == 9) { key('KeyB', true); window.dispatchEvent(new Event('blur')); }
    if (step == 10) { window.dispatchEvent(new Event('focus')); }
    if (step == 11) { canvas.remove(); }
});
EM_JS(int32, WasPrevented, (), { return Module.lastPrevented ? 1 : 0; });
EM_JS(void, DrawState,
      (uint32 width, uint32 height, float64 cssWidth, float64 cssHeight, float64 ratio,
       int32 focused, int32 visible, uint32 buttons, uint32 held, uint32 events, uint32 keys,
       float64 x, float64 y), {
    const status = document.getElementById('status');
    status.textContent = 'CSS ' + cssWidth + ' × ' + cssHeight + ' | framebuffer ' + width + ' × ' + height +
        ' | DPR ' + ratio + ' | focused ' + !!focused + ' | visible ' + !!visible +
        ' | held keys ' + held + ' | buttons ' + buttons + ' | events ' + events + ' | keydowns ' + keys +
        ' | pointer ' + Math.round(x) + ', ' + Math.round(y);
    const canvas = document.querySelector('#canvas');
    if (!canvas || !width || !height) return;
    const context = canvas.getContext('2d');
    if (!context) return;
    context.fillStyle = focused ? '#25496a' : '#292b36';
    context.fillRect(0, 0, width, height);
    context.fillStyle = '#ffffff'; context.font = Math.max(14, Math.round(18 * ratio)) + 'px system-ui';
    context.fillText('Click here, then try W / arrows / pointer / wheel', 12 * ratio, 32 * ratio);
    context.fillStyle = '#ffae65';
    context.fillRect(x * ratio - 5, y * ratio - 5, 10, 10);
});
EM_JS(void, ReportResult, (int32 passed), {
    if (typeof document != 'undefined') {
        const result = document.getElementById('result');
        if (result) result.textContent = passed ? (Module.arguments.includes('automated') ? 'W3 lifecycle checks passed' : 'W3 backend ready') : 'W3 checks failed';
    }
});
// clang-format on

void Drain() noexcept
{
    browser::InputEvent event;
    while (gWindow->PollBrowserInput(event))
    {
        ++gEvents;
        if (event.Kind == browser::EventKind::KeyDown)
        {
            ++gKeyDownEvents;
        }
    }
}

void Frame() noexcept
{
    if (!gWindow)
    {
        return;
    }
    (void)gWindow->HandleEvent({});
    Drain();
    browser::WindowState state;
    (void)gWindow->GetBrowserState(state);
    uint32 held = 0;
    for (bool key : state.Keys)
    {
        if (key)
        {
            ++held;
        }
    }
    DrawState(state.FramebufferWidth,
              state.FramebufferHeight,
              state.CssWidth,
              state.CssHeight,
              state.PixelRatio,
              state.Focused ? 1 : 0,
              state.Visible ? 1 : 0,
              state.Buttons,
              held,
              static_cast<uint32>(gEvents),
              static_cast<uint32>(gKeyDownEvents),
              state.PointerX,
              state.PointerY);
}

bool Create() noexcept
{
    return gManager.CreateWindow({}, gWindow);
}

bool Automated() noexcept
{
    if (!Verify(Create(), "create"))
    {
        return false;
    }
    browser::WindowState state;
    (void)gWindow->GetBrowserState(state);
    const auto native = gWindow->GetNativeWindowInfo();
    if (!Verify(native.System == WindowSystem::WebCanvas && native.CanvasSelector != nullptr && state.Attached &&
                    state.Visible && state.FramebufferWidth == static_cast<uint32>(state.CssWidth * state.PixelRatio) &&
                    state.FramebufferHeight == static_cast<uint32>(state.CssHeight * state.PixelRatio),
                "canvas descriptor"))
    {
        return false;
    }
    UniquePtr<Window> duplicate;
    if (!Verify(!gManager.CreateWindow({}, duplicate), "exclusive canvas ownership"))
    {
        return false;
    }
    Drain();
    Scenario(0);
    (void)gWindow->GetBrowserState(state);
    if (!Verify(state.Keys[static_cast<usize>(browser::Key::KeyW)] && state.Focused && WasPrevented(), "keydown"))
    {
        return false;
    }
    Scenario(1);
    (void)gWindow->GetBrowserState(state);
    if (!Verify(!state.Keys[static_cast<usize>(browser::Key::KeyW)] && !state.Focused, "blur clears held keys"))
    {
        return false;
    }
    Scenario(2);
    (void)gWindow->HandleEvent({});
    if (!Verify(gWindow->SetBrowserFramebufferLimit(128), "limit update"))
    {
        return false;
    }
    (void)gWindow->GetBrowserState(state);
    if (!Verify(state.CssWidth == 300 && state.CssHeight == 150 && state.FramebufferWidth <= 128 &&
                    state.FramebufferHeight <= 128 && !gWindow->SetBrowserFramebufferLimit(0),
                "CSS/pixel size limits"))
    {
        return false;
    }
    Scenario(3);
    (void)gWindow->HandleEvent({});
    (void)gWindow->GetBrowserState(state);
    if (!Verify(!state.Visible && state.FramebufferWidth == 0 && state.FramebufferHeight == 0, "hidden canvas"))
    {
        return false;
    }
    Scenario(4);
    (void)gWindow->HandleEvent({});
    if (!Verify(!WasPrevented(), "Tab navigation"))
    {
        return false;
    }
    Drain();
    Scenario(5);
    (void)gWindow->GetBrowserState(state);
    if (!Verify(state.DroppedEvents > 0 && !state.Keys[static_cast<usize>(browser::Key::KeyA)], "overflow held state"))
    {
        return false;
    }
    Drain();
    Scenario(6);
    browser::InputEvent event;
    if (!Verify(gWindow->PollBrowserInput(event) && event.Kind == browser::EventKind::Wheel && event.WheelX == 32 &&
                    event.WheelY == 48,
                "wheel normalization"))
    {
        return false;
    }
    Scenario(7);
    (void)gWindow->GetBrowserState(state);
    if (!Verify(state.Buttons == 1 && state.PointerX >= 0, "pointer"))
    {
        return false;
    }
    Scenario(8);
    (void)gWindow->GetBrowserState(state);
    if (!Verify(state.Buttons == 0 && state.Keys[static_cast<usize>(browser::Key::KeyW)],
                "pointer cancel preserves keyboard"))
    {
        return false;
    }
    Scenario(9);
    (void)gWindow->GetBrowserState(state);
    if (!Verify(!state.Focused && !state.Keys[static_cast<usize>(browser::Key::KeyB)], "window blur"))
    {
        return false;
    }
    Scenario(10);
    (void)gWindow->HandleEvent({});
    gWindow.Reset();
    if (!Verify(Create(), "restart"))
    {
        return false;
    }
    Drain();
    Scenario(0);
    usize keyEvents = 0;
    while (gWindow->PollBrowserInput(event))
    {
        if (event.Kind == browser::EventKind::KeyDown)
        {
            ++keyEvents;
        }
    }
    if (!Verify(keyEvents == 1, "no duplicate callbacks after restart"))
    {
        return false;
    }
    Scenario(11);
    if (!Verify(!gWindow->HandleEvent({}), "removed canvas"))
    {
        return false;
    }
    (void)gWindow->GetBrowserState(state);
    if (!Verify(!state.Attached && !state.Visible && !state.Keys[static_cast<usize>(browser::Key::KeyW)],
                "removed state"))
    {
        return false;
    }
    gWindow.Reset();
    LUDUS_LOG_WARN(logging::LOG_TEMP, "[W3:passed]");
    return true;
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void W3Restart()
{
    gWindow.Reset();
    gKeyDownEvents = 0;
    gEvents = 0;
    ReportResult(Create() ? 1 : 0);
}

int main(int argc, char** argv)
{
    if (!gManager.Initialize({}))
    {
        return 1;
    }
    if (argc > 1 && std::string_view(argv[1]) == "automated")
    {
        const bool passed = Automated();
        ReportResult(passed ? 1 : 0);
        return passed ? 0 : 1;
    }
    if (!Create())
    {
        return 1;
    }
    // Repeated creation/destruction is checked without blocking the event loop.
    gWindow.Reset();
    if (!Create())
    {
        return 1;
    }
    ReportResult(1);
    emscripten_set_main_loop(Frame, 0, true);
    return 0;
}
