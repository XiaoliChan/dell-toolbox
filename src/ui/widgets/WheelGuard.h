#pragma once

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QWidget>

namespace dtb::ui {

// Mouse wheels silently change spin boxes, sliders and combo boxes while the
// user is only trying to scroll a page. This filter swallows every wheel
// event aimed at such a control - focused or not - and re-offers it to the
// parent chain so the enclosing scroll area still scrolls. Values change
// only by drag/typing. Install once per page after construction:
// WheelGuard::apply(this);
class WheelGuard : public QObject {
public:
    explicit WheelGuard(QObject* parent) : QObject(parent) {}

    static void apply(QWidget* root) {
        auto* guard = new WheelGuard(root); // dies with the page
        const QList<QWidget*> children = root->findChildren<QWidget*>();
        for (QWidget* w : children) {
            if (qobject_cast<QAbstractSlider*>(w) || qobject_cast<QAbstractSpinBox*>(w)
                || qobject_cast<QComboBox*>(w))
                w->installEventFilter(guard);
        }
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Wheel) {
            // Wheels must never nudge a value. Returning true stops delivery,
            // but an ignored wheel is NOT propagated to parents on its own -
            // dropping it here would dead-stop the page under the control.
            // Walk the parent chain and offer the event to each widget; the
            // enclosing scroll area (or any other handler) accepts it there.
            // The combo popup is a separate window and is not filtered, so
            // scrolling an open popup still works.
            event->ignore();
            QWidget* w = qobject_cast<QWidget*>(watched);
            while (w && w->parentWidget()) {
                w = w->parentWidget();
                event->ignore(); // clear acceptance from the previous hop
                QCoreApplication::sendEvent(w, event);
                if (event->isAccepted())
                    break;
            }
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace dtb::ui
