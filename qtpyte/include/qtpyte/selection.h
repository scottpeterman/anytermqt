// include/qtpyte/selection.h
//
// A text selection, in absolute line coordinates.
//
// Anchor is where the drag started and does not move; focus follows the
// pointer. Neither is a viewport coordinate, because a viewport coordinate
// stops meaning the same text the moment output scrolls -- the highlight
// would crawl up the screen while the user held still. Absolute lines stay
// pinned to their text and are converted to viewport rows only when painting.
//
// The focus may be above the anchor, so nothing here assumes an order;
// normalized() supplies one where an order is needed.

#pragma once

namespace qtpyte {

struct Selection {
    int anchorLine = 0;
    int anchorCol = 0;
    int focusLine = 0;
    int focusCol = 0;

    // A selection exists and should be painted.
    bool active = false;
    // The primary button is down and the focus is still moving.
    bool dragging = false;

    void clear() {
        active = false;
        dragging = false;
        anchorLine = anchorCol = focusLine = focusCol = 0;
    }

    // Nothing between the two ends, which is what a plain click produces.
    bool isEmpty() const {
        return anchorLine == focusLine && anchorCol == focusCol;
    }

    // Start before end, whichever way the drag went. End column is exclusive.
    void normalized(int &startLine, int &startCol, int &endLine,
                    int &endCol) const {
        const bool forward = anchorLine < focusLine ||
                             (anchorLine == focusLine && anchorCol <= focusCol);
        if (forward) {
            startLine = anchorLine;
            startCol = anchorCol;
            endLine = focusLine;
            endCol = focusCol;
        } else {
            startLine = focusLine;
            startCol = focusCol;
            endLine = anchorLine;
            endCol = anchorCol;
        }
    }

    // The selected span on one absolute line, as [from, to) grid columns.
    // Returns false when the line is outside the selection entirely, so a
    // painter can skip it without arithmetic.
    bool spanOnLine(int line, int cols, int &from, int &to) const {
        if (!active) {
            return false;
        }
        int startLine = 0;
        int startCol = 0;
        int endLine = 0;
        int endCol = 0;
        normalized(startLine, startCol, endLine, endCol);

        if (line < startLine || line > endLine) {
            return false;
        }
        from = (line == startLine) ? startCol : 0;
        to = (line == endLine) ? endCol : cols;
        return to > from;
    }
};

}  // namespace qtpyte