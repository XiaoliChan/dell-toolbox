#pragma once

#include <QIcon>

namespace dtb::ui {

namespace NavIconKind {
inline constexpr int Dashboard = 0;
inline constexpr int Thermal = 1;
inline constexpr int Power = 2;
inline constexpr int Battery = 3;
inline constexpr int Settings = 4;
} // namespace NavIconKind

// Hand-painted 20x20 icons (renderer-independent; no font glyphs).
QIcon makeNavIcon(int kind, const QColor& color);

} // namespace dtb::ui
