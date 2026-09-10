"""python/anytermqt/__init__.py

A native Qt terminal widget for PySide6.

This file is not a convenience wrapper. It runs on the user's machine at the
moment they type `import anytermqt`, before the extension module is loaded,
and on Windows that timing is what makes the import work at all.

The extension links Qt, libpyside6 and libshiboken6. None of those are in the
wheel -- they are in PySide6's and shiboken6's install directories, a couple
of levels away in site-packages. On Linux and macOS the loader finds them
through an rpath baked into the binary at link time (see the INSTALL_RPATH
block in bindings/CMakeLists.txt). Windows has no rpath: the DLL search path
is a runtime property of the process, so it has to be set here, by Python,
before the .pyd is loaded.

Importing PySide6 first is the other half of it, and it matters everywhere.
PySide6 loads its own Qt on import and that copy wins for the rest of the
process. Letting it go first means there is one Qt in the process rather than
two -- which is the same invariant scripts/env.sh checks at build time, held
at run time instead.
"""

import os

# First, and not merely for the side effect on the DLL path below.
import PySide6
import shiboken6

if hasattr(os, "add_dll_directory"):  # Windows only
    # Qt6Core.dll and pyside6.abi3.dll sit in the PySide6 package directory;
    # shiboken6.abi3.dll sits in its own. PySide6 registers its directory when
    # it is imported, so the first of these is usually redundant -- it is here
    # because "usually" depends on a PySide6 implementation detail, and the
    # call costs nothing.
    for _pkg in (PySide6, shiboken6):
        _dir = os.path.dirname(os.path.abspath(_pkg.__file__))
        if os.path.isdir(_dir):
            os.add_dll_directory(_dir)
    del _pkg, _dir

# The extension module, which is named the same as the package and lives
# inside it: anytermqt/anytermqt.<abi>.<so|pyd>.
#
# Listed by name rather than with a wildcard, for the same reason
# bindings/CMakeLists.txt lists the generated sources by name: a class added
# to the typesystem and forgotten here fails immediately and says so, instead
# of being absent from Python while the C++ works.
from .anytermqt import (  # noqa: E402
    Palette,
    PtySession,
    TerminalWidget,
)

__all__ = ["Palette", "PtySession", "TerminalWidget"]

try:
    from importlib.metadata import version as _version

    __version__ = _version("anytermqt")
except Exception:  # running from a source tree with nothing installed
    __version__ = "0.0.0+unknown"