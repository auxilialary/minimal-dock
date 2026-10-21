#include "../src/media/LyricsModel.h"
#include <cstdio>
int main() {
    winrt::init_apartment();
    auto test = [](const char* name, const wchar_t* title, const wchar_t* artist, double duration, const wchar_t* candidate, const wchar_t* performer, double seconds) {
        MediaSnapshot track; track.title=title; track.artist=artist; track.duration=duration;
        const auto body=L"[{\"trackName\":\""+std::wstring(candidate)+L"\",\"artistName\":\""+performer+L"\",\"duration\":"+std::to_wstring(seconds)+L",\"plainLyrics\":\"fixture\"}]";
        auto old = Lyrics::Parse(body, track, true);
        auto now = Lyrics::Parse(body,track,true,nullptr,true);
        std::printf("%s: artist-aware=%s title-only=%s\n",name,old.empty()?"excluded":"selected",now.empty()?"excluded":"selected");
    };
    test("A: embedded artist, exact Jesica",L"3umph - Jesica (prod. Sonshitsuuu, Risii)",L"n1zamu",200,L"Jesica",L"3umph",200);
    test("A: Jessica spelling +15 seconds",L"3umph - Jesica (prod. Sonshitsuuu, Risii)",L"n1zamu",215,L"Jessica",L"3umph",200);
    test("B: eurooo +25 seconds",L"eurooo! (prod.risii+notee1)",L"3umph",215,L"eurooo",L"3umph",240);
    test("C: Turbo (Majestic)",L"Turbo (Majestic)",L"Big Baby Tape",200,L"Turbo (Majestic)",L"Big Baby Tape",200);
    test("C: meaningful unbracketed subtitle",L"Turbo (Majestic)",L"Big Baby Tape",200,L"Turbo Majestic",L"Big Baby Tape",200);
    test("Shorter recording -30 seconds",L"Turbo",L"Big Baby Tape",200,L"Turbo",L"Big Baby Tape",170);
    test("Extended recording +35 seconds",L"Turbo",L"Big Baby Tape",200,L"Turbo",L"Big Baby Tape",235);
    test("Wrong performer",L"Turbo",L"Big Baby Tape",200,L"Turbo",L"Unrelated",200);
    test("Unrelated title, same duration",L"Turbo",L"Big Baby Tape",200,L"Other",L"Big Baby Tape",200);
    winrt::uninit_apartment();
}
