#pragma once
// Sprite rectangles for the classic Winamp 2 skin format (main window only for now).
// Written from the public skin format layout; verify against several real skins
// before trusting edge cases. Coordinates are in unscaled skin pixels.

struct Rect { int x, y, w, h; };

namespace sprites {

inline constexpr int kMainWidth = 275;
inline constexpr int kMainHeight = 116;
inline constexpr int kTitleBarHeight = 14;

// main.bmp (275x116): full background.
inline constexpr Rect kMainBackground{0, 0, 275, 116};

// titlebar.bmp: title strip starts at x=27 (x<27 holds the small buttons).
inline constexpr Rect kTitleActive{27, 0, 275, 14};
inline constexpr Rect kTitleInactive{27, 15, 275, 14};

// titlebar.bmp small buttons, 9x9. Up state / pressed state.
inline constexpr Rect kMenuUp{0, 0, 9, 9},     kMenuDown{0, 9, 9, 9};
inline constexpr Rect kMinimizeUp{9, 0, 9, 9}, kMinimizeDown{9, 9, 9, 9};
inline constexpr Rect kShadeUp{0, 18, 9, 9},   kShadeDown{9, 18, 9, 9};
inline constexpr Rect kCloseUp{18, 0, 9, 9},   kCloseDown{18, 9, 9, 9};

// Where they sit on the main window.
inline constexpr Rect kMenuDst{6, 3, 9, 9};
inline constexpr Rect kMinimizeDst{244, 3, 9, 9};
inline constexpr Rect kShadeDst{254, 3, 9, 9};
inline constexpr Rect kCloseDst{264, 3, 9, 9};

// cbuttons.bmp (136x36): transport buttons, pressed row is y+18 (eject: y+16).
inline constexpr Rect kPrevUp{0, 0, 23, 18},   kPrevDown{0, 18, 23, 18};
inline constexpr Rect kPlayUp{23, 0, 23, 18},  kPlayDown{23, 18, 23, 18};
inline constexpr Rect kPauseUp{46, 0, 23, 18}, kPauseDown{46, 18, 23, 18};
inline constexpr Rect kStopUp{69, 0, 23, 18},  kStopDown{69, 18, 23, 18};
inline constexpr Rect kNextUp{92, 0, 23, 18},  kNextDown{92, 18, 23, 18};
inline constexpr Rect kEjectUp{114, 0, 22, 16}, kEjectDown{114, 16, 22, 16};

inline constexpr Rect kPrevDst{16, 88, 23, 18};
inline constexpr Rect kPlayDst{39, 88, 23, 18};
inline constexpr Rect kPauseDst{62, 88, 23, 18};
inline constexpr Rect kStopDst{85, 88, 23, 18};
inline constexpr Rect kNextDst{108, 88, 23, 18};
inline constexpr Rect kEjectDst{136, 89, 22, 16};

// numbers.bmp: digits 0-9, 9x13 each, left to right.
inline constexpr int kDigitW = 9;
inline constexpr int kDigitH = 13;
inline constexpr Rect digit(int d) { return Rect{d * kDigitW, 0, kDigitW, kDigitH}; }
// Time display digit positions on the main window: M M : S S
inline constexpr int kTimeDigitX[4] = {48, 60, 78, 90};
inline constexpr int kTimeDigitY = 26;

// ---- Window-shade mode (titlebar.bmp) -- ALL coordinates here: verify ------
inline constexpr int kShadeHeight = 14;
inline constexpr Rect kTitleShadeInactive{27, 29, 275, 14};  // verify
inline constexpr Rect kTitleShadeActive{27, 42, 275, 14};    // verify
// In shade mode the shade button becomes "unshade"; verify source rows.
inline constexpr Rect kUnshadeUp{0, 27, 9, 9};   // verify
inline constexpr Rect kUnshadeDown{9, 27, 9, 9}; // verify

// ---- Sliders ---------------------------------------------------------------
// posbar.bmp (307x10): track 248x10 at 0,0; thumb 29x10 at x=248, pressed x=278.
inline constexpr Rect kPosBarDst{16, 72, 248, 10};
inline constexpr Rect kPosBarTrack{0, 0, 248, 10};
inline constexpr Rect kPosThumbUp{248, 0, 29, 10};
inline constexpr Rect kPosThumbDown{278, 0, 29, 10};
inline constexpr int kPosThumbW = 29;

// volume.bmp: 28 background frames, 15px pitch, 68x13 visible each.
// Thumb 14x11 at (15,422) normal / (0,422) pressed.  // verify thumb rows
inline constexpr Rect kVolumeDst{107, 57, 68, 13};
inline constexpr int kSliderFrames = 28;
inline constexpr int kSliderFramePitch = 15;
inline constexpr Rect kSliderThumbUp{15, 422, 14, 11};    // verify
inline constexpr Rect kSliderThumbDown{0, 422, 14, 11};   // verify
inline constexpr int kSliderThumbW = 14;
inline constexpr int kSliderThumbY = 1;                   // verify: offset inside frame

// balance.bmp: same layout as volume.bmp but 38px wide visible; the frame
// index grows with distance from centre. Thumb sprites as above.  // verify
inline constexpr Rect kBalanceDst{177, 57, 38, 13};
inline constexpr int kBalanceSrcX = 9;  // verify: visible 38px start at x=9 in balance.bmp

// ---- Toggles (shufrep.bmp, 92x85) -- ALL coordinates here: verify ----------
// Rows per toggle: off, off+pressed, on, on+pressed (15px apart for repeat/shuffle).
inline constexpr Rect kRepeatDst{210, 89, 28, 15};
inline constexpr Rect kRepeatUp{0, 0, 28, 15}, kRepeatDown{0, 15, 28, 15};
inline constexpr Rect kRepeatOnUp{0, 30, 28, 15}, kRepeatOnDown{0, 45, 28, 15};
inline constexpr Rect kShuffleDst{164, 89, 47, 15};
inline constexpr Rect kShuffleUp{28, 0, 47, 15}, kShuffleDown{28, 15, 47, 15};
inline constexpr Rect kShuffleOnUp{28, 30, 47, 15}, kShuffleOnDown{28, 45, 47, 15};
// EQ / playlist: 23x12; off row y=61, on row y=73; pressed variants 46px / 69px to the right.
inline constexpr Rect kEqDst{219, 58, 23, 12};
inline constexpr Rect kEqUp{0, 61, 23, 12}, kEqDown{46, 61, 23, 12};
inline constexpr Rect kEqOnUp{0, 73, 23, 12}, kEqOnDown{46, 73, 23, 12};
inline constexpr Rect kPlDst{242, 58, 23, 12};
inline constexpr Rect kPlUp{23, 61, 23, 12}, kPlDown{69, 61, 23, 12};
inline constexpr Rect kPlOnUp{23, 73, 23, 12}, kPlOnDown{69, 73, 23, 12};

// ---- Play-state indicator (playpaus.bmp, 9x9 cells) -- verify ---------------
inline constexpr Rect kStateDst{26, 28, 9, 9};            // verify
inline constexpr Rect kStatePlaying{0, 0, 9, 9};          // verify
inline constexpr Rect kStatePaused{9, 0, 9, 9};           // verify
inline constexpr Rect kStateStopped{18, 0, 9, 9};         // verify


// ---- Song info display (verify all) ---------------------------------------
// text.bmp: 5x6 glyphs. Row 0: A-Z " @ ; row 1: 0-9 ... ; row 2: umlauts ? *
inline constexpr int kGlyphW = 5, kGlyphH = 6;
inline Rect glyph(wchar_t c) {
    if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    int col = 30, row = 0;  // blank cell
    if (c >= L'A' && c <= L'Z') { row = 0; col = c - L'A'; }
    else if (c >= L'0' && c <= L'9') { row = 1; col = c - L'0'; }
    else {
        constexpr wchar_t kRow1[] = L"~.:()-'!_+\\/[]^&%,=$#";  // cols 10..30 (col 10 is an ellipsis stand-in)
        constexpr wchar_t kRow0[] = L"\"@";                        // cols 26..27
        const wchar_t* p = nullptr;
        for (int i = 0; kRow0[i]; ++i) if (kRow0[i] == c) { row = 0; col = 26 + i; p = kRow0; }
        if (!p) for (int i = 1; kRow1[i]; ++i) if (kRow1[i] == c) { row = 1; col = 10 + i; p = kRow1; }
        if (!p && c == L'?') { row = 2; col = 3; }
        if (!p && c == L'*') { row = 2; col = 4; }
    }
    return Rect{col * kGlyphW, row * kGlyphH, kGlyphW, kGlyphH};
}
inline constexpr Rect kInfoTextArea{111, 24, 154, 6};   // scrolling title
inline constexpr Rect kBitrateDst{111, 43, 15, 6};
inline constexpr Rect kSampleRateDst{156, 43, 10, 6};
// monoster.bmp (56x24): active row 0, inactive row 12.
inline constexpr Rect kMonoDst{212, 41, 27, 12};
inline constexpr Rect kStereoDst{239, 41, 29, 12};
inline constexpr Rect kMonoOn{29, 0, 27, 12}, kMonoOff{29, 12, 27, 12};
inline constexpr Rect kStereoOn{0, 0, 29, 12}, kStereoOff{0, 12, 29, 12};


// ---- Spectrum analyzer area in the main window (verify) ---------------------
inline constexpr Rect kVisDst{24, 43, 76, 16};
inline constexpr int kVisBars = 19, kVisBarW = 3, kVisBarStep = 4;

}  // namespace sprites
