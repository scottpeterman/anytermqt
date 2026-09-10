// include/qtpyte/keymap.h
#pragma once

#include <QKeyEvent>
#include <QString>

#include <string>

namespace qtpyte {

// Cursor keys and the keypad send different sequences depending on modes the
// application sets. Both are tracked by the widget and passed in here.
struct KeyModes {
    // DECCKM (CSI ?1h): cursor keys send SS3 O rather than CSI [. Set by any
    // full-screen application that reads arrow keys, so getting it wrong
    // makes arrows work at a shell prompt and fail inside an editor.
    bool application_cursor = false;
    // DECKPAM: the numeric keypad sends application sequences.
    bool application_keypad = false;
};

// Translate a key event into the bytes a terminal would send.
//
// Returns an empty string when the key produces nothing to send -- a bare
// modifier, or a key this map has no sequence for. That is distinct from a
// key that legitimately sends nothing visible, so callers should check for
// empty rather than assuming every event yields output.
std::string encode_key(const QKeyEvent *event, const KeyModes &modes);

// Bytes for pasted or programmatically inserted text. Newlines become CR,
// because that is what a terminal sends for Return and what the line
// discipline turns back into a newline; sending LF instead means a shell sees
// a literal linefeed and a pasted script runs its lines together.
std::string encode_text(const QString &text, bool bracketed_paste);

}  // namespace qtpyte
