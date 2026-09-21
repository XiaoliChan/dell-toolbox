#pragma once

#include <QWidget>

namespace dtb::ui {

// AWCC-style arc gauge: rounded 270° track + value arc + big centered value.
class CircularGauge : public QWidget {
    Q_OBJECT
public:
    CircularGauge(const QString& caption, int maxValue, const QString& unit, QWidget* parent = nullptr);

    void setValue(int value);
    void setUnit(const QString& unit);
    // Absolute thresholds (80/90 on the value itself, e.g. temperatures);
    // default is fraction-of-max thresholds (0.8/0.9, e.g. fan RPM).
    void setAbsoluteColors(bool on) { m_absoluteColors = on; }
    QSize minimumSizeHint() const override { return {0, 150}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_caption;
    QString m_unit;
    int m_maxValue;
    int m_value = 0;
    bool m_hasValue = false;
    bool m_absoluteColors = false;
};

} // namespace dtb::ui
