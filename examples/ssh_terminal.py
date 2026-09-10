# examples/ssh_terminal.py
#
# SSH in a TerminalWidget, over paramiko. A small connect dialog, then a live
# session.
#
# The point of the example is that nothing in the widget knows what SSH is.
# It has three connection points and they are the same three whether the other
# end is a local shell or a socket on another continent:
#
#   channel bytes           -> widget.feed(QByteArray)
#   widget.dataReady        -> channel.send(bytes)
#   widget.resized          -> channel.resize_pty(cols, rows)
#
# PtySession is one implementation of that contract, not a requirement. This
# file is the other one, and it needs no C++ at all.
#
#   pip install anytermqt paramiko
#   python examples/ssh_terminal.py
#
# Deliberately thin. There is no host key persistence, no key-file picker, no
# reconnect, no jump host. Those belong in an application; what belongs here is
# the wiring, small enough to read in one sitting.

from __future__ import annotations

import ipaddress
import socket
import sys
import threading

from PySide6.QtCore import QByteArray, QObject, Qt, Signal
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QSpinBox,
    QVBoxLayout,
)

import anytermqt
import paramiko

# 100.64.0.0/10. Carrier-grade NAT space, and on network gear it is usually
# not where the box actually is: an inventory record that has gone stale, or a
# management address behind a translation that will not accept a session. DNS
# is the more reliable answer when the two disagree, so a CGNAT literal gets
# resolved rather than dialled.
CGNAT = ipaddress.ip_network("100.64.0.0/10")


def resolve_host(host: str) -> tuple[str, str | None]:
    """Return (address to connect to, note about what happened).

    Anything that is not a CGNAT literal is returned untouched -- a hostname
    is left for paramiko to resolve, and an ordinary address is used as given.

    For a CGNAT literal the address is turned back into a name and the name
    resolved forward again. When that produces something outside the range it
    is preferred, on the assumption that the DNS record is current and the
    literal is not. When it does not, the original is used and the note says
    so; the point is to make the substitution visible either way, never to
    silently connect somewhere other than where you asked.
    """
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        return host, None

    if address not in CGNAT:
        return host, None

    try:
        name = socket.gethostbyaddr(host)[0]
    except OSError as exc:
        return host, f"{host} is CGNAT space; no reverse record ({exc})."

    try:
        resolved = socket.gethostbyname(name)
    except OSError as exc:
        return host, f"{host} is CGNAT space; {name} did not resolve ({exc})."

    if resolved == host:
        return host, f"{host} is CGNAT space; {name} resolves to the same address."

    return resolved, f"{host} is CGNAT space; using {resolved} from {name}."


