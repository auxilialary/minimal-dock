#pragma once
namespace StartupPolicy {
inline bool CanProtect(bool rendered, bool inventory, bool appsRendered, bool watchdog, bool explorer) {
    return rendered && inventory && appsRendered && watchdog && explorer;
}
inline bool Initialized(bool system, bool inventory, bool apps, bool media, bool mediaRendered) {
    return system && inventory && apps && media && mediaRendered;
}
inline unsigned RetryDelay(unsigned attempt) { return attempt > 0 && attempt < 5 ? 250u << (attempt - 1) : 0; }
}
