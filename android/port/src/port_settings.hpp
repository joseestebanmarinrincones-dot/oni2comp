#pragma once

namespace port::settings {

// Modo de presentacion: false mantiene el divisor NTSC (30 Hz de logica),
// true desbloquea 60 Hz. La logica recompilada sigue siendo por frame; el
// delta time se expone a los hooks que lo consuman.
bool Unlocked60Fps();
void SetUnlocked60Fps(bool enabled);

}  // namespace port::settings
