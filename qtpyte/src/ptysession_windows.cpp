// src/ptysession_windows.cpp
//
// Windows backend for qtpyte::PtySession, over ConPTY.
//
// The shape differs from POSIX in three ways that between them account for
// nearly all of this file:
//
//   1. Two handles, not one. CreatePseudoConsole is given the read end of an
//      input pipe and the write end of an output pipe; we keep the other two
//      ends. There is no single bidirectional descriptor.
//   2. Nothing here is waitable the way a pty fd is. QSocketNotifier accepts
//      only sockets on Windows, and an anonymous pipe handle is not a
//      synchronisation object, so QWinEventNotifier cannot watch it either.
//      Reading therefore happens on a dedicated thread doing blocking
//      ReadFile, handing chunks back to the session's thread as queued calls.
//      (Named pipes with overlapped I/O would avoid the thread, at the cost
//      of considerably more code and a well-populated field of subtle bugs.)
//   3. The child's exit is watched separately. A process handle IS waitable,
//      so QWinEventNotifier does that part -- and it has to, because ConPTY
//      commonly holds the output pipe open after the child is gone, so
//      waiting for end-of-file would report the exit late or not at all.
//
// ConPTY arrived in Windows 10 1809 (build 17763). The three entry points are
// resolved from kernel32 at run time rather than linked, so that an older
// system fails with the message below instead of refusing to load the process
// at all -- which matters where this is likely to be deployed.
#include "qtpyte/ptysession.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QMetaObject>
#include <QPointer>
#include <QWinEventNotifier>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace qtpyte {
namespace {

constexpr DWORD kReadChunk = 64 * 1024;

// The SDK spells this HPCON, but only from 10.0.17763 onward. Using our own
// alias keeps the file compiling against an older SDK; the run-time check is
// what actually decides whether ConPTY is available.
using ConPty = void *;

using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, ConPty *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(ConPty, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(ConPty);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;

    bool available() const { return create && resize && close; }
};

const ConPtyApi &conpty() {
    static const ConPtyApi api = [] {
        ConPtyApi found;
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (!kernel) {
            return found;
        }
        found.create = reinterpret_cast<CreatePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "CreatePseudoConsole")));
        found.resize = reinterpret_cast<ResizePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "ResizePseudoConsole")));
        found.close = reinterpret_cast<ClosePseudoConsoleFn>(
            reinterpret_cast<void *>(GetProcAddress(kernel, "ClosePseudoConsole")));
        return found;
    }();
    return api;
}

QString last_error_text(const QString &what) {
    const DWORD code = GetLastError();
    LPWSTR buffer = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text = what + QStringLiteral(": ");
    if (len && buffer) {
        text += QString::fromWCharArray(buffer, static_cast<int>(len)).trimmed();
        LocalFree(buffer);
    } else {
        text += QStringLiteral("error %1").arg(code);
    }
    return text;
}

// CreateProcess takes one command line, not an argv, and the child is left to
// split it again. These are the rules CommandLineToArgvW applies in reverse:
// backslashes are literal except in a run immediately before a quote, where
// they double.
QString quote_arg(const QString &arg) {
    if (!arg.isEmpty() && !arg.contains(QLatin1Char(' ')) &&
        !arg.contains(QLatin1Char('\t')) && !arg.contains(QLatin1Char('"'))) {
        return arg;
    }
    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            out += ch;
            backslashes = 0;
            continue;
        }
        out += QString(backslashes, QLatin1Char('\\'));
        out += ch;
        backslashes = 0;
    }
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

// A CreateProcess environment block is one buffer of "KEY=VALUE\0...\0\0",
// sorted case-insensitively. Entries in `extra` are applied on top of the
// inherited environment, matching what the POSIX backend does.
struct CaseInsensitiveLess {
    bool operator()(const QString &a, const QString &b) const {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    }
};

