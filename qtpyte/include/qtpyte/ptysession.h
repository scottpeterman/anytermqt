// include/qtpyte/ptysession.h
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

namespace qtpyte {

class TerminalWidget;

// A child process on the far end of a pseudo-terminal, as a Qt object.
//
// The contract is bytes in, bytes out, and a size -- deliberately the same
// contract TerminalWidget offers, so the two connect signal-to-slot and
// neither learns anything about the other. That symmetry is the point: it is
// what lets a caller drop this class entirely and drive the widget from an
// SSH channel, a serial port, a socket or a recorded fixture instead.
//
// Why the abstraction lives here and not in qtpyte::Pty: a POSIX pty is one
// bidirectional file descriptor that can be watched with a QSocketNotifier,
// while ConPTY is a pair of pipe handles that cannot -- QSocketNotifier takes
// only sockets on Windows, and anonymous pipe handles are not waitable by
// QWinEventNotifier either, so that backend needs a reader thread. A "portable
// handle" interface would leak one platform's mechanism into the other. An
// interface in terms of bytes leaks neither.
//
// Instances are created through create(); the concrete classes are private to
// their platform's translation unit.
class PtySession : public QObject {
    Q_OBJECT

public:
    explicit PtySession(QObject *parent = nullptr) : QObject(parent) {}
    ~PtySession() override = default;

    // A session for the platform this was built for. Never null.
    static PtySession *create(QObject *parent = nullptr);

    // Launch `program` under a new pseudo-terminal sized cols x rows. Entries
    // in `env` are "KEY=VALUE" and are applied on top of the inherited
    // environment; TERM is set to xterm-256color unless env overrides it.
    //
    // There is no default program on purpose. /bin/bash is not a portable
    // answer and a header that pretends otherwise pushes the problem to a
    // caller who will not find out until they are on the other platform.
    virtual bool start(const QString &program,
                       const QStringList &args,
                       const QStringList &env,
                       int cols,
                       int rows) = 0;

    virtual bool running() const = 0;
    // Set when start() returns false, or when a backend fails later.
    virtual QString error() const = 0;

public Q_SLOTS:
    // Send bytes to the child. Connect TerminalWidget::dataReady here.
    virtual void write(const QByteArray &data) = 0;
    // Tell the child its window changed. Connect TerminalWidget::resized here.
    virtual void resize(int cols, int rows) = 0;
    // Ask the child to go away, politely and then not. Safe to call twice,
    // and safe to call when nothing was ever started.
    virtual void terminate() = 0;

Q_SIGNALS:
    // Bytes arrived from the child. Connect this to TerminalWidget::feed.
    void dataReceived(const QByteArray &data);

    // The child exited. `exitCode` is normalised across platforms: it is the
    // program's own exit status, or 128 + signal number where a POSIX child
    // was killed by a signal -- the convention a shell uses. The raw waitpid
    // status is deliberately not exposed, because it would mean something
    // different on each platform behind an identical signature.
    void finished(int exitCode);
};

// Connect a session to a widget: bytes in, bytes out, and size changes.
//
// Exactly three connections, and doing them by hand is fine. This exists
// because getting one of the three wrong produces a terminal that looks
// almost right -- typing works but full-screen applications never redraw --
// and that is a poor first ten minutes for someone trying the library out.
void attach(TerminalWidget *widget, PtySession *session);

}  // namespace qtpyte
