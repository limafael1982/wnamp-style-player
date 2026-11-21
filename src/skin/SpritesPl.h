#pragma once
// pledit.bmp sprite rectangles for the Winamp 2 playlist window frame.
// Written from the classic skin layout, not from Webamp's code. Coordinates are in
// unscaled skin pixels. Everything marked 'verify' should be checked against real skins.
#include "skin/Sprites.h"  // Rect

namespace sprites_pl {

inline constexpr int kDefaultWidth = 275;
inline constexpr int kDefaultHeight = 232;
inline constexpr int kTopH = 20;     // top bar height
inline constexpr int kBottomH = 38;  // bottom bar height
inline constexpr int kLeftW = 12;    // left border width      (verify)
inline constexpr int kRightW = 20;   // right border width     (verify)
inline constexpr int kRowH = 13;     // list row height        (verify)

// Top bar, active ("selected") window.
inline constexpr Rect kTopLeftActive{0, 0, 25, 20};
inline constexpr Rect kTitleActive{26, 0, 100, 20};
inline constexpr Rect kTopTileActive{127, 0, 25, 20};
inline constexpr Rect kTopRightActive{153, 0, 25, 20};
// Top bar, inactive window.  (verify: y=21)
inline constexpr Rect kTopLeftInactive{0, 21, 25, 20};
inline constexpr Rect kTitleInactive{26, 21, 100, 20};
inline constexpr Rect kTopTileInactive{127, 21, 25, 20};
inline constexpr Rect kTopRightInactive{153, 21, 25, 20};

// Side borders, tiled vertically. (verify: left border x=0..11, right border x=31..50)
inline constexpr Rect kLeftTile{0, 42, 12, 29};
inline constexpr Rect kRightTile{31, 42, 20, 29};

// Bottom bar. The ADD/REM/SEL/MISC/LIST buttons and mini transport are baked into it.
inline constexpr Rect kBottomLeft{0, 72, 125, 38};
inline constexpr Rect kBottomTile{179, 0, 25, 38};  // verify
inline constexpr Rect kBottomRight{126, 72, 150, 38};

// Hit areas (verify all). Close button sits in the top-right corner: x = width - 11.
inline constexpr int kCloseFromRight = 11, kCloseY = 3, kCloseSize = 9;
// Bottom buttons: y = height - 30, 18 px tall.
inline constexpr int kBtnFromBottom = 30, kBtnH = 18, kBtnW = 25;
inline constexpr int kAddX = 11, kRemX = 40, kSelX = 69, kMiscX = 98;

// LOAD LIST / list-options button at the bottom-right (verify).
inline constexpr int kLoadFromRight = 48;
// Resize grip in the bottom-right corner, and the resize steps used by classic skins:
// width grows in 25 px steps, height in 29 px steps (verify against real skins).
inline constexpr int kGripSize = 20;
inline constexpr int kStepX = 25, kStepY = 29;
inline constexpr int kMinW = 275, kMinH = 116;
inline constexpr int kMaxW = 275 + 25 * 20, kMaxH = 116 + 29 * 20;

}  // namespace sprites_pl
