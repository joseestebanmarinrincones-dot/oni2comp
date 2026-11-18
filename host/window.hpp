#pragma once

#include <cstdint>
#include <functional>

// The on-screen controller of the Android app (host/android_controls.cpp): the buttons held (the pad masks of window.cpp) and
// the two sticks, -1 to 1 with 0 in the middle. window.cpp puts them into the first controller port along with the keyboard and
// a real controller; the Android view hides itself when a real controller is connected.
void set_virtual_pad(std::uint16_t buttons, float lx, float ly, float rx, float ry);
// The Android on-screen pad's MENU button: opens or closes the quick menu (F1).
void toggle_quick_menu();

bool open_window(bool fullscreen);
int run_window(const std::function<int()> &emulate);
// reads SDL's events, the keyboard and the game controllers into the console's two controller ports (once a frame in a window
// run); host/pad_test.cpp calls it without a window
void poll_input();
