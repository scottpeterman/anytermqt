"""scripts/_wheelcheck.py

The parts of the wheel scripts that are easier to write once in Python than
twice in two shells.

wheel.bat had these inline, as `python -c` one-liners inside `for /f`. That
does not survive cmd's quoting rules: a regex containing quotes and brackets
gets torn apart before Python ever sees it, and the error names a fragment of
the expression rather than the problem. The shell version worked but used a
separate sed implementation of the same parse, which is one more thing that
can drift.

Called by scripts/wheel.sh and scripts/wheel.bat. Not part of the package and
not shipped in the wheel.

    python scripts/_wheelcheck.py pin pyproject.toml
    python scripts/_wheelcheck.py contents dist/anytermqt-....whl
    python scripts/_wheelcheck.py excludes dist/anytermqt-....whl
    python scripts/_wheelcheck.py grafted dist/manylinux/anytermqt-....whl
    python scripts/_wheelcheck.py verify
"""

import json
import pathlib
import re
import shutil
import sys
import tempfile
import zipfile

PIN_RE = re.compile(r"""dependencies\s*=\s*\[\s*["']PySide6==([0-9][0-9.]*)["']""")


def pin(path):
    """Print the exact PySide6 version pinned in pyproject.toml.

    Printed alone on stdout so the callers can capture it -- everything else
    here goes to stderr for that reason.
    """
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        print("cannot read %s: %s" % (path, exc), file=sys.stderr)
        return 1

    match = PIN_RE.search(text)
    if not match:
        print("no PySide6 pin found in %s" % path, file=sys.stderr)
        print('expected: dependencies = ["PySide6==6.10.3"]', file=sys.stderr)
        return 1

    print(match.group(1))
    return 0


def contents(path):
    """Check the wheel actually holds a module.

    A wheel with nothing in it is a valid wheel. If the install() rules in
    bindings/CMakeLists.txt did not run, or wrote somewhere other than the
    package directory, everything up to this point still reports success.
    """
    try:
        names = zipfile.ZipFile(path).namelist()
    except (OSError, zipfile.BadZipFile) as exc:
        print("[wheel] cannot read %s: %s" % (path, exc), file=sys.stderr)
        return 1

    modules = [n for n in names
               if n.startswith("anytermqt/") and n.endswith((".so", ".pyd", ".dylib"))]
    init = [n for n in names if n == "anytermqt/__init__.py"]

    if not modules:
        print("[wheel] No extension module inside the wheel. The install() rules")
        print("[wheel] in bindings/CMakeLists.txt did not run, or wrote elsewhere.")
        for name in names[:20]:
            print("[wheel]   %s" % name)
        return 1

    if not init:
        print("[wheel] No anytermqt/__init__.py inside the wheel -- check")
        print("[wheel] wheel.packages in pyproject.toml.")
        return 1

    print("[wheel] Contains %s" % modules[0])
    return 0


def excludes(path):
    """Print the SONAMEs auditwheel must not graft, one per line.

    The rule is the design: this wheel vendors nothing. Everything the module
    links resolves out of the process at run time -- Qt and Shiboken from
    PySide6's site-packages, the rest from the system -- so every dependency
    auditwheel would otherwise copy in has to be excluded, and its only
    remaining job is computing the platform tag.

    Derived rather than listed. A hand-written pattern for Qt, PySide6 and
    Shiboken looks complete and is not: Qt6Gui drags in libOpenGL.so.0, which
    matches none of those names and is absent from the manylinux allowlist, so
    auditwheel grafts it along with libGLX and libGLdispatch. Vendoring those
    three is worse than pointless -- libglvnd expects one dispatch table per
    process, and a second copy inside the wheel is how a working install stops
    finding the system's GL driver.

    PySide6 avoids this by linking libGL.so.1, which is allowlisted. This
    module links libOpenGL.so.0 instead, because that is what Qt's own
    imported targets put on the link line.

    So: subtract the allowlist from the module's direct dependencies. Anything
    already allowlisted is left alone, since auditwheel would not graft it and
    excluding it would only prune the symbol versions the tag is computed
    from.
    """
    try:
        names = zipfile.ZipFile(path).namelist()
    except (OSError, zipfile.BadZipFile) as exc:
        print("cannot read %s: %s" % (path, exc), file=sys.stderr)
        return 1

    modules = [n for n in names
               if n.startswith("anytermqt/") and n.endswith((".so", ".dylib"))]
    if not modules:
        print("no extension module in %s" % path, file=sys.stderr)
        return 1

    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        print("pyelftools not importable -- it ships with auditwheel",
              file=sys.stderr)
        return 1

    tmp = tempfile.mkdtemp()
    try:
        extracted = zipfile.ZipFile(path).extract(modules[0], tmp)
        with open(extracted, "rb") as fh:
            elf = ELFFile(fh)
            section = elf.get_section_by_name(".dynamic")
            if section is None:
                print("%s has no dynamic section" % modules[0], file=sys.stderr)
                return 1
            needed = [t.needed for t in section.iter_tags("DT_NEEDED")]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    allowed = _allowlist()
    if allowed is None:
        print("could not read auditwheel's manylinux policy", file=sys.stderr)
        return 1

    for soname in needed:
        if soname not in allowed:
            print(soname)
    return 0