std::vector<wchar_t> build_environment(const QStringList &extra) {
    std::map<QString, QString, CaseInsensitiveLess> vars;

    if (LPWCH block = GetEnvironmentStringsW()) {
        for (LPWCH entry = block; *entry; entry += wcslen(entry) + 1) {
            const QString text = QString::fromWCharArray(entry);
            // Windows carries "=C:=C:\..." drive-current-directory entries
            // whose name begins with '='. Splitting on the first '=' would
            // give them an empty name; skip them and let them be inherited.
            if (text.startsWith(QLatin1Char('='))) {
                continue;
            }
            const int eq = text.indexOf(QLatin1Char('='));
            if (eq > 0) {
                vars[text.left(eq)] = text.mid(eq + 1);
            }
        }
        FreeEnvironmentStringsW(block);
    }

    vars[QStringLiteral("TERM")] = QStringLiteral("xterm-256color");
    vars[QStringLiteral("COLORTERM")] = QStringLiteral("truecolor");
    for (const QString &entry : extra) {
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq > 0) {
            vars[entry.left(eq)] = entry.mid(eq + 1);
        }
    }

    std::vector<wchar_t> block;
    for (const auto &pair : vars) {
        const QString line = pair.first + QLatin1Char('=') + pair.second;
        const std::wstring wide = line.toStdWString();
        block.insert(block.end(), wide.begin(), wide.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

class WinPtySession : public PtySession {
public:
    explicit WinPtySession(QObject *parent) : PtySession(parent) {}

    ~WinPtySession() override { cleanup(); }

    bool start(const QString &program,
               const QStringList &args,
               const QStringList &env,
               int cols,
               int rows) override {
        if (!conpty().available()) {
            error_ = QStringLiteral(
                "ConPTY is not available on this system. It requires Windows 10 "
                "version 1809 (build 17763) or later.");
            return false;
        }
        if (hpc_) {
            error_ = QStringLiteral("session already started");
            return false;
        }

        HANDLE in_read = nullptr;
        HANDLE out_write = nullptr;
        if (!CreatePipe(&in_read, &in_write_, nullptr, 0) ||
            !CreatePipe(&out_read_, &out_write, nullptr, 0)) {
            error_ = last_error_text(QStringLiteral("CreatePipe"));
            closeHandle(in_read);
            closeHandle(out_write);
            cleanup();
            return false;
        }

        const COORD size{static_cast<SHORT>(std::max(1, cols)),
                         static_cast<SHORT>(std::max(1, rows))};
        const HRESULT hr = conpty().create(size, in_read, out_write, 0, &hpc_);

        // The pseudoconsole duplicates both handles into the console host, so
        // our copies go now. Holding the output write end in particular would
        // mean the reader never sees end-of-file.
        closeHandle(in_read);
        closeHandle(out_write);

        if (FAILED(hr)) {
            error_ = QStringLiteral("CreatePseudoConsole failed: 0x%1")
                         .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
            cleanup();
            return false;
        }

        if (!spawn(program, args, env)) {
            cleanup();
            return false;
        }

        startReader();
        watchForExit();
        return true;
    }

    bool running() const override {
        if (!process_) {
            return false;
        }
        return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }

    QString error() const override { return error_; }

    void write(const QByteArray &data) override {
        if (data.isEmpty() || !in_write_) {
            return;
        }
        DWORD written = 0;
        const char *at = data.constData();
        DWORD remaining = static_cast<DWORD>(data.size());
        while (remaining > 0) {
            if (!WriteFile(in_write_, at, remaining, &written, nullptr) ||
                written == 0) {
                break;  // the child is gone; the exit notifier will say so
            }
            at += written;
            remaining -= written;
        }
    }

    void resize(int cols, int rows) override {
        if (!hpc_) {
            return;
        }
        const COORD size{static_cast<SHORT>(std::max(1, cols)),
                         static_cast<SHORT>(std::max(1, rows))};
        conpty().resize(hpc_, size);
    }

    void terminate() override {
        if (running()) {
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, 2000);
        }
        reportExit();
        cleanup();
    }

private:
    bool spawn(const QString &program, const QStringList &args,
               const QStringList &env) {
        QString command = quote_arg(program);
        for (const QString &arg : args) {
            command += QLatin1Char(' ') + quote_arg(arg);
        }
        std::wstring command_line = command.toStdWString();
        command_line.push_back(L'\0');  // CreateProcessW may write to this

        SIZE_T attribute_bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
        std::vector<char> attribute_storage(attribute_bytes);
        auto *attributes =
            reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) {
            error_ = last_error_text(QStringLiteral("InitializeProcThreadAttributeList"));
            return false;
        }

        bool ok = UpdateProcThreadAttribute(attributes, 0,
                                            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                            hpc_, sizeof(hpc_), nullptr, nullptr);
        if (!ok) {
            error_ = last_error_text(QStringLiteral("UpdateProcThreadAttribute"));
            DeleteProcThreadAttributeList(attributes);
            return false;
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
        startup.lpAttributeList = attributes;

        std::vector<wchar_t> environment = build_environment(env);

        PROCESS_INFORMATION info{};
        ok = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                            environment.data(), nullptr, &startup.StartupInfo,
                            &info);
        const QString failure = ok ? QString()
                                   : last_error_text(QStringLiteral("CreateProcess"));
        DeleteProcThreadAttributeList(attributes);

        if (!ok) {
            error_ = failure;
            return false;
        }
        process_ = info.hProcess;
        closeHandle(info.hThread);
        return true;
    }

    void startReader() {
        HANDLE source = out_read_;
        QPointer<WinPtySession> self(this);
        std::atomic<bool> *stopping = &stopping_;
        reader_ = std::thread([source, self, stopping] {
            std::vector<char> buffer(kReadChunk);
            for (;;) {
                DWORD read = 0;
                if (!ReadFile(source, buffer.data(), kReadChunk, &read, nullptr) ||
                    read == 0) {
                    return;  // pipe closed: the pseudoconsole is shutting down
                }
                if (stopping->load()) {
                    return;
                }
                QByteArray chunk(buffer.data(), static_cast<int>(read));
                // Hop to the session's thread. Qt discards queued calls whose
                // context object has been destroyed, so a session that goes
                // away between the read and the delivery is not a dangling
                // pointer -- but the destructor still joins this thread before
                // closing the handle it is reading from.
                QMetaObject::invokeMethod(
                    self.data(),
                    [self, chunk] {
                        if (self) {
                            Q_EMIT self->dataReceived(chunk);
                        }
                    },
                    Qt::QueuedConnection);
            }
        });
    }

    void watchForExit() {
        exit_notifier_ = std::make_unique<QWinEventNotifier>(process_, this);
        connect(exit_notifier_.get(), &QWinEventNotifier::activated, this,
                [this] {
                    exit_notifier_->setEnabled(false);
                    reportExit();
                });
    }

    void reportExit() {
        if (reported_finished_) {
            return;
        }
        reported_finished_ = true;
        DWORD code = 0;
        if (process_ && !GetExitCodeProcess(process_, &code)) {
            code = 0;
        }
        Q_EMIT finished(static_cast<int>(code));
    }

    // Order matters more here than anywhere else in the file.
    //
    // ClosePseudoConsole does not return until the console host has flushed
    // what it still holds, and the only thing draining that pipe is the reader
    // thread. Joining the reader first is therefore a deadlock: it will not
    // finish until end-of-file, and end-of-file will not arrive until the
    // pseudoconsole is closed. Close first, then join.
    void cleanup() {
        stopping_.store(true);
        exit_notifier_.reset();

        closeHandle(in_write_);  // the child's stdin sees end-of-file

        if (hpc_) {
            conpty().close(hpc_);
            hpc_ = nullptr;
        }
        if (reader_.joinable()) {
            reader_.join();
        }

        closeHandle(out_read_);
        closeHandle(process_);
    }

    static void closeHandle(HANDLE &handle) {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
        handle = nullptr;
    }

    ConPty hpc_ = nullptr;
    HANDLE in_write_ = nullptr;
    HANDLE out_read_ = nullptr;
    HANDLE process_ = nullptr;
    std::thread reader_;
    std::atomic<bool> stopping_{false};
    std::unique_ptr<QWinEventNotifier> exit_notifier_;
    bool reported_finished_ = false;
    QString error_;
};

}  // namespace

PtySession *PtySession::create(QObject *parent) {
    return new WinPtySession(parent);
}

}  // namespace qtpyte
