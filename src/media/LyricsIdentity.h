#pragma once
#include "LyricsSearch.h"
#include <vector>
namespace Lyrics {
struct Identity { std::wstring title, artist; double weight = 1; };
struct Metadata { std::vector<Identity> identities; std::vector<std::wstring> queries; };
// Bounded search hint, not a correction to the displayed or ranked identity.
inline std::wstring SpellingHint(std::wstring title) {
    if (title.size() < 5 || title.size() > 16 || title.find(L' ') != std::wstring::npos) return {};
    for (size_t i = 1; i + 1 < title.size(); ++i)
        if (title[i] == L's' && std::wstring(L"aeiou").find(title[i - 1]) != std::wstring::npos &&
            std::wstring(L"aeiou").find(title[i + 1]) != std::wstring::npos) {
            title.insert(i, 1, L's'); return title;
        }
    return {};
}
inline Metadata Analyze(const MediaSnapshot& track) {
    Metadata out; auto identity = track;
    bool embedded = false;
    for (const auto* separator : {L" - ", L" – ", L" — ", L": "}) {
        auto at = track.title.find(separator);
        if (at == std::wstring::npos) continue;
        auto left = Clean(track.title.substr(0, at));
        auto right = track.title.substr(at + wcslen(separator));
        if (!left.empty() && !Clean(right).empty()) {
            identity.artist = left; identity.title = right; embedded = true;
        }
        break;
    }
    auto title = Clean(identity.title), full = Clean(identity.title, true), artist = Clean(identity.artist);
    auto add = [&](const std::wstring& name, const std::wstring& performer, double weight) {
        if (name.empty()) return;
        for (const auto& item : out.identities) if (item.title == name && item.artist == performer) return;
        out.identities.push_back({name, performer, weight});
    };
    add(full, artist, 1); add(title, artist, full == title ? 1 : .97);
    // Keep uploader as a weaker alternate rather than requiring it to match.
    if (embedded && Clean(track.artist) != artist) add(title, Clean(track.artist), .90);
    auto query = [&](const std::wstring& name, bool performer = true) {
        auto value = performer && !artist.empty() ? artist + L" " + name : name;
        if (!value.empty() && std::find(out.queries.begin(), out.queries.end(), value) == out.queries.end()) out.queries.push_back(value);
    };
    // Broaden request two to title alone; spelling and subtitle variants rank locally.
    query(title);
    query(title, false);
    return out;
}
inline double TextSimilarity(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return 0;
    if (a == b) return 1;
    // Bound work on untrusted metadata; token overlap still handles long titles.
    if (a.size() > 160 || b.size() > 160) return Similarity(a, b);
    std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u)});
        previous.swap(current);
    }
    const auto distance = previous.back();
    double characters = 1 - static_cast<double>(distance) / static_cast<double>(std::max(a.size(), b.size()));
    // Short names need exact evidence; longer names allow proportionate edits.
    if (std::min(a.size(), b.size()) < 4 || distance > std::max<size_t>(1, std::min(a.size(), b.size()) / 5)) characters = 0;
    return std::max(Similarity(a, b), characters);
}
}