def _allowlist():
    """The union of every manylinux policy's allowlist.

    The union rather than one policy's, because the target policy is not known
    until auditwheel has run -- and the allowlists are nested anyway, so the
    union is the most permissive, which is the safe direction here: a library
    wrongly treated as allowed gets grafted and the check downstream catches
    it, while one wrongly excluded silently lowers the tag.
    """
    try:
        import auditwheel
        policy = (pathlib.Path(auditwheel.__file__).parent
                  / "policy" / "manylinux-policy.json")
        with policy.open(encoding="utf-8") as fh:
            data = json.load(fh)
    except (ImportError, OSError, ValueError):
        return None

    allowed = set()
    for entry in data:
        for key in ("lib_whitelist", "lib_allowlist"):
            allowed.update(entry.get(key, ()))
    return allowed or None


def grafted(path):
    """Check auditwheel retagged the wheel without vendoring anything into it.

    Linux only, and the reason the repair step is not just a call to
    auditwheel. Vendoring is auditwheel's main job; here it is the failure.
    One SONAME missing from the exclude list and it copies in a second Qt --
    which is the crash the wheel is built to avoid -- and reports success
    either way. The difference is visible in the tag and nowhere else, so it
    has to be asserted rather than read.

    auditwheel names the directory after the package: anytermqt.libs on
    Linux, anytermqt.dylibs on macOS.
    """
    try:
        names = zipfile.ZipFile(path).namelist()
    except (OSError, zipfile.BadZipFile) as exc:
        print("[wheel] cannot read %s: %s" % (path, exc), file=sys.stderr)
        return 1

    libs = [n for n in names
            if re.search(r"\.(libs|dylibs)/", n) and not n.endswith("/")]

    if libs:
        print("[wheel] auditwheel grafted %d shared libraries into the wheel."
              % len(libs), file=sys.stderr)
        print("[wheel] A SONAME is missing from the exclude list in wheel.sh,",
              file=sys.stderr)
        print("[wheel] so the wheel now carries its own copy of what should",
              file=sys.stderr)
        print("[wheel] resolve out of PySide6. Grafted:", file=sys.stderr)
        for name in sorted(n.split("/")[-1] for n in libs)[:10]:
            print("[wheel]   %s" % name, file=sys.stderr)
        if len(libs) > 10:
            print("[wheel]   ... and %d more" % (len(libs) - 10), file=sys.stderr)
        return 1

    print("[wheel] Nothing grafted -- still the module and __init__.py alone")
    return 0


def verify():
    """Import the installed package and say where it came from.

    Run by a different interpreter than the rest of this script: the test
    virtualenv's, from a working directory outside the repository. Living in
    scripts/ is deliberate -- sys.path[0] becomes scripts/, which holds no
    package named anytermqt, so nothing in the checkout can satisfy the
    import in place of the wheel.
    """
    import anytermqt

    print("[wheel] imported from %s" % anytermqt.__file__)
    print("[wheel] version %s" % anytermqt.__version__)
    print("[wheel] exports %s" % ", ".join(anytermqt.__all__))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    command = argv[1]
    if command == "pin" and len(argv) == 3:
        return pin(argv[2])
    if command == "contents" and len(argv) == 3:
        return contents(argv[2])
    if command == "excludes" and len(argv) == 3:
        return excludes(argv[2])
    if command == "grafted" and len(argv) == 3:
        return grafted(argv[2])
    if command == "verify" and len(argv) == 2:
        return verify()

    print("usage: _wheelcheck.py "
          "{pin <pyproject>|contents <wheel>|excludes <wheel>|"
          "grafted <wheel>|verify}",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))