// src/keymap.cpp
#include "qtpyte/keymap.h"

#include <QChar>

namespace qtpyte {
namespace {

// xterm's modifier encoding: a parameter of 1 + a bitmask, appended to the
// sequence. Shift 1, Alt 2, Control 4, Meta 8. A value of 1 means no
// modifiers, in which case the parameter is left off entirely.
int modifier_param(Qt::KeyboardModifiers mods) {
    int mask = 0;
    if (mods & Qt::ShiftModifier) mask |= 1;
    if (mods & Qt::AltModifier) mask |= 2;
    if (mods & Qt::ControlModifier) mask |= 4;
    if (mods & Qt::MetaModifier) mask |= 8;
    return mask + 1;
}

// CSI sequences ending in a letter: arrows, Home, End.
std::string csi_letter(char final_byte, int mod, bool application_cursor) {
    if (mod > 1) {
        // A modified cursor key is always CSI form, even in application mode
        // -- SS3 carries no parameters.
        return std::string("\033[1;") + std::to_string(mod) + final_byte;
    }
    if (application_cursor) {
        return std::string("\033O") + final_byte;
    }
    return std::string("\033[") + final_byte;
}

// CSI sequences ending in '~': Insert, Delete, PageUp, PageDown, F5 and up.
std::string csi_tilde(int number, int mod) {
    std::string out = "\033[" + std::to_string(number);
    if (mod > 1) {
        out += ';';
        out += std::to_string(mod);
    }
    out += '~';
    return out;
}

// Control characters from a letter: Ctrl-A is 0x01, and so on up to Ctrl-Z.
// The oddities beyond the alphabet matter more than they look -- Ctrl-[ is
// Escape, and vim users press it constantly.
std::string control_char(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return std::string(1, static_cast<char>(key - Qt::Key_A + 1));
    }
    switch (key) {
        case Qt::Key_Space:
        case Qt::Key_2:            return std::string(1, '\0');   // NUL
        case Qt::Key_BracketLeft:  return "\033";                 // ESC
        case Qt::Key_Backslash:    return std::string(1, '\034');
        case Qt::Key_BracketRight: return std::string(1, '\035');
        case Qt::Key_AsciiCircum:
        case Qt::Key_6:            return std::string(1, '\036');
        case Qt::Key_Underscore:
        case Qt::Key_Minus:        return std::string(1, '\037');
        case Qt::Key_Question:     return std::string(1, '\177'); // DEL
        default:                   return std::string();
    }
}

}  // namespace

std::string encode_key(const QKeyEvent *event, const KeyModes &modes) {
    if (event == nullptr) {
        return std::string();
    }

    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int mod = modifier_param(mods);

    // A bare modifier press is not a keystroke to send.
    switch (key) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
        case Qt::Key_AltGr:
        case Qt::Key_CapsLock:
        case Qt::Key_NumLock:
        case Qt::Key_ScrollLock:
            return std::string();
        default:
            break;
    }

    std::string out;

    switch (key) {
        case Qt::Key_Up:    out = csi_letter('A', mod, modes.application_cursor); break;
        case Qt::Key_Down:  out = csi_letter('B', mod, modes.application_cursor); break;
        case Qt::Key_Right: out = csi_letter('C', mod, modes.application_cursor); break;
        case Qt::Key_Left:  out = csi_letter('D', mod, modes.application_cursor); break;
        case Qt::Key_Home:  out = csi_letter('H', mod, modes.application_cursor); break;
        case Qt::Key_End:   out = csi_letter('F', mod, modes.application_cursor); break;

        case Qt::Key_Insert:   out = csi_tilde(2, mod); break;
        case Qt::Key_Delete:   out = csi_tilde(3, mod); break;
        case Qt::Key_PageUp:   out = csi_tilde(5, mod); break;
        case Qt::Key_PageDown: out = csi_tilde(6, mod); break;

        // F1-F4 are SS3 in their unmodified form and CSI once modified.
        case Qt::Key_F1:
        case Qt::Key_F2:
        case Qt::Key_F3:
        case Qt::Key_F4: {
            const char final_byte = static_cast<char>('P' + (key - Qt::Key_F1));
            out = (mod > 1) ? std::string("\033[1;") + std::to_string(mod) + final_byte
                            : std::string("\033O") + final_byte;
            break;
        }
        case Qt::Key_F5:  out = csi_tilde(15, mod); break;
        case Qt::Key_F6:  out = csi_tilde(17, mod); break;
        case Qt::Key_F7:  out = csi_tilde(18, mod); break;
        case Qt::Key_F8:  out = csi_tilde(19, mod); break;
        case Qt::Key_F9:  out = csi_tilde(20, mod); break;
        case Qt::Key_F10: out = csi_tilde(21, mod); break;
        case Qt::Key_F11: out = csi_tilde(23, mod); break;
        case Qt::Key_F12: out = csi_tilde(24, mod); break;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            // CR, not LF. The line discipline turns it into a newline; sending
            // LF here means a shell in canonical mode never sees end-of-line.
            out = "\r";
            break;

        case Qt::Key_Backspace:
            // DEL (0x7f), matching VERASE as configured on the pty. Sending
            // BS (0x08) instead is the classic "backspace prints ^H" bug.
            out = (mods & Qt::ControlModifier) ? std::string(1, '\010')
                                               : std::string(1, '\177');
            break;

        case Qt::Key_Tab:
            out = "\t";
            break;
        case Qt::Key_Backtab:
            out = "\033[Z";  // CSI Z, shift-tab
            break;
        case Qt::Key_Escape:
            out = "\033";
            break;

        default:
            break;
    }

    if (!out.empty()) {
        // Alt on a sequence key is already folded into the modifier
        // parameter; nothing further to prepend.
        return out;
    }

    // Control combinations that map to a control character.
    if ((mods & Qt::ControlModifier) && !(mods & Qt::AltModifier)) {
        const std::string ctrl = control_char(key);
        if (!ctrl.empty()) {
            return ctrl;
        }
    }

    // Ordinary text. Qt has already applied Shift and AltGr, so event->text()
    // is the character the user actually typed.
    const QString text = event->text();
    if (text.isEmpty()) {
        return std::string();
    }

    std::string bytes;
    if (mods & Qt::AltModifier) {
        // Alt-x is sent as ESC then x. Terminals have never agreed on whether
        // to do this or set the high bit; ESC-prefixing is what readline,
        // vim and tmux all expect.
        bytes += '\033';
    }
    if ((mods & Qt::ControlModifier)) {
        const std::string ctrl = control_char(key);
        if (!ctrl.empty()) {
            bytes += ctrl;
            return bytes;
        }
    }
    bytes += text.toUtf8().toStdString();
    return bytes;
}

std::string encode_text(const QString &text, bool bracketed_paste) {
    QString normalised = text;
    normalised.replace(QLatin1String("\r\n"), QLatin1String("\r"));
    normalised.replace(QLatin1Char('\n'), QLatin1Char('\r'));

    std::string body = normalised.toUtf8().toStdString();
    if (!bracketed_paste) {
        return body;
    }
    // Bracketed paste lets the application tell typed input from pasted
    // input, which is how an editor avoids auto-indenting a pasted block.
    return "\033[200~" + body + "\033[201~";
}

}  // namespace qtpyte
