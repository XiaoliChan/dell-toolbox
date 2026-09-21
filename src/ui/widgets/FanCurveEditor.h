#pragma once

#include <QWidget>

#include <QList>
#include <QPair>

namespace dtb {

class FanCurveEditor : public QWidget {
    Q_OBJECT
public:
    explicit FanCurveEditor(const QString& title, QWidget* parent = nullptr);

    void setPoints(const QList<QPair<int, int>>& pts);
    const QList<QPair<int, int>>& points() const { return m_points; }

signals:
    void pointsEdited(const QList<QPair<int, int>>& points);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    QRect plotRect() const;
    QPointF pointToWidget(const QPair<int, int>& p) const;
    QPair<int, int> widgetToPoint(const QPointF& pos) const;
    int hitTest(const QPointF& pos) const;
    void commit();

    QString m_title;
    QList<QPair<int, int>> m_points;
    int m_dragIndex = -1;
    static constexpr int kMinTempC = 30;
    static constexpr int kMaxTempC = 100;
    static constexpr int kPointRadius = 6;
};

} // namespace dtb
