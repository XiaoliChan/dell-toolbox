#include "ui/widgets/NavIcons.h"

#include <QPainter>
#include <QPixmap>

namespace dtb::ui {

namespace {
// Draw one row of a slider glyph: the track is split around the knob so the
// knob reads cleanly on any background (no background-colored patches).
void sliderRow(QPainter& p, qreal y, qreal knobX, const QColor& color) {
    constexpr qreal kLeft = 2.8;
    constexpr qreal kRight = 19.2;
    constexpr qreal kKnobR = 2.6;
    constexpr qreal kStroke = 1.6;
    if (knobX - kKnobR > kLeft) {
        p.drawLine(QPointF(kLeft, y), QPointF(knobX - kKnobR - 0.9, y));
    }
    if (knobX + kKnobR < kRight) {
        p.drawLine(QPointF(knobX + kKnobR + 0.9, y), QPointF(kRight, y));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(knobX, y), kKnobR, kKnobR);
    p.setPen(QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
}
} // namespace

QIcon makeNavIcon(int kind, const QColor& color) {
    // 22-logical canvas rendered at 4x: stays crisp at 125%/150% Windows
    // display scaling, where a 2x bitmap visibly blurs.
    QPixmap pm(88, 88);
    pm.setDevicePixelRatio(4);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case NavIconKind::Dashboard: { // 2x2 rounded tiles, generous gutters so the
        // four blocks stay distinct at small sizes and high DPI
        constexpr qreal kTile = 6.4, kGap = 2.8;
        const qreal x2 = 3.2 + kTile + kGap, y2 = x2;
        p.drawRoundedRect(QRectF(3.2, 3.2, kTile, kTile), 1.9, 1.9);
        p.drawRoundedRect(QRectF(x2, 3.2, kTile, kTile), 1.9, 1.9);
        p.drawRoundedRect(QRectF(3.2, y2, kTile, kTile), 1.9, 1.9);
        p.drawRoundedRect(QRectF(x2, y2, kTile, kTile), 1.9, 1.9);
        break;
    }
    case NavIconKind::Thermal: { // thermometer with stem ticks
        p.drawRoundedRect(QRectF(9.6, 2.8, 2.8, 10.4), 1.4, 1.4);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(11.0, 15.8), 3.2, 3.2);
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(15.2, 5.6), QPointF(17.4, 5.6));
        p.drawLine(QPointF(15.2, 9.0), QPointF(17.4, 9.0));
        break;
    }
    case NavIconKind::Power: // lightning bolt
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        {
            static const QPointF bolt[6] = {{12.3, 2.6}, {5.9, 12.1}, {10.1, 12.1},
                                            {9.7, 19.4}, {16.1, 9.9}, {11.9, 9.9}};
            p.drawPolygon(bolt, 6);
        }
        break;
    case NavIconKind::Battery: {
        p.drawRoundedRect(QRectF(3.1, 6.7, 13.9, 9.0), 2.3, 2.3);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(18.2, 9.7, 1.8, 2.9), 0.9, 0.9); // cap
        p.drawRoundedRect(QRectF(5.3, 8.8, 5.2, 4.8), 1.3, 1.3);  // charge level
        break;
    }
    default: // settings: two sliders with offset knobs
        sliderRow(p, 6.9, 13.4, color);
        sliderRow(p, 15.1, 7.8, color);
        break;
    }
    p.end();
    return QIcon(pm);
}

} // namespace dtb::ui
