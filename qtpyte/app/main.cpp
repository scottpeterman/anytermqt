// app/main.cpp
//
// Host window for the terminal widget.
#include <QApplication>
#include <QCommandLineParser>
#include <QMainWindow>

#include "qtpyte/ptysession.h"
#include "qtpyte/terminalwidget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qtpyte-term"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("A terminal widget over the pyte emulation core."));
    parser.addHelpOption();
#if defined(Q_OS_WIN)
    const QString default_shell = QStringLiteral("cmd.exe");
#else
    const QString default_shell = QStringLiteral("/bin/bash");
#endif
    QCommandLineOption shell({QStringLiteral("s"), QStringLiteral("shell")},
                             QStringLiteral("Program to run."),
                             QStringLiteral("path"), default_shell);
    QCommandLineOption scrollback({QStringLiteral("b"), QStringLiteral("scrollback")},
                                  QStringLiteral("Scrollback rows to retain."),
                                  QStringLiteral("rows"),
                                  QStringLiteral("5000"));
    parser.addOption(shell);
    parser.addOption(scrollback);
    parser.process(app);

    QMainWindow window;
    auto *terminal = new qtpyte::TerminalWidget(&window);
    terminal->setScrollbackSize(parser.value(scrollback).toInt());
    window.setCentralWidget(terminal);
    window.resize(900, 560);
    window.setWindowTitle(QStringLiteral("qtpyte"));
    window.show();

    // The widget knows nothing about processes; a session supplies the bytes.
    // Swapping this for an SSH channel or a socket is the whole point of the
    // split, and would touch these lines and nothing else.
    auto *session = qtpyte::PtySession::create(terminal);
    qtpyte::attach(terminal, session);

    QObject::connect(session, &qtpyte::PtySession::finished,
                     &window, [&window](int) { window.close(); });

    if (!session->start(parser.value(shell), {}, {},
                        terminal->columns(), terminal->terminalRows())) {
        qCritical("failed to start %s: %s",
                  qUtf8Printable(parser.value(shell)),
                  qUtf8Printable(session->error()));
        return 1;
    }
    terminal->setFocus();
    return app.exec();
}
