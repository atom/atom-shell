// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include <string>

#include "base/containers/fixed_flat_map.h"
#include "base/strings/string_util.h"
#include "shell/common/keyboard_util.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/event_constants.h"

namespace electron {

namespace {

using CodeAndShiftedChar = std::pair<ui::KeyboardCode, std::optional<char16_t>>;

// TODO: Why does making this constexpr fail
CodeAndShiftedChar KeyboardCodeFromKeyIdentifier(const std::string_view str) {
#if BUILDFLAG(IS_MAC)
  constexpr auto CommandOrControl = ui::VKEY_COMMAND;
#else
  constexpr auto CommandOrControl = ui::VKEY_CONTROL;
#endif

  constexpr auto Lookup =
      base::MakeFixedFlatMap<std::string_view, CodeAndShiftedChar>({
          {"alt", {ui::VKEY_MENU, {}}},
          {"altgr", {ui::VKEY_ALTGR, {}}},
          {"backspace", {ui::VKEY_BACK, {}}},
          {"capslock", {ui::VKEY_CAPITAL, {}}},
          {"cmd", {ui::VKEY_COMMAND, {}}},
          {"cmdorctrl", {CommandOrControl, {}}},
          {"command", {ui::VKEY_COMMAND, {}}},
          {"commandorcontrol", {CommandOrControl, {}}},
          {"control", {ui::VKEY_CONTROL, {}}},
          {"ctrl", {ui::VKEY_CONTROL, {}}},
          {"delete", {ui::VKEY_DELETE, {}}},
          {"down", {ui::VKEY_DOWN, {}}},
          {"end", {ui::VKEY_END, {}}},
          {"enter", {ui::VKEY_RETURN, {}}},
          {"esc", {ui::VKEY_ESCAPE, {}}},
          {"escape", {ui::VKEY_ESCAPE, {}}},
          {"f1", {ui::VKEY_F1, {}}},
          {"f10", {ui::VKEY_F10, {}}},
          {"f11", {ui::VKEY_F11, {}}},
          {"f12", {ui::VKEY_F12, {}}},
          {"f13", {ui::VKEY_F13, {}}},
          {"f14", {ui::VKEY_F14, {}}},
          {"f15", {ui::VKEY_F15, {}}},
          {"f16", {ui::VKEY_F16, {}}},
          {"f17", {ui::VKEY_F17, {}}},
          {"f18", {ui::VKEY_F18, {}}},
          {"f19", {ui::VKEY_F19, {}}},
          {"f2", {ui::VKEY_F2, {}}},
          {"f20", {ui::VKEY_F20, {}}},
          {"f21", {ui::VKEY_F21, {}}},
          {"f22", {ui::VKEY_F22, {}}},
          {"f23", {ui::VKEY_F23, {}}},
          {"f24", {ui::VKEY_F24, {}}},
          {"f3", {ui::VKEY_F3, {}}},
          {"f4", {ui::VKEY_F4, {}}},
          {"f5", {ui::VKEY_F5, {}}},
          {"f6", {ui::VKEY_F6, {}}},
          {"f7", {ui::VKEY_F7, {}}},
          {"f8", {ui::VKEY_F8, {}}},
          {"f9", {ui::VKEY_F9, {}}},
          {"home", {ui::VKEY_HOME, {}}},
          {"insert", {ui::VKEY_INSERT, {}}},
          {"left", {ui::VKEY_LEFT, {}}},
          {"medianexttrack", {ui::VKEY_MEDIA_NEXT_TRACK, {}}},
          {"mediaplaypause", {ui::VKEY_MEDIA_PLAY_PAUSE, {}}},
          {"mediaprevioustrack", {ui::VKEY_MEDIA_PREV_TRACK, {}}},
          {"mediastop", {ui::VKEY_MEDIA_STOP, {}}},
          {"meta", {ui::VKEY_COMMAND, {}}},
          {"num0", {ui::VKEY_NUMPAD0, {}}},
          {"num1", {ui::VKEY_NUMPAD1, {}}},
          {"num2", {ui::VKEY_NUMPAD2, {}}},
          {"num3", {ui::VKEY_NUMPAD3, {}}},
          {"num4", {ui::VKEY_NUMPAD4, {}}},
          {"num5", {ui::VKEY_NUMPAD5, {}}},
          {"num6", {ui::VKEY_NUMPAD6, {}}},
          {"num7", {ui::VKEY_NUMPAD7, {}}},
          {"num8", {ui::VKEY_NUMPAD8, {}}},
          {"num9", {ui::VKEY_NUMPAD9, {}}},
          {"numadd", {ui::VKEY_ADD, {}}},
          {"numdec", {ui::VKEY_DECIMAL, {}}},
          {"numdiv", {ui::VKEY_DIVIDE, {}}},
          {"numlock", {ui::VKEY_NUMLOCK, {}}},
          {"nummult", {ui::VKEY_MULTIPLY, {}}},
          {"numsub", {ui::VKEY_SUBTRACT, {}}},
          {"option", {ui::VKEY_MENU, {}}},
          {"pagedown", {ui::VKEY_NEXT, {}}},
          {"pageup", {ui::VKEY_PRIOR, {}}},
          {"plus", {ui::VKEY_OEM_PLUS, '+'}},
          {"printscreen", {ui::VKEY_SNAPSHOT, {}}},
          {"return", {ui::VKEY_RETURN, {}}},
          {"right", {ui::VKEY_RIGHT, {}}},
          {"scrolllock", {ui::VKEY_SCROLL, {}}},
          {"shift", {ui::VKEY_SHIFT, {}}},
          {"space", {ui::VKEY_SPACE, {}}},
          {"super", {ui::VKEY_COMMAND, {}}},
          {"tab", {ui::VKEY_TAB, {}}},
          {"up", {ui::VKEY_UP, {}}},
          {"volumedown", {ui::VKEY_VOLUME_DOWN, {}}},
          {"volumemute", {ui::VKEY_VOLUME_MUTE, {}}},
          {"volumeup", {ui::VKEY_VOLUME_UP, {}}},
      });

  if (auto iter = Lookup.find(str); iter != Lookup.end())
    return iter->second;

  return {ui::VKEY_UNKNOWN, {}};
}

constexpr CodeAndShiftedChar KeyboardCodeFromCharCode(char16_t c) {
  switch (c) {
    case ' ':
      return {ui::VKEY_SPACE, {}};
    case '!':
      return {ui::VKEY_1, '!'};
    case '"':
      return {ui::VKEY_OEM_7, '"'};
    case '#':
      return {ui::VKEY_3, '#'};
    case '$':
      return {ui::VKEY_4, '$'};
    case '%':
      return {ui::VKEY_5, '%'};
    case '&':
      return {ui::VKEY_7, '&'};
    case '(':
      return {ui::VKEY_9, '('};
    case ')':
      return {ui::VKEY_0, ')'};
    case '*':
      return {ui::VKEY_8, '*'};
    case '+':
      return {ui::VKEY_OEM_PLUS, '+'};
    case ',':
      return {ui::VKEY_OEM_COMMA, {}};
    case '-':
      return {ui::VKEY_OEM_MINUS, {}};
    case '.':
      return {ui::VKEY_OEM_PERIOD, {}};
    case '/':
      return {ui::VKEY_OEM_2, {}};
    case '0':
      return {ui::VKEY_0, {}};
    case '1':
      return {ui::VKEY_1, {}};
    case '2':
      return {ui::VKEY_2, {}};
    case '3':
      return {ui::VKEY_3, {}};
    case '4':
      return {ui::VKEY_4, {}};
    case '5':
      return {ui::VKEY_5, {}};
    case '6':
      return {ui::VKEY_6, {}};
    case '7':
      return {ui::VKEY_7, {}};
    case '8':
      return {ui::VKEY_8, {}};
    case '9':
      return {ui::VKEY_9, {}};
    case ':':
      return {ui::VKEY_OEM_1, ':'};
    case ';':
      return {ui::VKEY_OEM_1, {}};
    case '<':
      return {ui::VKEY_OEM_COMMA, '<'};
    case '=':
      return {ui::VKEY_OEM_PLUS, {}};
    case '>':
      return {ui::VKEY_OEM_PERIOD, '>'};
    case '?':
      return {ui::VKEY_OEM_2, '?'};
    case '@':
      return {ui::VKEY_2, '@'};
    case '[':
      return {ui::VKEY_OEM_4, {}};
    case '\'':
      return {ui::VKEY_OEM_7, {}};
    case '\\':
      return {ui::VKEY_OEM_5, {}};
    case ']':
      return {ui::VKEY_OEM_6, {}};
    case '^':
      return {ui::VKEY_6, '^'};
    case '_':
      return {ui::VKEY_OEM_MINUS, '_'};
    case '`':
      return {ui::VKEY_OEM_3, {}};
    case 'a':
      return {ui::VKEY_A, {}};
    case 'b':
      return {ui::VKEY_B, {}};
    case 'c':
      return {ui::VKEY_C, {}};
    case 'd':
      return {ui::VKEY_D, {}};
    case 'e':
      return {ui::VKEY_E, {}};
    case 'f':
      return {ui::VKEY_F, {}};
    case 'g':
      return {ui::VKEY_G, {}};
    case 'h':
      return {ui::VKEY_H, {}};
    case 'i':
      return {ui::VKEY_I, {}};
    case 'j':
      return {ui::VKEY_J, {}};
    case 'k':
      return {ui::VKEY_K, {}};
    case 'l':
      return {ui::VKEY_L, {}};
    case 'm':
      return {ui::VKEY_M, {}};
    case 'n':
      return {ui::VKEY_N, {}};
    case 'o':
      return {ui::VKEY_O, {}};
    case 'p':
      return {ui::VKEY_P, {}};
    case 'q':
      return {ui::VKEY_Q, {}};
    case 'r':
      return {ui::VKEY_R, {}};
    case 's':
      return {ui::VKEY_S, {}};
    case 't':
      return {ui::VKEY_T, {}};
    case 'u':
      return {ui::VKEY_U, {}};
    case 'v':
      return {ui::VKEY_V, {}};
    case 'w':
      return {ui::VKEY_W, {}};
    case 'x':
      return {ui::VKEY_X, {}};
    case 'y':
      return {ui::VKEY_Y, {}};
    case 'z':
      return {ui::VKEY_Z, {}};
    case '{':
      return {ui::VKEY_OEM_4, '{'};
    case '|':
      return {ui::VKEY_OEM_5, '|'};
    case '}':
      return {ui::VKEY_OEM_6, '}'};
    case '~':
      return {ui::VKEY_OEM_3, '~'};
    case 0x08:
      return {ui::VKEY_BACK, {}};
    case 0x09:
      return {ui::VKEY_TAB, {}};
    case 0x0D:
      return {ui::VKEY_RETURN, {}};
    case 0x1B:
      return {ui::VKEY_ESCAPE, {}};
    case 0x7F:
      return {ui::VKEY_DELETE, {}};
    default:
      return {ui::VKEY_UNKNOWN, {}};
  }
}

}  // namespace

ui::KeyboardCode KeyboardCodeFromStr(const std::string_view str,
                                     std::optional<char16_t>* shifted_char) {
  auto const [code, shifted] =
      str.size() == 1 ? KeyboardCodeFromCharCode(base::ToLowerASCII(str[0]))
                      : KeyboardCodeFromKeyIdentifier(base::ToLowerASCII(str));

  if (code == ui::VKEY_UNKNOWN)
    LOG(WARNING) << "Invalid accelerator token: " << str;

  *shifted_char = shifted;
  return code;
}

}  // namespace electron
