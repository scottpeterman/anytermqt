// src/ptysession.cpp
//
// The platform-neutral part of PtySession. The backends live in
// ptysession_posix.cpp and ptysession_windows.cpp; exactly one is compiled.
#include "qtpyte/ptysession.h"

#include "qtpyte/terminalwidget.h"

namespace qtpyte {

void attach(TerminalWidget *widget, PtySession *session) {
    if (!widget || !session) {
        return;
    }
    QObject::connect(session, &PtySession::dataReceived,
                     widget, &TerminalWidget::feed);
    QObject::connect(widget, &TerminalWidget::dataReady,
                     session, &PtySession::write);
    // The third one is the one that gets forgotten. Without it the emulator
    // knows the new shape and the child does not, so a full-screen
    // application keeps drawing to the old size after a window resize.
    QObject::connect(widget, &TerminalWidget::resized,
                     session, &PtySession::resize);
}

}  // namespace qtpyte
