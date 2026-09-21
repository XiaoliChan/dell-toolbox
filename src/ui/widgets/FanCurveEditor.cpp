#include "ui/widgets/FanCurveEditor.h"

#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "ui/Theme.h"

namespace dtb {

FanCurveEditor::FanCurveEditor(const QString& title, QWidget* parent)
    : QWidget(parent), m_title(title) {
    setMinimumHeight(260);
    // Fixed vertical: layouts (and scroll-area clamping) must not
    // squeeze the editor into a title-only sliver.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
}

QRect FanCurveEditor::plotRect() const {
    return rect().adjusted(46, 34, -14, -34);
}

QPointF FanCurveEditor::pointToWidget(const QPair<int, int>& p) const {
    const QRect r = plotRect();
    const double x = r.left() + double(p.first - kMinTempC) / (kMaxTempC - kMinTempC) * r.width();
    const double y = r.bottom() - double(p.second) / 100.0 * r.height();
    return QPointF(x, y);
}

QPair<int, int> FanCurveEditor::widgetToPoint(const QPointF& pos) const {
    const QRect r = plotRect();
    int t = kMinTempC
            + int((pos.x() - r.left()) / r.width() * (kMaxTempC - kMinTempC) + 0.5);
    int p = int((r.bottom() - pos.y()) / r.height() * 100.0 + 0.5);
    return {qBound(kMinTempC, t, kMaxTempC), qBound(0, p, 100)};
}

int FanCurveEditor::hitTest(const QPointF& pos) const {
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF c = pointToWidget(m_points[i]);
        if (QLineF(c, pos).length() <= kPointRadius + 3)
            return i;
    }
    return -1;
}

void FanCurveEditor::setPoints(const QList<QPair<int, int>>& pts) {
    m_points = pts;
    update();
}

void FanCurveEditor::commit() {
    std::sort(m_points.begin(), m_points.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    update();
    emit pointsEdited(m_points);
}

void FanCurveEditor::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(ui::colors::kCard));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    QFont small = painter.font();
    small.setPixelSize(11);
    painter.setFont(small);
    painter.setPen(QColor(ui::colors::kTextDim));
    painter.drawText(QRect(0, 6, width(), 20), Qt::AlignHCenter, m_title);

    const QRect r = plotRect();
    painter.setPen(QPen(QColor("#393b40"), 1));
    for (int t = kMinTempC; t <= kMaxTempC; t += 10) {
        const int x = int(pointToWidget({t, 0}).x());
        painter.drawLine(x, r.top(), x, r.bottom());
        painter.setPen(QColor(ui::colors::kTextDim));
        painter.drawText(QRect(x - 16, r.bottom() + 6, 32, 14), Qt::AlignCenter, QString::number(t));
        painter.setPen(QPen(QColor("#393b40"), 1));
    }
    for (int p = 0; p <= 100; p += 25) {
        const int y = int(pointToWidget({0, p}).y());
        painter.drawLine(r.left(), y, r.right(), y);
        painter.setPen(QColor(ui::colors::kTextDim));
        painter.drawText(QRect(6, y - 7, 34, 14), Qt::AlignVCenter | Qt::AlignRight, QString::number(p) + QStringLiteral("%"));
        painter.setPen(QPen(QColor("#393b40"), 1));
    }

    if (m_points.size() >= 2) {
        QPolygonF poly;
        for (const auto& pt : m_points)
            poly.append(pointToWidget(pt));
        painter.setPen(QPen(QColor(ui::colors::kAccent), 2));
        painter.drawPolyline(poly);
    }
    for (int i = 0; i < m_points.size(); ++i) {
        painter.setBrush(i == m_dragIndex ? QColor("#ffffff") : QColor(ui::colors::kAccent));
        painter.setPen(QPen(QColor("#1b1c1f"), 2));
        painter.drawEllipse(pointToWidget(m_points[i]), kPointRadius, kPointRadius);
    }
    painter.end();
}

void FanCurveEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragIndex = hitTest(event->position());
        if (m_dragIndex >= 0) {
            setCursor(Qt::ClosedHandCursor);
            update();
        }
    }
    QWidget::mousePressEvent(event);
}

void FanCurveEditor::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragIndex >= 0 && (event->buttons() & Qt::LeftButton)) {
        QPair<int, int> p = widgetToPoint(event->position());
        // Keep x strictly ordered between the neighbours.
        if (m_dragIndex > 0)
            p.first = qMax(p.first, m_points[m_dragIndex - 1].first + 1);
        if (m_dragIndex < m_points.size() - 1)
            p.first = qMin(p.first, m_points[m_dragIndex + 1].first - 1);
        if (p.first < kMinTempC || p.first > kMaxTempC)
            return;
        m_points[m_dragIndex] = p;
        update();
    } else {
        setCursor(hitTest(event->position()) >= 0 ? Qt::OpenHandCursor : Qt::CrossCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void FanCurveEditor::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragIndex >= 0) {
        m_dragIndex = -1;
        setCursor(Qt::CrossCursor);
        commit();
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void FanCurveEditor::contextMenuEvent(QContextMenuEvent* event) {
    const int hit = hitTest(event->pos());
    auto* menu = new QMenu(this);
    if (hit >= 0) {
        if (m_points.size() > 2) {
            connect(menu->addAction(tr("Remove point")), &QAction::triggered, this, [this, hit] {
                m_points.removeAt(hit);
                commit();
            });
        }
    } else {
        connect(menu->addAction(tr("Add point here")), &QAction::triggered, this, [this, event] {
            m_points.append(widgetToPoint(event->pos()));
            commit();
        });
    }
    menu->exec(event->globalPos());
    menu->deleteLater();
}

} // namespace dtb
