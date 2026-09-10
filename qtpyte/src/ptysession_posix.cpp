// src/ptysession_posix.cpp
//
// POSIX backend for qtpyte::PtySession: the existing qtpyte::Pty, watched with
// a QSocketNotifier. The pty handshake, the winsize ioctl and the reaping all
// stay in Pty where they are already tested; this file only turns a readable
// file descriptor into a signal.
#include "qtpyte/ptysession.h"

#include <QSocketNotifier>

#include <sys/wait.h>

#include <memory>
#include <vector>

#include "qtpyte/pty.h"

namespace qtpyte {
namespace {

// Bytes pulled from the pty per readable notification. Large enough that a
// `cat` of something big does not turn into thousands of tiny feeds, small
// enough that the event loop still gets a turn -- without a cap, `yes`
// freezes the UI.
constexpr int kReadChunk = 64 * 1024;
constexpr int kReadBudgetPerTurn = 8;

// waitpid packs exit status and termination signal into one int, and the
// macros to unpack it do not exist on Windows. Normalising here means
// PtySession::finished carries the same meaning on every platform.
int normalise_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return status;
}

class UnixPtySession : public PtySession {
public:
    explicit UnixPtySession(QObject *parent) : PtySession(parent) {}

    ~UnixPtySession() override {
        // Before the fd closes, or Qt warns about a notifier on an invalid
        // descriptor during teardown.
        notifier_.reset();
    }

    bool start(const QString &program,
               const QStringList &args,
               const QStringList &env,
               int cols,
               int rows) override {
        std::vector<std::string> argv;
        argv.reserve(static_cast<std::size_t>(args.size()));
        for (const QString &arg : args) {
            argv.push_back(arg.toStdString());
        }
        std::vector<std::string> environment;
        environment.reserve(static_cast<std::size_t>(env.size()));
        for (const QString &entry : env) {
            environment.push_back(entry.toStdString());
        }

        if (!pty_.start(program.toStdString(), argv, cols, rows, environment)) {
            return false;
        }

        reported_finished_ = false;
        notifier_ = std::make_unique<QSocketNotifier>(pty_.fd(),
                                                      QSocketNotifier::Read,
                                                      this);
        connect(notifier_.get(), &QSocketNotifier::activated,
                this, &UnixPtySession::readFromPty);
        return true;
    }

    bool running() const override { return pty_.running(); }
    QString error() const override { return QString::fromStdString(pty_.error()); }

    void write(const QByteArray &data) override {
        if (data.isEmpty()) {
            return;
        }
        pty_.write(data.constData(), static_cast<unsigned long>(data.size()));
    }

    void resize(int cols, int rows) override { pty_.resize(cols, rows); }

    void terminate() override {
        pty_.terminate();
        collectExit();
    }

private:
    void readFromPty() {
        std::vector<char> buf(kReadChunk);
        bool ended = false;

        // Drain in bounded turns rather than until EAGAIN. A command producing
        // output faster than we can paint would otherwise never give the event
        // loop back, and the widget would stop responding to the keystroke
        // that would have stopped it.
        for (int turn = 0; turn < kReadBudgetPerTurn; ++turn) {
            const long n = pty_.read(buf.data(), buf.size());
            if (n > 0) {
                Q_EMIT dataReceived(QByteArray(buf.data(), static_cast<int>(n)));
                continue;
            }
            if (n == 0) {
                ended = true;
            }
            break;  // 0 = gone, -1 = nothing waiting
        }

        if (ended) {
            collectExit();
        }
    }

    void collectExit() {
        if (reported_finished_) {
            return;
        }
        reported_finished_ = true;
        if (notifier_) {
            notifier_->setEnabled(false);
        }
        int status = 0;
        pty_.reap(&status);
        Q_EMIT finished(normalise_status(status));
    }

    Pty pty_;
    std::unique_ptr<QSocketNotifier> notifier_;
    bool reported_finished_ = false;
};

}  // namespace

PtySession *PtySession::create(QObject *parent) {
    return new UnixPtySession(parent);
}

}  // namespace qtpyte
