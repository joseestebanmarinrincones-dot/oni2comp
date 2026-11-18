// The Android on-screen controller (android/apk/src/org/ps2native/recomp/VirtualPadView.java). The view draws the buttons and
// sends what is held here with a JNI call; window.cpp merges it into the first controller port. Kept in its own file so the PC
// build does not need JNI headers.
#if defined(__ANDROID__)

#include <jni.h>

#include "window.hpp"

extern "C" JNIEXPORT void JNICALL Java_org_ps2native_recomp_MainActivity_nativeSetVirtualPad(JNIEnv *, jclass, jint buttons, jfloat lx, jfloat ly,
                                                                                            jfloat rx, jfloat ry) {
    set_virtual_pad(static_cast<std::uint16_t>(buttons & 0xFFFF), lx, ly, rx, ry);
}

extern "C" JNIEXPORT void JNICALL Java_org_ps2native_recomp_MainActivity_nativeToggleMenu(JNIEnv *, jclass) { toggle_quick_menu(); }

#endif
