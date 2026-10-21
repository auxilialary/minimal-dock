#pragma once
#include "MediaService.h"
#include <algorithm>
#include <set>
#include <sstream>
namespace Lyrics {
inline std::wstring Normalize(std::wstring text) {
    int count = NormalizeString(NormalizationKC, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (count > 0) { std::wstring folded(static_cast<size_t>(count), L' ');
        count = NormalizeString(NormalizationKC, text.c_str(), static_cast<int>(text.size()), folded.data(), count);
        if (count > 0) { folded.resize(static_cast<size_t>(count)); text = folded; } }
    if (!text.empty()) LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), static_cast<int>(text.size()), text.data(), static_cast<int>(text.size()), nullptr, nullptr, 0);
    std::wstring out;
    for (wchar_t c : text) { WORD type = 0; GetStringTypeW(CT_CTYPE1, &c, 1, &type);
        if (type & (C1_ALPHA | C1_DIGIT)) out += c;
        else if (!out.empty() && out.back() != L' ') out += L' '; }
    if (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}
inline std::set<std::wstring> Tokens(const std::wstring& text) {
    std::wistringstream in(text); std::set<std::wstring> result; std::wstring word;
    while (in >> word) result.insert(word); return result;
}
inline std::wstring Clean(const std::wstring& text, bool subtitles = false) {
    std::wstring stripped;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'(' || text[i] == L'[' || text[i] == L'{') {
            auto end = text.find_first_of(L")]}", i + 1);
            if (end != std::wstring::npos) {
                auto group = Normalize(text.substr(i + 1, end - i - 1));
                bool version = false;
                for (const auto& token : Tokens(group))
                    if (token == L"live" || token == L"acoustic" || token == L"remix" || token == L"slowed" || token == L"sped" || token == L"instrumental" || token == L"demo" || token == L"remaster") version = true;
                if (group.starts_with(L"prod ") || group.starts_with(L"produced ") || group.starts_with(L"feat ") || group.starts_with(L"ft ")) version = false;
                const bool credit = group.starts_with(L"prod") || group.starts_with(L"feat ") || group.starts_with(L"ft ");
                const bool upload = group == L"free download" || group == L"official audio" || group == L"official video";
                if (version || (subtitles && !credit && !upload)) stripped += L" " + group + L" ";
                i = end; continue;
            }
        }
        stripped += text[i];
    }
    auto normalized = Normalize(stripped); std::wistringstream in(normalized); std::wstring word, out;
    while (in >> word) {
        if (word == L"prod" || word == L"produced") break;
        if (word == L"feat" || word == L"ft" || word == L"featuring") {
            while (in >> word) if (word == L"live" || word == L"acoustic" || word == L"remix" || word == L"slowed" || word == L"sped" || word == L"up") out += L" " + word;
            break;
        }
        if (word == L"official" || word == L"audio" || word == L"video" || word == L"visualizer" ||
            word == L"exclusive" || word == L"reupload" || word == L"hd" || word == L"lyrics") continue;
        if (!out.empty()) out += L' '; out += word;
    }
    return out;
}
inline std::set<std::wstring> Versions(const std::wstring& text) {
    auto words = Tokens(text); std::set<std::wstring> result;
    for (const auto* word : {L"live", L"acoustic", L"remix", L"slowed", L"sped", L"instrumental", L"remaster", L"demo"})
        if (words.count(word)) result.insert(word);
    return result;
}
inline double Similarity(const std::wstring& a, const std::wstring& b) {
    auto x = Tokens(a), y = Tokens(b); if (x.empty() || y.empty()) return 0;
    size_t common = 0; for (const auto& token : x) common += y.count(token);
    return 2.0 * static_cast<double>(common) / static_cast<double>(x.size() + y.size());
}
// An explicit artist/title separator supplies a search identity, never display metadata.
inline MediaSnapshot SearchIdentity(const MediaSnapshot& track) {
    auto result = track;
    for (const auto* separator : {L" - ", L" – ", L" — ", L": "}) {
        auto at = track.title.find(separator);
        if (at == std::wstring::npos) continue;
        auto artist = Clean(track.title.substr(0, at));
        auto title = Clean(track.title.substr(at + wcslen(separator)));
        if (!artist.empty() && !title.empty()) { result.artist = artist; result.title = title; }
        break;
    }
    return result;
}
inline bool OneEdit(const std::wstring& a, const std::wstring& b) {
    if (a.size() < 5 || b.size() < 5 || a == b) return false;
    size_t i = 0, j = 0, edits = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++i; ++j; continue; }
        if (++edits > 1) return false;
        if (a.size() >= b.size()) ++i;
        if (b.size() >= a.size()) ++j;
    }
    return edits + (a.size() - i) + (b.size() - j) == 1;
}

}
