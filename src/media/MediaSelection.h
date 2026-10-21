#pragma once
#include <vector>
namespace Media {
inline int SelectPlaying(const std::vector<bool>& playing, int current) {
    if (current >= 0 && static_cast<size_t>(current) < playing.size() && playing[current]) return current;
    for (size_t i = 0; i < playing.size(); ++i) if (playing[i]) return static_cast<int>(i);
    return -1;
}
}
