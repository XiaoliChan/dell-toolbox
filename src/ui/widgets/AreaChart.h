#pragma once

#include <QWidget>

#include <QQueue>

namespace dtb::ui {

// Magician temperature chart: accent line with a vertical gradient fill and a
// subtle grid, newest sample at the right edge.
class AreaChart : public QWidget {
    Q_OBJECT
public:
    explicit AreaChart(QWidget* parent = nullptr);

    void push(int value);
    void setMaxValue(int max) { m_max = qMax(1, max); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QQueue<int> m_samples;
    int m_capacity = 90;
    int m_max = 110;
};

} // namespace dtb::ui
