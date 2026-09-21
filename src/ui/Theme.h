#pragma once

#include <QApplication>
#include <QLabel>

namespace dtb::ui {

void applyTheme(QApplication& app);


// A word-wrapped QLabel's minimumSizeHint is the full single-line width,
// which blows up layout minimums and forces horizontal clipping. Pin the
// minimum width so wrapping actually happens.
inline void wrapLabel(QLabel* label) {
    label->setWordWrap(true);
    label->setMinimumWidth(80);
}

// Single palette source for QSS and custom painting.
namespace colors {
inline constexpr const char* kWindow = "#0d0e11";
inline constexpr const char* kSidebar = "#101116";
inline constexpr const char* kCard = "#15161b";
inline constexpr const char* kCardBorder = "#1f2127";
inline constexpr const char* kHover = "#1a1c22";
inline constexpr const char* kField = "#1a1c22";
inline constexpr const char* kFieldBorder = "#26282f";
inline constexpr const char* kAccent = "#4f8cff";
inline constexpr const char* kText = "#e9ebee";
inline constexpr const char* kTextDim = "#9aa0ab";
inline constexpr const char* kTextFaint = "#62666e";
inline constexpr const char* kWarn = "#ffd166";
inline constexpr const char* kDanger = "#ff5d5d";
inline constexpr const char* kGood = "#4ade80";
} // namespace colors

} // namespace dtb::ui
