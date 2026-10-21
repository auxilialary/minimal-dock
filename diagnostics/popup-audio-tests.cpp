#include "../src/web/MouseInput.h"
#include "../src/audio/AudioMixer.h"
#include "../src/windows/PopupLayout.h"
#include "../src/windows/PopupInteraction.h"
#include "audio-fakes.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
void Check(bool value,const char* message) { if(!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
template<class F> Audio::Snapshot Wait(AudioMixer& audio,F condition) {
    for(int i=0;i<400;++i) { auto s=audio.Read(); if(condition(s)) return s; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    auto s=audio.Read(); std::fprintf(stderr,"Timeout rows=%zu error=%ls\n",s.rows.size(),s.error.c_str()); for(auto& r:s.rows) std::fprintf(stderr,"id=%u volume=%d mute=%d\n",r.id,r.volume,r.muted); Check(false,"event delivery timeout"); return {};
}
int main() {
    HWND host = CreateWindowExW(0, L"STATIC", L"wheel test", WS_POPUP, -700, 120, 320, 400,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "hidden input test host");
    for (UINT message : {WM_MOUSEWHEEL, WM_MOUSEHWHEEL}) {
        Check(MouseInput::IsWheel(message), "wheel routing classification");
        for (short delta : {short(-120), short(-15), short(15), short(120)}) {
            auto wp = MAKEWPARAM(MK_SHIFT | MK_CONTROL, static_cast<WORD>(delta));
            Check(static_cast<INT32>(MouseInput::Data(message, wp)) == delta, "signed high resolution wheel delta");
            POINT expected{27, 53}, screen = expected; ClientToScreen(host, &screen);
            auto actual = MouseInput::Position(host, message, MAKELPARAM(screen.x, screen.y));
            Check(actual.x == expected.x && actual.y == expected.y, "negative screen to physical client coordinates");
        }
    }
    auto client = MouseInput::Position(host, WM_MOUSEMOVE, MAKELPARAM(19, 31));
    Check(client.x == 19 && client.y == 31, "non-wheel stays client coordinates");
    DestroyWindow(host);

    for(int p=-10;p<=110;++p) Check(Audio::Percent(Audio::Scalar(p))==std::clamp(p,0,100),"0-100 scaling");
    RECT popupBounds{100,100,420,500};
    for (UINT message : {WM_LBUTTONDOWN,WM_RBUTTONDOWN,WM_MOUSEWHEEL,WM_MOUSEMOVE,WM_LBUTTONUP}) {
        Check(!DismissPopup(message,popupBounds,{101,101}),"inside input never dismisses");
        Check(DismissPopup(message,popupBounds,{99,101}) == (message == WM_LBUTTONDOWN),"outside left click only");
    }
    int positions=0;
    for(int dpi : {96,120,144,192}) for(int x : {-4000,-1000,0,1000,5000})
      for(int width : {40,200,360,6000}) for(int height : {36,480,3000}) {
        RECT work{-1920,-1080,0,0}, anchor{-1000,-80,-880,-24};
        auto r=CalculatePopup(work,anchor,x,MulDiv(width,dpi,96),MulDiv(height,dpi,96),MulDiv(8,dpi,96));
        Check(r.left>=work.left&&r.right<=work.right&&r.top>=work.top&&r.bottom<=work.bottom,"monitor boundary"); ++positions;
      }
    auto r=CalculatePopup({0,0,1920,1080},{900,900,1020,956},960,200,36,8);
    Check(r.top==856&&r.left==860,"tooltip center and 8 DIP gap");
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto devices=Make<FakeDevices>(); auto original=devices->current;
    auto a=Make<FakeSession>(), b=Make<FakeSession>(); b->identity=L"session-B"; b->pid=9999; b->processResult=AUDCLNT_SUCCESS(0x00d);
    original->manager->list->items={a,b,a}; // Duplicate enumeration must not duplicate controls.
    AudioMixer audio([devices](IMMDeviceEnumerator** out){return devices.CopyTo(out);});
    audio.Open(nullptr);
    auto s=Wait(audio,[](const auto& v){return v.rows.size()==3;});
    Check(s.rows[0].volume==70&&s.rows[1].volume==42,"enumeration values");
    Check(s.rows[1].id!=s.rows[2].id&&s.rows[1].identity!=s.rows[2].identity,"same executable sessions stay distinct");
    Check(s.rows[2].pid==0,"multi-process session must not claim a single process");
    Check(s.rows[1].name==L"Actual helper session"&&s.rows[1].pid==0,"actual session identity fallback");
    Check(a->writes==0&&b->writes==0&&original->endpoint->writes==0,"startup does not change audio");
    unsigned id=s.rows[1].id;
    audio.Set(id,0,false,false);
    Wait(audio,[id](const auto& v){return v.rows.size()==3&&v.rows[1].id==id&&v.rows[1].volume==0;});
    audio.Set(id,100,false,false); audio.Set(id,0,true,true);
    Wait(audio,[](const auto& v){return v.rows.size()==3&&v.rows[1].volume==100&&v.rows[1].muted;});
    audio.Set(0,13,false,false); audio.Set(0,0,true,true);
    Wait(audio,[](const auto& v){return !v.rows.empty()&&v.rows[0].volume==13&&v.rows[0].muted;});
    { std::lock_guard l(audioLock); a->level=.19f; a->mute=FALSE; a->Notify(); }
    Wait(audio,[](const auto& v){return v.rows.size()==3&&v.rows[1].volume==19&&!v.rows[1].muted;});
    audio.Adjust(0, .25); audio.Adjust(0, .75);
    Wait(audio,[](const auto& v){return v.rows[0].volume==14;});
    audio.Adjust(id, 2);
    Wait(audio,[](const auto& v){return v.rows[1].volume==21;});
    audio.Adjust(id, -100);
    Wait(audio,[](const auto& v){return v.rows[1].volume==0;});
    { std::lock_guard l(audioLock); a->level=.19f; a->Notify(); }
    Wait(audio,[](const auto& v){return v.rows[1].volume==19;});
    auto c=Make<FakeSession>(); c->identity=L"new-not-in-enumerator";
    { std::lock_guard l(audioLock); original->manager->events->OnSessionCreated(c.Get()); }
    Wait(audio,[](const auto& v){return v.rows.size()==4;});
    { std::lock_guard l(audioLock); c->state=AudioSessionStateExpired; c->events->OnStateChanged(c->state); }
    Wait(audio,[](const auto& v){return v.rows.size()==3;});
    { std::lock_guard l(audioLock); a->events->OnSessionDisconnected(DisconnectReasonFormatChanged); }
    auto rebound=Wait(audio,[id](const auto& v){return v.rows.size()==3&&v.rows[1].id!=id;});
    id=rebound.rows[1].id;
    auto replacement=Make<FakeDevice>(); replacement->endpoint->level=.31f;
    ComPtr<IAudioSessionNotification> stale;
    { std::lock_guard l(audioLock); stale=original->manager->events; devices->current=replacement; devices->events->OnDefaultDeviceChanged(eRender,eMultimedia,L"replacement"); }
    Wait(audio,[](const auto& v){return v.rows.size()==1&&v.rows[0].volume==31;});
    { std::lock_guard l(audioLock); stale->OnSessionCreated(a.Get()); }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(audio.Read().rows.size()==1,"stale device callbacks ignored");
    audio.Set(id,77,false,false);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(a->level==.19f,"stale slider cannot modify former device");
    { std::lock_guard l(audioLock); devices->current.Reset(); devices->events->OnDefaultDeviceChanged(eRender,eMultimedia,nullptr); }
    Wait(audio,[](const auto& v){return !v.error.empty()&&v.rows.empty();});
    { std::lock_guard l(audioLock); devices->current=replacement; devices->events->OnDefaultDeviceChanged(eRender,eMultimedia,L"returned"); }
    Wait(audio,[](const auto& v){return v.rows.size()==1;});
    audio.Close();
    { std::lock_guard l(audioLock); Check(!devices->events&&!replacement->endpoint->events&&!a->events,"close unregisters all callbacks"); }
    Check(audio.Read().rows.empty(),"closed snapshot released");
    for (int count : {0, 1, 4, 10, 25}) {
        auto fakeDevices = Make<FakeDevices>();
        for (int i = 0; i < count; ++i) {
            auto session = Make<FakeSession>();
            session->identity = L"unique-instance-" + std::to_wstring(i);
            session->systemSounds = i == 0;
            fakeDevices->current->manager->list->items.push_back(session);
        }
        AudioMixer mixer([fakeDevices](IMMDeviceEnumerator** out){return fakeDevices.CopyTo(out);});
        mixer.Open(nullptr);
        auto snapshot = Wait(mixer, [count](const auto& v){return v.rows.size() == static_cast<size_t>(count + 1);});
        Check(snapshot.rows[0].id == 0, "master stays separate");
        if (count) Check(snapshot.rows[1].name == L"System sounds" && snapshot.rows[1].id != 0,
            "system sounds and master are distinct");
        for (auto& session : fakeDevices->current->manager->list->items)
            Check(session->writes == 0, "list enumeration never changes audio");
        mixer.Close();
    }
    CoUninitialize();
    std::printf("PASS: %d popup boundary/DPI cases; Core Audio mocks: enumeration, identity, volumes, mute, callbacks, session creation/expiry, device replacement/loss/recovery, stale events and teardown. No real audio APIs activated.\n",positions);
}

