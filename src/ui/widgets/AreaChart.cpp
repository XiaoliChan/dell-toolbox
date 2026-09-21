#include "ui/widgets/AreaChart.h"

#include <QPainter>
#include <QPainterPath>

#include "ui/Theme.h"

namespace dtb::ui {

AreaChart::AreaChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(0, 110); // width flexible
}

void AreaChart::push(int value) {
    if (m_samples.size() >= m_capacity)
        m_samples.dequeue();
    m_samples.enqueue(value);
    update();
}

void AreaChart::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect plot = rect().adjusted(0, 8, -2, -8);
    painter.setPen(QPen(QColor("#1f2127"), 1));
    for (int i = 1; i < 4; ++i) {
        const int y = plot.top() + plot.height() * i / 4;
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    if (m_samples.size() >= 2) {
        QPolygonF line;
        const double stepX = double(plot.width()) / double(m_capacity - 1);
        const double x0 = plot.right() - stepX * (m_samples.size() - 1);
        int i = 0;
        for (int v : m_samples) {
            const double y = plot.bottom() - double(qBound(0, v, m_max)) / m_max * plot.height();
            line.append(QPointF(x0 + stepX * i, y));
            ++i;
        }
        QPainterPath fill(line.first());
        for (int i = 1; i < line.size(); ++i)
            fill.lineTo(line[i]);
        fill.lineTo(QPointF(line.last().x(), plot.bottom()));
        fill.lineTo(QPointF(line.first().x(), plot.bottom()));
        fill.closeSubpath();
        QLinearGradient grad(0, plot.top(), 0, plot.bottom());
        QColor c(colors::kAccent);
        c.setAlpha(70);
        grad.setColorAt(0, c);
        c.setAlpha(0);
        grad.setColorAt(1, c);
        painter.setPen(Qt::NoPen);
        painter.fillPath(fill, grad);

        painter.setPen(QPen(QColor(colors::kAccent), 2));
        painter.drawPolyline(line);

        painter.setBrush(QColor("#ffffff"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(line.last(), 3, 3);
    }
    painter.end();
}

} // namespace dtb::ui
