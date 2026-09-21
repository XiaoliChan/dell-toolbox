#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace dtb::ui {

// Samsung Magician-style card: title lives INSIDE the panel's header row;
// content goes into bodyLayout(). Optional right-side header widgets.
class Card : public QFrame {
public:
    explicit Card(const QString& title, QWidget* parent = nullptr);

    QVBoxLayout* bodyLayout() const { return m_body; }
    QHBoxLayout* headerLayout() const { return m_header; }

private:
    QHBoxLayout* m_header = nullptr;
    QVBoxLayout* m_body = nullptr;
};

} // namespace dtb::ui
