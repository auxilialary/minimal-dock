#include "../src/app/VisibilityPolicy.h"
#include "../src/app/StartupPolicy.h"
#include "../src/windows/IslandLayout.h"
#include "../src/windows/WindowMotion.h"
#include "../src/windows/EdgeTrigger.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace Visibility;
    assert(Decode(0) == Mode::Always && Decode(999) == Mode::Always);
    assert(Decode(1) == Mode::Auto && Decode(2) == Mode::Smart);
    for (auto mode : {Mode::Always, Mode::Auto, Mode::Smart}) {
        for (bool obstruction : {false, true}) for (bool edge : {false, true}) {
            assert(!Show(mode, true, obstruction, edge)); // Fullscreen always wins, even stale edge input.
            assert(Show(mode, false, obstruction, edge) ==
                (mode == Mode::Always || edge || (mode == Mode::Smart && !obstruction)));
        }
    }
    assert(!Show(Mode::Auto, false, false, false));
    assert(Show(Mode::Smart, false, false, false));
    assert(!Show(Mode::Smart, false, true, false));
    // Rendering readiness never depends on fullscreen, selected mode, or Explorer.
    assert(StartupPolicy::Initialized(true, true, true, true, true));
    for (int missing = 0; missing < 5; ++missing) {
        bool state[5]{true,true,true,true,true}; state[missing] = false;
        assert(!StartupPolicy::Initialized(state[0],state[1],state[2],state[3],state[4]));
    }
    assert(!StartupPolicy::CanProtect(true,true,true,true,false));
    assert(!StartupPolicy::CanProtect(true,true,true,false,true));
    assert(StartupPolicy::CanProtect(true,true,true,true,true)); // Delayed Explorer arrival.
    // Exercise real Win32 message dispatch using a hidden fixture, without launching Dock.
    HWND owner = CreateWindowExW(0,L"STATIC",L"visibility-test",0,0,0,1,1,HWND_MESSAGE,
        nullptr,GetModuleHandleW(nullptr),nullptr);
    assert(owner);
    {
        EdgeTrigger trigger; assert(trigger.Create(owner));
        assert(!IsWindowVisible(trigger.Handle()));
        const auto style = GetWindowLongPtrW(trigger.Handle(),GWL_EXSTYLE);
        assert((style & WS_EX_NOACTIVATE) && (style & WS_EX_TOOLWINDOW));
        assert(SendMessageW(trigger.Handle(),WM_MOUSEACTIVATE,0,0) == MA_NOACTIVATE);
        MSG message{};
        for (int movement = 0; movement < 100; ++movement) {
            SendMessageW(trigger.Handle(),WM_MOUSEMOVE,0,0);
            assert(PeekMessageW(&message,owner,EdgeTrigger::Enter,EdgeTrigger::Enter,PM_REMOVE));
            SendMessageW(trigger.Handle(),WM_MOUSELEAVE,0,0);
            assert(PeekMessageW(&message,owner,EdgeTrigger::Pointer,EdgeTrigger::Pointer,PM_REMOVE));
        }
        trigger.Place({-1920,0,0,1080},false);
        assert(!IsWindowVisible(trigger.Handle()));
    }
    DestroyWindow(owner);
    for (UINT dpi : {96u,120u,144u,192u}) {
        auto scale = [dpi](int n) { return MulDiv(n, static_cast<int>(dpi), 96); };
        for (RECT monitor : {RECT{0,0,1920,1080}, RECT{-2560,-200,0,1240}, RECT{1920,0,3000,1920}}) {
            auto layout = CalculateMediaIslands(monitor,scale(56),scale(120),scale(10),scale(10),
                scale(440),scale(100),true,scale(220));
            assert(abs(layout.apps.left + layout.apps.right - monitor.left - monitor.right) <= 1);
            assert(Fullscreen(monitor,monitor,false));
            assert(!Fullscreen(monitor,monitor,true));
            RECT work = monitor; work.bottom -= scale(48);
            assert(!Fullscreen(work,monitor,false));
            assert(Overlaps(monitor,layout.apps));
            RECT above = monitor; above.bottom = layout.apps.top;
            assert(!Overlaps(above,layout.apps));
            auto only = CalculateIslands(monitor,scale(56),scale(120),scale(10),scale(10),0,1,false);
            assert(abs(only.system.left + only.system.right - monitor.left - monitor.right) <= 1);
            RECT from = layout.apps; OffsetRect(&from,0,scale(8));
            for (int frame=0;frame<=16;++frame) {
                auto rect = WindowMotionFrame(from,layout.apps,WindowMotionProgress(frame/16.0));
                assert(rect.left+rect.right == layout.apps.left+layout.apps.right);
            }
        }
    }
    // Rapid edge/leave/focus sequences and notifications: input latch is the only override.
    for (int i=0;i<10000;++i) {
        bool reveal = (i % 2) != 0;
        for (int notification=0;notification<4;++notification)
            assert(Show(Mode::Auto,false,true,reveal) == reveal);
        assert(!Show(Mode::Auto,true,true,reveal));
    }
    std::cout << "Visibility/startup/geometry policy tests passed; no Dock launch or registry writes.\n";
}
