// Prueba del InputManager: mapeo de mando virtual SDL y HUD multitactil.
// Compila con SDL2:
//   clang++ -std=c++17 -O2 -Isrc -I../third_party/sdl2/include/SDL2 `
//     tests/input_manager_test.cpp src/input_manager.cpp `
//     ../third_party/sdl2/lib/libSDL2.dll.a -o input_test.exe

#include "input_manager.hpp"

#include <SDL.h>

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *name) {
    std::printf("  %-52s %s\n", name, condition ? "PASS" : "FAIL");
    if (!condition) ++failures;
}

bool nearly(float value, float expected, float tolerance = 0.05f) {
    return std::fabs(value - expected) <= tolerance;
}

}  // namespace

int main() {
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    port::InputManager input;
    check(input.Initialize(), "Initialize");
    input.Resize(1280, 720);
    check(!input.HasController(), "sin mando: HUD visible");
    check(input.HudVisible(), "HudVisible");

    std::printf("\n=== Stick + botones multitactiles ===\n");
    float sx = 0.0f, sy = 0.0f;
    check(input.StickCenter(sx, sy), "StickCenter");
    check(input.TouchDown(1, sx, sy), "TouchDown stick");
    check(input.TouchMove(1, sx + 0.18f, sy - 0.08f),
          "TouchMove stick");
    input.Update();
    check(input.Pad().left_x > 0.3f, "stick x positivo");
    check(input.Pad().left_y < -0.1f, "stick y arriba");

    float cross_x = 0.0f, cross_y = 0.0f;
    check(input.ButtonCenter(port::PadButton::Cross, cross_x, cross_y),
          "ButtonCenter Cross");
    check(input.TouchDown(2, cross_x, cross_y), "TouchDown Cross");
    input.Update();
    check(input.Pad().pressed(port::PadButton::Cross),
          "Cross con stick activo");
    check(input.Pad().left_x > 0.3f, "stick sigue activo");

    float circle_x = 0.0f, circle_y = 0.0f;
    input.ButtonCenter(port::PadButton::Circle, circle_x, circle_y);
    check(input.TouchDown(3, circle_x, circle_y), "TouchDown Circle");
    input.Update();
    check(input.Pad().pressed(port::PadButton::Cross) &&
              input.Pad().pressed(port::PadButton::Circle),
          "dos botones a la vez");

    check(input.TouchUp(2), "TouchUp Cross");
    input.Update();
    check(!input.Pad().pressed(port::PadButton::Cross),
          "Cross se libera");
    check(input.Pad().pressed(port::PadButton::Circle),
          "Circle sigue pulsado");

    std::printf("\n=== Start y latch de un frame ===\n");
    float start_x = 0.0f, start_y = 0.0f;
    check(input.ButtonCenter(port::PadButton::Start, start_x, start_y),
          "ButtonCenter Start");
    check(input.TouchDown(4, start_x, start_y), "TouchDown Start");
    check(input.TouchUp(4), "TouchUp Start inmediato");
    input.Update();
    check(input.Pad().pressed(port::PadButton::Start),
          "tap rapido se conserva un frame");
    input.Update();
    check(!input.Pad().pressed(port::PadButton::Start),
          "latch se limpia al siguiente frame");

    input.TouchUp(1);
    input.TouchUp(3);
    input.Update();
    check(nearly(input.Pad().left_x, 0.0f), "stick vuelve al centro");
    check(!input.Pad().pressed(port::PadButton::Circle),
          "Circle liberado");

    check(!input.TouchDown(5, 0.5f, 0.5f),
          "zona muerta no consume el toque");

    std::printf("\n=== Mando virtual SDL ===\n");
    const int virtual_index = SDL_JoystickAttachVirtual(
        SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 16, 0);
    if (virtual_index < 0) {
        std::printf("  (SDL virtual joystick no disponible: %s)\n",
                    SDL_GetError());
    } else {
        SDL_Joystick *joystick = SDL_JoystickOpen(virtual_index);
        SDL_Event added{};
        added.type = SDL_CONTROLLERDEVICEADDED;
        added.cdevice.which = virtual_index;
        input.HandleEvent(added);
        check(input.HasController(), "hotplug: mando detectado");
        check(!input.HudVisible(), "HUD oculto con mando");

        if (joystick != nullptr) {
            SDL_JoystickSetVirtualButton(
                joystick, SDL_CONTROLLER_BUTTON_A, 1);
            SDL_JoystickSetVirtualButton(
                joystick, SDL_CONTROLLER_BUTTON_X, 1);
            SDL_JoystickSetVirtualButton(
                joystick, SDL_CONTROLLER_BUTTON_BACK, 1);
            SDL_JoystickSetVirtualAxis(
                joystick, SDL_CONTROLLER_AXIS_LEFTX, 32767);
            SDL_JoystickSetVirtualAxis(
                joystick, SDL_CONTROLLER_AXIS_LEFTY, -32768);
            SDL_JoystickSetVirtualAxis(
                joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
            SDL_GameControllerUpdate();
            input.Update();
            check(input.Pad().pressed(port::PadButton::Cross),
                  "A -> Cross");
            check(input.Pad().pressed(port::PadButton::Square),
                  "X -> Square");
            check(input.Pad().pressed(port::PadButton::Select),
                  "Back -> Select");
            check(input.Pad().pressed(port::PadButton::L2),
                  "gatillo izquierdo -> L2");
            check(input.Pad().left_x > 0.5f, "eje izquierdo X");

            SDL_JoystickSetVirtualButton(
                joystick, SDL_CONTROLLER_BUTTON_A, 0);
            SDL_JoystickSetVirtualButton(
                joystick, SDL_CONTROLLER_BUTTON_X, 0);
            SDL_JoystickSetVirtualButton(
                joystick, SDL_CONTROLLER_BUTTON_BACK, 0);
            SDL_JoystickSetVirtualAxis(
                joystick, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0);
            SDL_GameControllerUpdate();
            input.Update();
            check(!input.Pad().pressed(port::PadButton::Cross),
                  "A liberado");
        }

        SDL_JoystickID instance = -1;
        if (joystick != nullptr) {
            instance = SDL_JoystickInstanceID(joystick);
            SDL_JoystickClose(joystick);
        }
        SDL_Event removed{};
        removed.type = SDL_CONTROLLERDEVICEREMOVED;
        removed.cdevice.which = instance;
        input.HandleEvent(removed);
        check(!input.HasController(), "desconexion: HUD vuelve");
        SDL_JoystickDetachVirtual(virtual_index);
    }

    input.Shutdown();
    SDL_Quit();

    std::printf("\nFALLOS: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
