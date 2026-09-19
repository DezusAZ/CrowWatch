// SquachWatch-CYD — where a list stops scrolling.
//
// Every scrolling screen on this device clamped at the top and nowhere
// else, so all of them ran on into empty space: keep dragging and the rows
// you wanted end up above the fold with nothing underneath but background,
// and on the screens with a pinned button strip the bottom rows drift out
// from under your thumb. Eight screens had the same two lines of the same
// bug.
//
// The stop is two rows of slack under the last item rather than a hard
// bottom edge. A list that halts with its last row jammed against the strip
// looks stuck; a little air reads as "that is the end".
//
// This is the uniform-row version, which is seven of the eight. Settings has
// its own because its rows are three different heights -- BACKGROUND and
// OUTFIT take two lines and group headers take less than one -- so the
// answer there is not arithmetic on a row count and has to be measured
// backwards from the end of the list.
#pragma once

// Clamps `scroll` in place. `bodyH` is the height the rows are drawn into
// and `rowH` the height of one, both in device pixels; `rows` is how many
// there are in total.
inline void uiClampScroll(int& scroll, int rows, int bodyH, int rowH, int slack = 2) {
    if (scroll < 0) scroll = 0;
    if (rowH <= 0) return;
    const int visible = bodyH / rowH;
    int most = rows - visible + slack;
    if (most < 0) most = 0;
    if (scroll > most) scroll = most;
}
