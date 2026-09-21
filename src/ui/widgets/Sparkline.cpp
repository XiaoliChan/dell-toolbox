#include "ui/widgets/Sparkline.h"

#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "ui/Theme.h"

namespace dtb::ui {

Sparkline::Sparkline(const QString& caption, int maxValue, QWidget* parent)
    : QWidget(parent), m_caption(caption), m_unit(QStringLiteral("")), m_maxValue(maxValue) {
    setMinimumHeight(110);
}

void Sparkline::push(int value) {
    if (m_samples.size() >= m_capacity)
        m_samples.dequeue();
    m_samples.enqueue(value);
    m_last = value;
    update();
}

void Sparkline::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect card = rect().adjusted(0, 0, -1, -1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(colors::kCard));
    painter.drawRoundedRect(card, 8, 8);

    QFont small = painter.font();
    small.setPixelSize(11);
    painter.setFont(small);
    painter.setPen(QColor(colors::kTextDim));
    painter.drawText(card.adjusted(14, 10, -14, -card.height() + 26), Qt::AlignLeft, m_caption);

    QFont big = painter.font();
    big.setBold(true);
    big.setPixelSize(20);
    painter.setFont(big);
    painter.setPen(QColor("#ffffff"));
    const QString reading = m_last == INT_MIN ? QStringLiteral("--")
                                              : QString::number(m_last) + m_unit;
    painter.drawText(card.adjusted(14, 26, -14, -card.height() + 54), Qt::AlignLeft, reading);

    const QRect plot = card.adjusted(14, 58, -14, -14);
    painter.setPen(QPen(QColor("#393b40"), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plot);

    if (m_samples.size() >= 2) {
        QPolygonF points;
        const double stepX = double(plot.width()) / double(m_capacity - 1);
        const double x0 = plot.right() - stepX * (m_samples.size() - 1);
        int i = 0;
        for (int v : m_samples) {
            const double y = plot.bottom() - double(qBound(0, v, m_maxValue)) / m_maxValue * plot.height();
            points.append(QPointF(x0 + stepX * i, y));
            ++i;
        }
        painter.setPen(QPen(QColor(colors::kAccent), 2));
        painter.drawPolyline(points);
    }
    painter.end();
}

} // namespace dtb::ui
