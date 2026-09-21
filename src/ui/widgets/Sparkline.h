#pragma once

#include <QWidget>

#include <QQueue>

namespace dtb::ui {

// Scrolling line chart with a fixed-size sample ring; the newest sample is
// always at the right edge.
class Sparkline : public QWidget {
    Q_OBJECT
public:
    explicit Sparkline(const QString& caption, int maxValue, QWidget* parent = nullptr);

    void push(int value);
    void setUnit(const QString& unit) { m_unit = unit; }
    QSize minimumSizeHint() const override { return {240, 96}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_caption;
    QString m_unit;
    int m_maxValue;
    QQueue<int> m_samples;
    int m_capacity = 60;
    int m_last = INT_MIN;
};

} // namespace dtb::ui
