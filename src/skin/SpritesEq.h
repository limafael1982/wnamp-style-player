#pragma once
// Sprite rectangles for the equalizer window (eq.bmp). Coordinates are in unscaled skin pixels.
// Written from the public Winamp 2 skin layout; anything marked "verify" should be checked
// against several real skins.
#include "skin/Sprites.h"  // Rect

namespace sprites_eq {

inline constexpr int kWidth = 275;
inline constexpr int kHeight = 116;
inline constexpr int kTitleBarHeight = 14;

// eq.bmp
inline constexpr Rect kBackground{0, 0, 275, 116};
inline constexpr Rect kTitleActive{0, 134, 275, 14};
inline constexpr Rect kTitleInactive{0, 149, 275, 14};

// Close button: normal / pressed (verify: eq.bmp (0,116) and (0,125)).
inline constexpr Rect kCloseUp{0, 116, 9, 9};
inline constexpr Rect kCloseDown{0, 125, 9, 9};
inline constexpr Rect kCloseDst{264, 3, 9, 9};

// ON toggle (verify all four).
inline constexpr Rect kOnDst{14, 18, 26, 12};
inline constexpr Rect kOnOffUp{10, 119, 26, 12};
inline constexpr Rect kOnOffDown{128, 119, 26, 12};
inline constexpr Rect kOnOnUp{69, 119, 26, 12};
inline constexpr Rect kOnOnDown{187, 119, 26, 12};

// AUTO toggle (verify all four).
inline constexpr Rect kAutoDst{39, 18, 32, 12};
inline constexpr Rect kAutoOffUp{36, 119, 32, 12};
inline constexpr Rect kAutoOffDown{154, 119, 32, 12};
inline constexpr Rect kAutoOnUp{95, 119, 32, 12};
inline constexpr Rect kAutoOnDown{213, 119, 32, 12};

// Sliders: index 0 = preamp, 1..10 = bands. Each slot is 14x63 on the window.
inline constexpr int kSliderW = 14;
inline constexpr int kSliderH = 63;
inline constexpr int kSliderY = 38;       // verify
inline constexpr int kPreampX = 21;       // verify
inline constexpr int kBandX0 = 78;        // verify
inline constexpr int kBandPitch = 18;     // verify

// Slider background strip: 28 frames in 2 rows of 14 (verify layout and that frame 0 = top / +12 dB).
inline constexpr int kFrameCount = 28;
inline constexpr int kFrameColumns = 14;
inline constexpr Rect sliderFrame(int f) {
    return Rect{13 + (f % kFrameColumns) * 15, 164 + (f / kFrameColumns) * 65, kSliderW, kSliderH};
}

// Thumb: normal / pressed (verify). Horizontal offset inside the 14px slot is a guess.
inline constexpr int kThumbW = 11;
inline constexpr int kThumbH = 11;
inline constexpr int kThumbDx = 1;  // verify
inline constexpr Rect kThumbUp{0, 164, 11, 11};
inline constexpr Rect kThumbDown{0, 176, 11, 11};

// ---- Response curve (all verify) -------------------------------------------
// Curve area on the window, and the eq.bmp pieces used to draw it.
inline constexpr Rect kCurveArea{86, 17, 113, 19};   // verify
inline constexpr Rect kGraphBg{0, 294, 113, 19};     // verify: graph background in eq.bmp
inline constexpr Rect kPreampLine{0, 314, 113, 1};   // verify: 1px zero/preamp line
inline constexpr int kLineColorX = 115;              // verify: 1x19 column of line colours
inline constexpr int kLineColorY = 294;              // verify

// ---- PRESETS button (verify) -------------------------------------------------
inline constexpr Rect kPresetsDst{217, 18, 44, 12};
inline constexpr Rect kPresetsUp{224, 164, 44, 12};
inline constexpr Rect kPresetsDown{224, 176, 44, 12};

}  // namespace sprites_eq