class ConnectDialog(QDialog):
    """Host, port, user, password. Nothing else."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Connect")

        self.host = QLineEdit()
        self.host.setPlaceholderText("hostname or address")
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.port.setValue(22)
        self.user = QLineEdit()
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.EchoMode.Password)

        # An option rather than a policy. Verifying against known_hosts is the
        # right default anywhere it can succeed, and on a lab bench that is
        # rebuilt weekly it succeeds for nothing -- every box is a new key and
        # the check becomes a prompt to click through, which teaches the wrong
        # reflex faster than no check at all. Made explicit and visible here so
        # it stays a decision instead of a line buried in the source.
        self.trust_new = QCheckBox("Accept unknown host keys")
        self.trust_new.setChecked(True)
        self.trust_new.setToolTip(
            "Unchecked: the host must already be in ~/.ssh/known_hosts.\n"
            "Checked: unknown keys are accepted for this session only and\n"
            "not written anywhere. Convenient on a lab bench, wrong on\n"
            "anything you care about."
        )

        form = QFormLayout()
        form.addRow("Host", self.host)
        form.addRow("Port", self.port)
        form.addRow("User", self.user)
        form.addRow("Password", self.password)
        form.addRow("", self.trust_new)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)

        self.host.setFocus()

    def values(self) -> dict:
        return {
            "host": self.host.text().strip(),
            "port": self.port.value(),
            "user": self.user.text().strip(),
            "password": self.password.text(),
            "trust_new": self.trust_new.isChecked(),
        }


class SshSession(QObject):
    """A paramiko shell channel, shaped like something a widget can talk to.

    The reader runs in a thread because channel.recv blocks, and Qt widgets
    may only be touched from the thread that owns them. Everything the reader
    learns therefore leaves through a signal: emitting across threads gives a
    queued connection, so feed() lands on the GUI thread even though the bytes
    arrived on another one. Calling widget.feed() from the reader directly
    would appear to work and would corrupt the screen under load.
    """

    dataReceived = Signal(QByteArray)
    disconnected = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._client: paramiko.SSHClient | None = None
        self._channel: paramiko.Channel | None = None
        self._reader: threading.Thread | None = None
        self._closing = False

    def connect_to(self, host, port, user, password, trust_new, cols, rows):
        client = paramiko.SSHClient()
        client.load_system_host_keys()
        client.set_missing_host_key_policy(
            paramiko.AutoAddPolicy() if trust_new else paramiko.RejectPolicy()
        )

        client.connect(
            hostname=host,
            port=port,
            username=user,
            password=password,
            look_for_keys=False,
            allow_agent=False,
            timeout=15,
        )

        # The size is passed at open time as well as on resize. Without it the
        # far end assumes 80x24 and anything full-screen -- a pager, an editor,
        # a status display -- draws to the wrong shape until the first resize
        # happens to correct it.
        self._channel = client.invoke_shell(term="xterm-256color",
                                            width=cols, height=rows)
        self._client = client

        # Network gear drops idle sessions quietly and the symptom is a
        # terminal that simply stops responding.
        client.get_transport().set_keepalive(30)

        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self):
        try:
            while not self._closing:
                data = self._channel.recv(8192)
                if not data:
                    break
                self.dataReceived.emit(QByteArray(data))
        except Exception as exc:
            if not self._closing:
                self.disconnected.emit(str(exc))
                return
        if not self._closing:
            self.disconnected.emit("closed by remote host")

    def write(self, data: QByteArray):
        if self._channel is not None and not self._closing:
            try:
                self._channel.send(bytes(data))
            except OSError:
                pass

    def resize(self, cols: int, rows: int):
        if self._channel is not None and not self._closing:
            try:
                self._channel.resize_pty(width=cols, height=rows)
            except OSError:
                pass

    def close(self):
        self._closing = True
        for handle in (self._channel, self._client):
            if handle is not None:
                try:
                    handle.close()
                except Exception:
                    pass
        self._channel = None
        self._client = None


class SshWindow(QMainWindow):
    def __init__(self, settings: dict):
        super().__init__()
        self.terminal = anytermqt.TerminalWidget()
        self.terminal.setTerminalFont(QFont("Menlo, Consolas, monospace", 11))
        self.setCentralWidget(self.terminal)
        self.resize(900, 560)

        self.session = SshSession(self)

        # The three connections the whole example exists to show.
        self.session.dataReceived.connect(self.terminal.feed)
        self.terminal.dataReady.connect(self.session.write)
        self.terminal.resized.connect(self.session.resize)

        self.session.disconnected.connect(self._on_disconnected)

        self._connect(settings)

    def _connect(self, settings):
        host, note = resolve_host(settings["host"])
        if note:
            self._notice(note)

        self.setWindowTitle(f"{settings['user']}@{settings['host']}")
        self._notice(f"Connecting to {host}:{settings['port']} ...")

        try:
            self.session.connect_to(
                host=host,
                port=settings["port"],
                user=settings["user"],
                password=settings["password"],
                trust_new=settings["trust_new"],
                cols=self.terminal.columns(),
                rows=self.terminal.terminalRows(),
            )
        except paramiko.SSHException as exc:
            # Includes the unknown-host-key rejection, which is the one people
            # hit first with the checkbox cleared.
            self._notice(f"SSH error: {exc}")
        except OSError as exc:
            self._notice(f"Could not connect: {exc}")

    def _notice(self, message: str):
        """Write to the screen, not to stdout. A GUI has no console."""
        self.terminal.feed(QByteArray(f"\x1b[36m{message}\x1b[0m\r\n".encode()))

    def _on_disconnected(self, reason: str):
        self._notice(f"Disconnected: {reason}")

    def closeEvent(self, event):
        self.session.close()
        super().closeEvent(event)


def main() -> int:
    app = QApplication(sys.argv)

    dialog = ConnectDialog()
    if dialog.exec() != QDialog.DialogCode.Accepted:
        return 0

    settings = dialog.values()
    if not settings["host"] or not settings["user"]:
        QMessageBox.warning(None, "Connect", "Host and user are required.")
        return 2

    window = SshWindow(settings)
    window.show()
    window.terminal.setFocus()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())