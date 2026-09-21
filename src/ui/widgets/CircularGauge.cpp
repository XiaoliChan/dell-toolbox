#include "ui/widgets/CircularGauge.h"

#include <QPainter>

#include "ui/Theme.h"

namespace dtb::ui {

CircularGauge::CircularGauge(const QString& caption, int maxValue, const QString& unit, QWidget* parent)
    : QWidget(parent), m_caption(caption), m_unit(unit), m_maxValue(maxValue) {
    setMinimumSize(0, 150); // width flexible: layouts may compress, paint adapts
}

void CircularGauge::setValue(int value) {
    m_value = value;
    m_hasValue = true;
    update();
}

void CircularGauge::setUnit(const QString& unit) {
    m_unit = unit;
    update();
}

void CircularGauge::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Content = ring + caption, anchored as one block and centred vertically.
    // Captions anchor to the arc (not the widget bottom), removing the large
    // gap that appeared when layouts stretched the widget vertically.
    const int ring = qMin(width(), height() - 46);
    const int arcD = qMax(40, ring - 24);
    const qreal top = qMax<qreal>(8.0, (height() - arcD - 30) / 2.0);
    QRectF arcRect((width() - arcD) / 2.0, top, arcD, arcD);

    const int startAngle = 225 * 16;
    const int span = -270 * 16;
    painter.setPen(QPen(QColor("#23252c"), 8, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arcRect, startAngle, span);

    if (m_hasValue && m_maxValue > 0) {
        const double frac = qBound(0.0, double(m_value) / m_maxValue, 1.0);
        QColor arc(colors::kAccent);
        if (m_absoluteColors) {
            if (m_value >= 90)
                arc = QColor(colors::kDanger);
            else if (m_value >= 80)
                arc = QColor(colors::kWarn);
        } else {
            if (frac >= 0.9)
                arc = QColor(colors::kDanger);
            else if (frac >= 0.8)
                arc = QColor(colors::kWarn);
        }
        painter.setPen(QPen(arc, 8, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arcRect, startAngle, int(-270 * 16 * frac));
    }

    QFont big = font();
    big.setBold(true);
    const QString valueText = m_hasValue ? QString::number(m_value) : QStringLiteral("--");
    // Shrink long readings (e.g. 4-digit RPM) until they fit inside the ring.
    int px = int(ring * 0.28);
    const int maxTextW = qMax(20, int(ring - 44)); // inner hole minus breathing room
    for (;;) {
        big.setPixelSize(px);
        if (px <= 10 || QFontMetrics(big).horizontalAdvance(valueText) <= maxTextW)
            break;
        --px;
    }
    painter.setFont(big);
    painter.setPen(QColor("#ffffff"));
    painter.drawText(arcRect.adjusted(0, -4, 0, -4), Qt::AlignCenter, valueText);

    QFont small = font();
    small.setPixelSize(11);
    small.setBold(false);
    painter.setFont(small);
    painter.setPen(QColor(colors::kTextFaint));
    painter.drawText(QRectF(0, arcRect.center().y() + ring * 0.16, width(), 18), Qt::AlignHCenter,
                     m_unit);
    painter.drawText(QRectF(0, arcRect.bottom() + 8, width(), 18), Qt::AlignHCenter, m_caption);
    painter.end();
}

} // namespace dtb::ui
