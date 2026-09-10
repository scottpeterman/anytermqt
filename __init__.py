# anytermqt/__init__.py
import os
import PySide6                      # must load first: its Qt is the one used
if hasattr(os, "add_dll_directory"):
    os.add_dll_directory(os.path.dirname(PySide6.__file__))
from .anytermqt import *            # the extension module