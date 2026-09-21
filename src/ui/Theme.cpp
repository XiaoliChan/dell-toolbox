#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFont>

namespace dtb::ui {

// QSS uses literal hex values (multi-digit %N markers interact badly with
// sequential QString::arg calls); the colors:: constants mirror them for
// custom-painted widgets.
void applyTheme(QApplication& app) {
    app.setStyle(QStringLiteral("Fusion"));
    QFont uiFont(QStringLiteral("Segoe UI"));
    uiFont.setPointSize(9);
    app.setFont(uiFont);

    const QString qss = QStringLiteral(R"(
* { outline: none; }
QWidget { background: transparent; color: #e9ebee; font-size: 13px; }
QMainWindow { background: #0d0e11; }
QWidget#sidebar { background: #101116; border-right: 1px solid #1f2127; }
QLabel { background: transparent; }
QLabel#pageTitle { font-size: 21px; font-weight: 800; color: #ffffff; }
QLabel#appTitle { font-size: 13px; font-weight: 800; color: #ffffff; }
QLabel#appVersion { font-size: 11px; color: #62666e; }
QLabel#brandText { color: #ffffff; font-size: 17px; font-weight: 800; letter-spacing: 0.3px; }
QLabel#cardValue { font-size: 24px; font-weight: 800; color: #ffffff; }
QLabel#cardCaption { font-size: 11px; font-weight: 600; color: #62666e; }

QListWidget { background: transparent; border: none; padding: 0px 10px; outline: 0; }
QListWidget::item {
    color: #b9bec7; padding: 9px 12px; border-radius: 8px; margin: 2px 0;
}
QListWidget::item:hover { background: #1a1c22; color: #e9ebee; }
QListWidget::item:selected { background: #1c2536; color: #6a9dff; }

QFrame#card { background: #15161b; border: 1px solid #1f2127; border-radius: 14px; }
QLabel#cardTitle { font-size: 14px; font-weight: 700; color: #e9ebee; background: transparent; }
QLabel#chip { border-radius: 10px; padding: 4px 12px; font-size: 11px; font-weight: 700; background: #12233c; color: #6a9dff; }
QLabel#chipGood { border-radius: 10px; padding: 4px 12px; font-size: 11px; font-weight: 700; background: #12331f; color: #4ade80; }
QLabel#chipInfo { border-radius: 10px; padding: 4px 12px; font-size: 11px; font-weight: 700; background: #12233c; color: #6a9dff; }
QLabel#chipWarn { border-radius: 10px; padding: 4px 12px; font-size: 11px; font-weight: 700; background: #3a3013; color: #ffd166; }
QLabel#chipBad { border-radius: 10px; padding: 4px 12px; font-size: 11px; font-weight: 700; background: #3b1a1a; color: #ff5d5d; }
QLabel#statValue { font-size: 26px; font-weight: 800; color: #ffffff; }
QLabel#statCaption { font-size: 11px; color: #62666e; font-weight: 600; }

QPushButton {
    background: #4f8cff; color: #ffffff; border: none; border-radius: 10px;
    padding: 9px 20px; font-weight: 700;
}
QPushButton:hover { background: #6a9dff; }
QPushButton:pressed { background: #3f7af0; }
QPushButton:disabled { background: #23252c; color: #62666e; }
QPushButton#ghostButton { background: transparent; color: #e9ebee; border: 1px solid #1f2127; }
QPushButton#ghostButton:hover { border-color: #4f8cff; color: #ffffff; }

QRadioButton { spacing: 10px; padding: 10px 14px; border-radius: 10px; font-weight: 600; }
QRadioButton:hover { background: #1a1c22; }
QRadioButton::indicator {
    width: 18px; height: 18px; border-radius: 9px;
    border: 1.6px solid #4a4e57; background: transparent; subcontrol-position: center left;
}
QRadioButton::indicator:hover { border-color: #4f8cff; }
/* Fluent-style radio: thin ring with a round dot inside when checked. */
QRadioButton::indicator:checked {
    border: 1.6px solid #4f8cff;
    background:
        qradialgradient(cx: 0.5, cy: 0.5, radius: 0.5, fx: 0.5, fy: 0.5,
                        stop: 0 #4f8cff, stop: 0.52 #4f8cff,
                        stop: 0.56 rgba(79, 140, 255, 0), stop: 1 rgba(79, 140, 255, 0));
}

QSlider { min-height: 26px; }
QSlider::groove:horizontal { height: 4px; background: #26282f; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #4f8cff; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 18px; height: 18px; margin: -7px 0; border-radius: 9px; background: #ffffff;
}
QSlider::handle:hover { background: #e9ebee; }
QSlider::groove:disabled { background: #1c1e24; }
QSlider::sub-page:disabled { background: #3a3d45; }
QSlider::handle:disabled { background: #3a3d45; }

QComboBox {
    background: #1a1c22; border: 1px solid #26282f; border-radius: 10px;
    padding: 8px 12px; color: #e9ebee; min-width: 90px;
}
QComboBox:hover { border-color: #3a3d45; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow {
    image: url(:/res/combo-arrow.png); width: 10px; height: 10px; margin-right: 8px;
}
QComboBox QAbstractItemView {
    background: #17181d; border: 1px solid #2a2d35; border-radius: 8px;
    color: #e9ebee; selection-background-color: #4f8cff; selection-color: #ffffff;
    outline: 0; padding: 2px;
}
QComboBox QAbstractItemView::item { min-height: 26px; padding: 4px 10px; border-radius: 6px; }
QComboBox QAbstractItemView::item:hover { background: #23252c; }

QTextEdit, QLineEdit, QSpinBox {
    background: #1a1c22; color: #e9ebee; border: 1px solid #26282f;
    border-radius: 10px; padding: 7px 10px; selection-background-color: #4f8cff;
}
QTextEdit:focus, QLineEdit:focus, QSpinBox:focus { border-color: #4f8cff; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; background: transparent; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2a2d35; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #3a3d45; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2a2d35; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QToolTip {
    background: #1b1d23; color: #e9ebee; border: 1px solid #2a2d35;
    padding: 6px 8px; border-radius: 6px; font-size: 12px;
}
)");
    app.setStyleSheet(qss);
}

namespace {
// Marks a combo's popup window translucent whenever its view (re)appears.
// QComboBox creates the popup view lazily on first showPopup, so both the
// eager pass at startup and this filter are needed.
class ComboPopupSoftener : public QObject {
public:
    using QObject::QObject;
    static void mark(QComboBox* combo) {
        combo->view()->window()->setAttribute(Qt::WA_TranslucentBackground);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ChildAdded)
            if (auto* combo = qobject_cast<QComboBox*>(watched); combo && combo->view())
                mark(combo);
        return QObject::eventFilter(watched, event);
    }
};
} // namespace

void softenComboPopups(QWidget* root) {
    const QList<QComboBox*> combos = root->findChildren<QComboBox*>();
    for (QComboBox* combo : combos) {
        ComboPopupSoftener::mark(combo); // forces view + container creation
        combo->installEventFilter(new ComboPopupSoftener(combo)); // parented: dies with combo
    }
}

} // namespace dtb::ui
