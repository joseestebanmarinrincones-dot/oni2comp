#pragma once

// The keyboard and mouse layout of a window run (docs/keys.md): which key or mouse button works which button of the console's
// controller, the key file the player can edit (keys.ini), and the key card (the quick menu's Keys entry) made from what is in force.

#include <string>
#include <vector>

namespace keymap {

// What a key can do. The first 16 are the pad's buttons in the order of the controller layout in window.cpp.
enum Action {
    DpadUp, DpadDown, DpadLeft, DpadRight,
    LeftUp, LeftDown, LeftLeft, LeftRight,      // left stick, pushed all the way
    RightUp, RightDown, RightLeft, RightRight,  // right stick, pushed all the way
    Cross, Circle, Square, Triangle,
    L1, R1, L2, R2, L3, R3,
    Start, Select,
    Roll,  // while held, the direction keys of the left stick push the right stick as well (God of War's evade roll)
    Rage,  // L3 and R3 pressed together, which one key can do and two keys can hardly
    Help,  // a key for the key card (none by default; the quick menu's Keys entry shows it)
    Menu,  // another key for the quick menu (none by default; F1 always opens it), host/quickmenu.cpp
    ActionCount
};

// An input is a number: a keyboard scancode (SDL's, below 512: the key by its place on the keyboard), or one of these.
constexpr int kMouseLeft = 1000, kMouseMiddle = 1001, kMouseRight = 1002, kMouseX1 = 1003, kMouseX2 = 1004;
constexpr int kWheelUp = 1010, kWheelDown = 1011;

struct Config {
    std::vector<int> inputs[ActionCount];
    int mouse_stick = 0;       // 0 the mouse moves no stick, 1 it pushes the left stick, 2 the right stick
    double mouse_speed = 1.0;  // 0.1 to 10
    int mouse_invert = 0;      // 0 none, 1 left-right, 2 up-down, 3 both
    double mouse_deadzone = 0; // 0 to 0.9: how far the stick jumps out as soon as the mouse moves
    int look = -1;             // the picture look (F2) written in keys.ini, -1 when the file has no 'look' line
    int scale = 0;             // the inner resolution (1 to 3 times the console's) used at the next start, 0 when the file has no 'scale' line
    int fps_limit = -2;        // the speed limit: -2 when the file has no line, -1 the console (60 NTSC / 50 PAL), 0 no limit, above 0 frames a second
    int fsr_sharpness = -1;    // FSR 1 RCAS sharpening, 0 to 100 percent; -1 when the file has no line
    int aa_strength = -1;      // FXAA strength, 0 to 100 percent; -1 when the file has no line
};

// The picture looks the F2 key goes through. The number is also the video option bits (ps2/video.hpp): bit 0 smooth edges, bit 1 soft, bit 2 sharp (look 4: cubic enlargement + light sharpening; look 5 = sharp + smooth edges), bit 3 FSR 1 EASU, bit 4 RCAS, bit 5 FXAA.
// Looks 9 and 10 are FSR 1 with FXAA on top; look 11 is sharp with smooth edges and FXAA; look 12 is the cheapest enlargement (the
// nearest source pixel, no blending), for a graphics chip that cannot keep up.
constexpr int kLookCount = 13;
constexpr int kFpsConsole = -1;    // the console's own speed (60 frames a second in NTSC, 50 in PAL)
constexpr int kFpsUnlimited = 0;   // as fast as the PC can run the game
constexpr int kFsrSharpnessDefault = 50;  // percent: 50 keeps the previous RCAS amount (0.2 stops)
constexpr int kAaStrengthDefault = 75;    // percent: the previous FXAA amount
#ifdef __ANDROID__
// A phone has the GPU of a low-end desktop: the plain crisp enlargement is the default; the menu still offers the others.
constexpr int kDefaultLook = 0;
#else
constexpr int kDefaultLook = 5;
#endif
unsigned look_options(int look);            // the video option bits of a look: looks 6, 7, 9 and 10 are FSR 1 (bit 3 EASU, bit 4 RCAS)
const char *look_name(int look);           // "Soft + smooth edges", for the title and the key card
const char *look_word(int look);           // the word written in keys.ini
// keys.ini text with its 'look' line set to `look` (other lines untouched; the line is added at the end when missing)
std::string with_look(const std::string &file_text, int look);
// the same for the 'scale' line (the quick menu's resolution, used when the game starts the next time)
std::string with_scale(const std::string &file_text, int scale);
// the same for the speed limit ('console', 'unlimited' or a number of frames a second)
std::string with_fps_limit(const std::string &file_text, int limit);
// the same for the FSR 1 and FXAA amounts (0 to 100 percent)
std::string with_fsr_sharpness(const std::string &file_text, int percent);
std::string with_aa_strength(const std::string &file_text, int percent);

// "Console (60)", "120" or "Unlimited", for the quick menu and the title.
std::string fps_name(int limit, int console_fps);
// Reads 'console', 'auto', 'unlimited', 'off', 0 (no limit) or a number from 1 to 1000. Returns false (and leaves `limit` alone) otherwise.
bool parse_fps_limit(const std::string &text, int &limit);

const char *action_name(int action);  // the word used in keys.ini

// The quick menu's "Change keys" page (docs/keys.md): one line for every button of the controller and for roll and rage, in the order of
// keys.ini (the actions 0 to kChangeableCount - 1; help and menu are left to the file).
constexpr int kChangeableCount = Rage + 1;
const char *action_label(int action);                      // "Square", "Left stick Up", "Rage (L3 + R3)"
std::string keys_text(const Config &config, int action);   // "Left Mouse", "Space, Enter, Keypad Enter", "none"
// keys.ini text with the line of `action` set to the keys in `config` (other lines, comments included, untouched; the line is added at the end when missing)
std::string with_inputs(const std::string &file_text, int action, const std::vector<int> &inputs);
// Gives `input` (a key or mouse button) to `action`, which loses the keys it had. A button that already had `input` swaps: it gets the keys
// `action` had. Returns false and a reason in `message` when the input belongs to the program. Otherwise `message` says what happened and
// `changed` holds every action whose keys are different now (the one asked for first).
bool assign_input(Config &config, int action, int input, std::vector<int> &changed, std::string &message);
// The keys of the controller's buttons, roll and rage back to the defaults (the mouse and picture lines are not touched). `changed` as above.
void restore_default_inputs(Config &config, std::vector<int> &changed);
Config defaults();

// Reads the text of a key file on top of `config` (which starts as defaults()). Every problem becomes a line in `problems`; the
// line it is about is ignored, the rest still counts.
void parse(const std::string &text, Config &config, std::vector<std::string> &problems);

std::string input_name(int input);              // "Left Shift", "Left Mouse"
std::string default_file_text();                // keys.ini as written on the first start
std::string card_text(const Config &config);    // the lines of the key card
// the program's own keys cannot be given to the game: F1, F2, F3, F4, F5, F8, F9, F11
bool is_reserved(int input);

}  // namespace keymap
