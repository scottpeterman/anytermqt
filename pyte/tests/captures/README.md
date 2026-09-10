Raw session captures for the differential tests.

Anything ending in `.raw` here is picked up automatically by the `ptdiff_*`
ctest entries and replayed through both reference pyte and `ptdump`. See the
Testing section of the top-level README; `rawcap.py` is what records them.

Keep captures that exercise something the rest of the corpus doesn't. Keep them
small — the point is coverage of escape-sequence shapes, not volume.
