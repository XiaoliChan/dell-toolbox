#include "ui/widgets/Card.h"

#include <QLabel>

namespace dtb::ui {

Card::Card(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("card"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 14, 18, 18);
    outer->setSpacing(12);

    m_header = new QHBoxLayout;
    m_header->setSpacing(10);
    auto* t = new QLabel(title, this);
    t->setObjectName(QStringLiteral("cardTitle"));
    m_header->addWidget(t);
    m_header->addStretch(1);
    outer->addLayout(m_header);

    m_body = new QVBoxLayout;
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(10);
    outer->addLayout(m_body);
}

} // namespace dtb::ui
