#pragma once
#include "LyricsIdentity.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <cmath>
namespace Lyrics {
using winrt::Windows::Data::Json::JsonObject;
inline winrt::hstring String(const JsonObject& row, const wchar_t* name) {
    auto value = row.TryLookup(name);
    return value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String ? value.GetString() : winrt::hstring{};
}
struct Confidence { double score = 0, title = 0, artist = 0; bool duration = false; };
inline Confidence Rank(const JsonObject& row, const MediaSnapshot& track, bool titleOnly = false) {
    Confidence best;
    const auto candidate = Clean(std::wstring(String(row, L"trackName")), true);
    const auto other = Clean(std::wstring(String(row, L"artistName")));
    const auto duration = row.GetNamedNumber(L"duration", 0);
    const bool known = std::isfinite(track.duration) && track.duration > 0 && std::isfinite(duration) && duration > 0;
    const double difference = known ? std::abs(duration - track.duration) : 0;
    for (const auto& identity : Analyze(track).identities) {
        const double t = TextSimilarity(identity.title, candidate), a = TextSimilarity(identity.artist, other);
        if (t < .64) continue; // Duration can never rescue a clearly unrelated title.
        const auto versions = Versions(identity.title), candidateVersions = Versions(candidate);
        // Explicitly different recordings are unsafe even if the base title agrees.
        if (versions != candidateVersions) continue;
        if (titleOnly) {
            if (t < .80 || (known && difference > std::max(30.0, track.duration * .20))) continue;
            double score = .84 * t + (known ? .14 * std::max(0.0, 1 - difference / 60) : .07) + .02 * a;
            if (score > best.score) best = {score, t, a, known && difference <= std::max(20.0, track.duration * .15)};
            continue;
        }
        double score = .70 * t + .24 * a;
        if (known) score += .06 * std::max(0.0, 1 - difference / 90);
        if (!track.album.empty()) score += .01 * Similarity(Normalize(track.album), Normalize(std::wstring(String(row, L"albumName"))));
        // A major duration difference lowers confidence without a binary cutoff.
        if (known) score -= std::min(.18, difference / std::max(120.0, std::min(track.duration, duration)) * .12);
        score *= identity.weight;
        if (score > best.score) best = {score, t, a, known && difference <= std::max(20.0, track.duration * .15)};
    }
    return best;
}
inline double Score(const JsonObject& row, const MediaSnapshot& track) { return Rank(row, track).score; }
inline bool Match(const JsonObject& row, const MediaSnapshot& track) { return Score(row, track) >= .84; }
struct Evaluation { unsigned candidates = 0, rejected = 0; double score = 0; bool malformed = false; std::wstring reason = L"none"; };
inline std::wstring Parse(const std::wstring& body, const MediaSnapshot& track, bool search, Evaluation* diagnostics = nullptr, bool titleOnly = false) {
    using namespace winrt::Windows::Data::Json;
    struct Candidate { JsonObject row; Confidence confidence; std::wstring identity; double priority; };
    Evaluation evaluation; std::vector<Candidate> candidates;
    auto consider = [&](const JsonObject& row) {
        ++evaluation.candidates;
        auto confidence = Rank(row, track, titleOnly);
        if (confidence.score < .72) { ++evaluation.rejected; evaluation.reason = L"confidence"; return; }
        if (String(row, L"syncedLyrics").empty() && String(row, L"plainLyrics").empty() && !row.GetNamedBoolean(L"instrumental", false)) {
            ++evaluation.rejected; evaluation.reason = L"no-content"; return;
        }
        auto identity = Clean(std::wstring(String(row, L"artistName"))) + L"\n" + Clean(std::wstring(String(row, L"trackName")), true);
        double priority = confidence.score + (String(row, L"syncedLyrics").empty() ? 0 : .012);
        // Duplicate releases of the same identity are not competing song identities.
        for (auto& item : candidates) if (item.identity == identity &&
            std::abs(item.row.GetNamedNumber(L"duration", 0) - row.GetNamedNumber(L"duration", 0)) <= 5) {
            if (priority > item.priority) item = {row, confidence, identity, priority};
            return;
        }
        candidates.push_back({row, confidence, identity, priority});
    };
    try {
        if (!search) consider(JsonObject::Parse(body));
        else { auto rows = JsonArray::Parse(body); for (unsigned i = 0; i < std::min(rows.Size(), 200u); ++i)
            try { consider(rows.GetObjectAt(i)); } catch (...) { ++evaluation.rejected; evaluation.malformed = true; } }
    } catch (...) { evaluation.malformed = true; }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.priority > b.priority; });
    std::wstring best;
    if (!candidates.empty()) {
        const auto& selected = candidates.front(); const auto& c = selected.confidence;
        evaluation.score = c.score;
        const double margin = candidates.size() > 1 ? c.score - candidates[1].confidence.score : 1;
        const bool high = c.score >= .90 && c.title >= .85 && c.artist >= .75;
        const bool medium = c.score >= .84 && c.artist >= .65 && c.title >= .78;
        const bool noArtist = Clean(track.artist).empty() && c.title == 1 && c.duration && Tokens(Clean(track.title)).size() >= 2;
        const bool titleMatch = c.title >= .80 && c.score >= (c.duration ? .82 : .90);
        if (titleOnly ? titleMatch && margin >= .075 :
            ((high && margin >= .04) || ((medium || noArtist) && margin >= .075))) {
            const auto& row = selected.row; JsonObject result;
            result.SetNamedValue(L"synced", JsonValue::CreateStringValue(String(row, L"syncedLyrics")));
            result.SetNamedValue(L"plain", JsonValue::CreateStringValue(String(row, L"plainLyrics")));
            result.SetNamedValue(L"instrumental", JsonValue::CreateBooleanValue(row.GetNamedBoolean(L"instrumental", false)));
            result.SetNamedValue(L"id", JsonValue::CreateNumberValue(row.GetNamedNumber(L"id", 0)));
            best = result.Stringify();
        } else evaluation.reason = L"ambiguous";
    }
    if (diagnostics) *diagnostics = evaluation;
    return best;
}
}

