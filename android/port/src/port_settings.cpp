#include "port_settings.hpp"

namespace port::settings {
namespace {
bool g_unlocked_60fps = true;
}

bool Unlocked60Fps() {
    return g_unlocked_60fps;
}

void SetUnlocked60Fps(bool enabled) {
    g_unlocked_60fps = enabled;
}

}  // namespace port::settings
