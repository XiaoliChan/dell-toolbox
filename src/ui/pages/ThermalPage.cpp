#include "ui/pages/ThermalPage.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <cmath> // std::lround (debounce unit conversion)

#include "core/Logger.h"
#include "ui/Theme.h"
#include "ui/widgets/Card.h"
#include "ui/widgets/FanCurveEditor.h"
#include "ui/widgets/WheelGuard.h"

#include "core/FanCurve.h"

#ifdef Q_OS_WIN
#include "hal/win/AwccConflict.h"
#endif

namespace dtb::ui {
namespace {
// Numeric field that only becomes editable on CLICK - hover and wheel never
// change it (wheel is inert by design; the page scrolls instead).
class ClickSpinBox : public QSpinBox {
public:
    explicit ClickSpinBox(QWidget* parent) : QSpinBox(parent) {
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setReadOnly(true);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        setReadOnly(false);
        setFocus(Qt::MouseFocusReason);
        selectAll();
        QSpinBox::mousePressEvent(e);
    }
    void focusOutEvent(QFocusEvent* e) override {
        setReadOnly(true);
        QSpinBox::focusOutEvent(e);
    }
    void wheelEvent(QWheelEvent* e) override { e->ignore(); }
};
// Read-only reference curves per firmware mode. The BIOS tunes fan behaviour
// per profile internally and the official curves are not exposed over any WMI
// surface (docs/awcc-analysis/00) - these presets mirror each mode's intent
// and are shown read-only. Custom keeps the user's own editable curves.
QList<QPair<int, int>> referenceCurve(ThermalMode mode) {
    switch (mode) {
    case ThermalMode::Quiet:
        return {{40, 25}, {50, 28}, {60, 35}, {70, 48}, {80, 68}, {90, 100}};
    case ThermalMode::Cool:
        return {{40, 30}, {50, 36}, {60, 46}, {70, 60}, {80, 80}, {90, 100}};
    case ThermalMode::Balanced:
        return {{40, 28}, {50, 33}, {60, 42}, {70, 56}, {80, 76}, {90, 100}};
    case ThermalMode::Performance:
        return {{40, 40}, {50, 52}, {60, 64}, {70, 78}, {80, 92}, {90, 100}};
    case ThermalMode::GMode:
        return {{40, 100}, {55, 100}, {70, 100}, {90, 100}};
    case ThermalMode::Custom:
        break;
    }
    return {};
}
} // namespace

ThermalPage::ThermalPage(HalSet hal, Controller* controller, ConfigStore* config, QWidget* parent)
    : QWidget(parent), m_hal(hal), m_controller(controller), m_config(config) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* title = new QLabel(tr("Thermal"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);

    layout->addWidget(buildModeCard());
    m_curvesCard = buildCurvesCard();
    layout->addWidget(m_curvesCard, 1);
    layout->addWidget(buildSceneCard());

    connect(controller, &Controller::snapshotUpdated, this, &ThermalPage::onSnapshot);
    // Settings "Reset to defaults": re-read every control from the reloaded
    // config (boost sliders back to Auto, curves/mode combo re-synced).
    connect(controller, &Controller::configReloaded, this, &ThermalPage::onConfigReloaded);
    connect(controller, &Controller::thermalModeChanged, this, &ThermalPage::onModeAdopted);
    // Page-wide: also covers the curves card (mode/style combos, boost
    // sliders) - a wheel there would silently change the fan minimum.
    WheelGuard::apply(this);
}

void ThermalPage::onModeAdopted(ThermalMode mode, bool external) {
    Q_UNUSED(external);
    // External change (AWCC/DPM): re-sync the radios without re-triggering
    // writes.
    for (int i = 0; i < m_modeRadios.size(); ++i) {
        m_modeRadios[i]->blockSignals(true);
        m_modeRadios[i]->setChecked(m_modeValues[i] == mode);
        m_modeRadios[i]->blockSignals(false);
    }
    const int idx = m_curveMode ? m_curveMode->findData(int(mode)) : -1;
    if (m_curveMode && idx >= 0 && idx != m_curveMode->currentIndex())
        m_curveMode->setCurrentIndex(idx);
}

void ThermalPage::addProfile(QButtonGroup* group, QVBoxLayout* layout, ThermalMode mode,
                             const QString& title, const QString& description) {
    auto* radio = new QRadioButton(title, this);
    radio->setChecked(m_config->loadMode() == mode);
    group->addButton(radio);
    layout->addWidget(radio);
    auto* desc = new QLabel(description, this);
    desc->setObjectName(QStringLiteral("cardCaption"));
    wrapLabel(desc);
    desc->setIndent(24);
    desc->setMaximumWidth(620);
    layout->addWidget(desc);
    const ThermalMode m = mode;
    m_modeRadios.append(radio);
    m_modeValues.append(m);
    connect(radio, &QRadioButton::toggled, this, [this, m](bool on) {
        if (on)
            onModeSelectedWithMode(m);
    });
}

QWidget* ThermalPage::buildModeCard() {
    auto* box = new Card(QStringLiteral("Thermal setting"), this);
    auto* layout = box->bodyLayout();
    layout->setSpacing(4);
    auto* modeSubtitle = new QLabel(
        tr("Profiles are executed by the BIOS: one selection adjusts both the fan curves and the "
           "power behaviour, exactly like the same four profiles inside Alienware Command Center."),
        this);
    modeSubtitle->setObjectName(QStringLiteral("cardCaption"));
    wrapLabel(modeSubtitle);
    layout->addWidget(modeSubtitle);

    m_banner = new QLabel(this);
    m_banner->setStyleSheet(QStringLiteral(
        "background-color:#5c4a1e; color:#ffd54f; border-radius:6px; padding:8px 12px;"));
    m_banner->setWordWrap(true);
    m_banner->hide();
    layout->addWidget(m_banner);

    m_modeGroup = new QButtonGroup(box);
    addProfile(m_modeGroup, layout, ThermalMode::Quiet, tr("Quiet"),
               tr("Processor and cooling fan speed are adjusted to reduce fan noise. May mean a higher "
                  "system surface temperature and reduced performance."));
    addProfile(m_modeGroup, layout, ThermalMode::Cool, tr("Cool"),
               tr("Processor and cooling fan speed are adjusted to help maintain a cooler system "
                  "surface temperature. May mean reduced performance and more noise."));
    addProfile(m_modeGroup, layout, ThermalMode::Balanced, tr("Optimized (Balanced)"),
               tr("The standard setting for cooling fan and processor heat management: a balance of "
                  "performance, noise and temperature."));
    addProfile(m_modeGroup, layout, ThermalMode::Performance, tr("Ultra Performance"),
               tr("Processor and cooling fan speed is increased for more performance. May mean higher "
                  "system surface temperature and more noise."));
    addProfile(m_modeGroup, layout, ThermalMode::GMode, tr("G-Mode"),
               tr("Maximum fan speed and unlocked power state - the same toggle as AWCC's G logo."));
    addProfile(m_modeGroup, layout, ThermalMode::Custom, tr("Custom (fan curves)"),
               tr("Your own per-fan temperature curves take over the fans."));
    return box;
}

QWidget* ThermalPage::buildCurvesCard() {
    auto* box = new Card(QStringLiteral("Fan curves"), this);
    auto* body = new QVBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    m_curveMode = new QComboBox(box);
    m_curveMode->addItem(tr("Quiet"), int(ThermalMode::Quiet));
    m_curveMode->addItem(tr("Cool"), int(ThermalMode::Cool));
    m_curveMode->addItem(tr("Optimized"), int(ThermalMode::Balanced));
    m_curveMode->addItem(tr("Ultra Performance"), int(ThermalMode::Performance));
    m_curveMode->addItem(tr("G-Mode"), int(ThermalMode::GMode));
    m_curveMode->addItem(tr("Custom"), int(ThermalMode::Custom));
    top->addWidget(m_curveMode);
    top->addStretch(1);
    body->addLayout(top);

    auto* note = new QLabel(
        tr("Reference curves per mode - the BIOS applies its own factory tuning per profile and does "
           "not expose the official curves over WMI. Custom unlocks fan control: Easy sets a minimum "
           "speed per fan, Advanced edits temperature curves."), box);
    note->setObjectName(QStringLiteral("cardCaption"));
    wrapLabel(note);
    body->addWidget(note);

    m_fanStyle = new QComboBox(box);
    m_fanStyle->addItem(tr("Easy - fan boost"));
    m_fanStyle->addItem(tr("Advanced - fan curves"));
    m_fanStyle->setVisible(false); // Custom mode only
    body->addWidget(m_fanStyle);

    // Easy: one minimum-speed (boost) slider per fan. 0 follows the curve.
    m_easyBody = new QWidget(box);
    auto* easy = new QVBoxLayout(m_easyBody);
    easy->setContentsMargins(0, 0, 0, 0);
    easy->setSpacing(6);
    if (m_hal.thermal) {
        for (const HalFanInfo& fi : m_hal.thermal->enumerateFans()) {
            auto* rowW = new QWidget(m_easyBody);
            auto* rl = new QHBoxLayout(rowW);
            rl->setContentsMargins(0, 0, 0, 0);
            auto* cap = new QLabel(fi.sensorId == 0x01 ? tr("CPU fan minimum") : tr("GPU fan minimum"), rowW);
            cap->setMinimumWidth(110);
            auto* slider = new QSlider(Qt::Horizontal, rowW);
            slider->setRange(0, 100);
            slider->setValue(m_config->loadFanBoost(fi.id));
            auto* value = new QLabel(slider->value() > 0 ? tr("%1%").arg(slider->value())
                                                         : tr("Auto"), rowW);
            value->setMinimumWidth(44);
            rl->addWidget(cap);
            rl->addWidget(slider, 1);
            rl->addWidget(value);
            easy->addWidget(rowW);
            const FanId id = fi.id;
            m_boostSaved[id] = slider->value();
            // Route through the controller: apply() clamps curve targets up
            // to the boost floor with the shared write throttling, so the
            // curve and the boost never fight each other.
            //
            // Persistence is debounced (400 ms after the last change) plus an
            // immediate flush on drag release. No isSliderDown() gating: a
            // drag whose mouse-release is lost (modal, minimise, lock) latches
            // that flag forever and silently disables all later saves.
            connect(slider, &QSlider::valueChanged, this, [this, value, id](int v) {
                value->setText(v > 0 ? tr("%1%").arg(v) : tr("Auto"));
                m_controller->setFanBoost(id, v); // hardware follows the drag (1 Hz throttled)
                m_boostDebounce->start();
            });
            connect(slider, &QSlider::sliderReleased, this, [this] { saveBoosts(); });
            m_boostSliders.append({fi.id, slider});
            m_boostLabels.append({fi.id, value});
        }
    }
    if (easy->count() == 0)
        easy->addWidget(new QLabel(tr("No fans detected"), m_easyBody));
    body->addWidget(m_easyBody);

    // Advanced: the per-fan curve editors + reset.
    m_advanceBody = new QWidget(box);
    auto* advance = new QVBoxLayout(m_advanceBody);
    advance->setContentsMargins(0, 0, 0, 0);
    advance->setSpacing(8);
    auto* row = new QWidget(m_advanceBody);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    if (m_hal.thermal) {
        for (const HalFanInfo& fi : m_hal.thermal->enumerateFans()) {
            // Short captions: the long hex titles overflowed the narrow editors.
            const QString name = fi.sensorId == 0x01 ? tr("CPU fan") : tr("GPU fan");
            auto* editor = new FanCurveEditor(name, box);
            editor->setPoints(m_config->loadCurve(fi.id));
            const FanId id = fi.id;
            connect(editor, &FanCurveEditor::pointsEdited, this,
                    [this, id](const QList<QPair<int, int>>& pts) { onCurveEdited(id, pts); });
            m_curveEditors.append({fi.id, editor});
            layout->addWidget(editor, 1);
        }
    }
    if (layout->count() == 0)
        layout->addWidget(new QLabel(tr("No fans detected"), box));
    advance->addWidget(row);
    auto* resetRow = new QHBoxLayout;
    resetRow->setContentsMargins(0, 0, 0, 0);
    m_curveReset = new QPushButton(tr("Reset to default"), m_advanceBody);
    resetRow->addWidget(m_curveReset);
    resetRow->addStretch(1);
    advance->addLayout(resetRow);
    body->addWidget(m_advanceBody);
    // Anchor the composed body into the card. Without this the layout is an
    // orphan: its widgets pile up unmanaged at the card's top-left (the
    // 'CPU fan GPU fan' sliver bug) and the card renders as an empty shell.
    box->bodyLayout()->addLayout(body);

    connect(m_curveMode, &QComboBox::currentIndexChanged, this, &ThermalPage::onCurveModeChanged);
    connect(m_fanStyle, &QComboBox::currentIndexChanged, this, &ThermalPage::onFanStyleChanged);
    connect(m_curveReset, &QPushButton::clicked, this, &ThermalPage::onCurveReset);
    m_boostDebounce = new QTimer(this);
    m_boostDebounce->setSingleShot(true);
    m_boostDebounce->setInterval(400);
    connect(m_boostDebounce, &QTimer::timeout, this, [this] { saveBoosts(); });
    // Start on the saved mode so the card matches the radio selection even
    // before the controller's first thermalModeChanged arrives (which never
    // comes in passive/monitor mode).
    const int initIdx = m_curveMode->findData(int(m_config->loadMode()));
    if (initIdx >= 0)
        m_curveMode->setCurrentIndex(initIdx);
    onCurveModeChanged(m_curveMode->currentIndex());
    return box;
}

// Persist every changed boost (config + controller) exactly once per change.
// Skips no-change clicks: a forced rewrite at an unchanged value would push a
// redundant WMI write past the dead zone.
void ThermalPage::saveBoosts() {
    for (const auto& entry : m_boostSliders) {
        const int v = entry.second->value();
        if (v == m_boostSaved.value(entry.first, -1))
            continue;
        m_boostSaved[entry.first] = v;
        m_config->saveFanBoost(entry.first, v);
        m_controller->setFanBoost(entry.first, v);
    }
}

void ThermalPage::onCurveReset() {
    const FanCurve defaults = FanCurve::defaultCurve();
    for (const auto& entry : m_curveEditors) {
        entry.second->setPoints(defaults.points());
        onCurveEdited(entry.first, defaults.points());
    }
    for (const auto& entry : m_boostSliders) {
        entry.second->setValue(0); // releases the fan back to the mode curve
    }
    saveBoosts();
}

QWidget* ThermalPage::buildSceneCard() {
    auto* box = new Card(QStringLiteral("Game auto-switch"), this);
    auto* sceneSubtitle = new QLabel(
        tr("Switches to G-Mode when a game is detected (GPU load above the enter threshold, or a "
           "listed foreground process) and back to your selected profile afterwards. AWCC does not "
           "expose its internal switching policy, so every threshold here is adjustable."), this);
    sceneSubtitle->setObjectName(QStringLiteral("cardCaption"));
    wrapLabel(sceneSubtitle);
    box->bodyLayout()->addWidget(sceneSubtitle);

    const SceneParams p = m_config->loadSceneParams();
    m_sceneEnabled = new QCheckBox(tr("Enabled"), box);
    m_sceneEnabled->setChecked(p.enabled);
    box->bodyLayout()->addWidget(m_sceneEnabled);

    m_sceneBody = new QWidget(box);
    auto* form = new QFormLayout(m_sceneBody);
    form->setContentsMargins(0, 0, 0, 0);
    m_gpuEnter = new ClickSpinBox(m_sceneBody);
    m_gpuEnter->setRange(20, 100);
    m_gpuEnter->setValue(p.gpuEnterThreshold);
    m_gpuEnter->setMaximumWidth(150);
    m_gpuEnter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_gpuExit = new ClickSpinBox(m_sceneBody);
    m_gpuExit->setRange(10, 95);
    m_gpuExit->setValue(p.gpuExitThreshold);
    m_gpuExit->setMaximumWidth(150);
    m_gpuExit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_processes = new QTextEdit(m_sceneBody);
    m_processes->setPlaceholderText(tr("game.exe\nlauncher.exe\n(one per line)"));
    m_processes->setPlainText(p.gameProcesses.join(QLatin1Char('\n')));
    m_processes->setFixedHeight(72);

    // Debounce durations: click-to-edit value + a unit dropdown (s / min).
    auto makeDebounceRow = [&](int seconds, QSpinBox** spinOut, QComboBox** unitOut) {
        auto* rowW = new QWidget(m_sceneBody);
        auto* hl = new QHBoxLayout(rowW);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(6);
        auto* spin = new ClickSpinBox(rowW);
        spin->setRange(1, 600);
        spin->setValue(qBound(1, seconds, 600));
        spin->setMaximumWidth(110);
        spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* unit = new QComboBox(rowW);
        unit->addItems({QStringLiteral("s"), QStringLiteral("min")});
        unit->setMaximumWidth(90);
        unit->setProperty("wasMin", false);
        hl->addWidget(spin, 1);
        hl->addWidget(unit);
        connect(unit, &QComboBox::currentIndexChanged, rowW, [spin, unit](int index) {
            const bool wasMin = unit->property("wasMin").toBool();
            const bool toMin = index == 1;
            int sec = spin->value() * (wasMin ? 60 : 1);
            spin->setRange(1, toMin ? 10 : 600);
            spin->setValue(toMin ? qBound(1, int(std::lround(sec / 60.0)), 10) : qBound(1, sec, 600));
            unit->setProperty("wasMin", toMin);
        });
        hl->addStretch(1);
        *spinOut = spin;
        *unitOut = unit;
        return rowW;
    };

    auto* enterDeb = makeDebounceRow(p.enterDebounceS, &m_enterDebounce, &m_enterUnit);
    auto* exitDeb = makeDebounceRow(p.exitDebounceS, &m_exitDebounce, &m_exitUnit);

    form->addRow(tr("GPU enter threshold (%)"), m_gpuEnter);
    form->addRow(tr("GPU exit threshold (%)"), m_gpuExit);
    form->addRow(tr("Enter confirmation"), enterDeb);
    form->addRow(tr("Exit confirmation"), exitDeb);
    form->addRow(tr("Foreground processes"), m_processes);
    m_sceneReset = new QPushButton(tr("Reset to defaults"), m_sceneBody);
    form->addRow(QString(), m_sceneReset);
    box->bodyLayout()->addWidget(m_sceneBody);
    m_sceneBody->setVisible(p.enabled);

    connect(m_sceneEnabled, &QCheckBox::toggled, this, [this](bool on) {
        SceneParams sp = m_config->loadSceneParams();
        sp.enabled = on;
        m_config->saveSceneParams(sp);
        m_controller->scene()->setParams(sp);
        m_sceneBody->setVisible(on);
    });
    connect(m_sceneReset, &QPushButton::clicked, this, &ThermalPage::onSceneReset);
    for (QSpinBox* spin : {m_gpuEnter, m_gpuExit, m_enterDebounce, m_exitDebounce})
        connect(spin, &QSpinBox::valueChanged, this, &ThermalPage::onSceneParamsChanged);
    connect(m_processes, &QTextEdit::textChanged, this, &ThermalPage::onSceneParamsChanged);
    return box;
}

void ThermalPage::onModeSelectedWithMode(ThermalMode mode) {
    dtbLog(info) << "ui: user selected thermal mode" << int(mode);
    m_config->saveMode(mode);
    m_controller->baseline()->setMode(mode);
    m_controller->forceNextModeWrite();
    const int idx = m_curveMode ? m_curveMode->findData(int(mode)) : -1;
    if (m_curveMode && idx >= 0 && idx != m_curveMode->currentIndex())
        m_curveMode->setCurrentIndex(idx);
}

void ThermalPage::onCurveModeChanged(int index) {
    if (index < 0 || !m_curveMode)
        return;
    const auto mode = static_cast<ThermalMode>(m_curveMode->itemData(index).toInt());
    const bool custom = mode == ThermalMode::Custom;
    m_fanStyle->setVisible(custom);
    m_easyBody->setVisible(custom && m_fanStyle->currentIndex() == 0);
    m_advanceBody->setVisible(custom ? m_fanStyle->currentIndex() == 1 : true);
    // Reset writes the saved custom curves and boosts: only offer it while a
    // custom curve is actually editable, never over a read-only reference.
    if (m_curveReset)
        m_curveReset->setVisible(custom);
    for (const auto& entry : m_curveEditors) {
        entry.second->setPoints(custom ? m_config->loadCurve(entry.first) : referenceCurve(mode));
        entry.second->setEnabled(custom); // presets are read-only, custom is adjustable
    }
}

void ThermalPage::onFanStyleChanged(int index) {
    const bool easy = index == 0;
    m_easyBody->setVisible(easy);
    m_advanceBody->setVisible(!easy);
}

void ThermalPage::onCurveEdited(FanId fan, const QList<QPair<int, int>>& points) {
    m_config->saveCurve(fan, points);
    if (!m_hal.thermal)
        return;
    for (const HalFanInfo& fi : m_hal.thermal->enumerateFans())
        if (fi.id == fan) {
            FanCurve curve;
            curve.setPoints(points);
            m_controller->baseline()->setFanCurve(fan, fi.sensorId, curve);
        }
}

void ThermalPage::onSceneParamsChanged() {
    SceneParams p = m_config->loadSceneParams(); // keep enabled/others, update edited fields
    p.gpuEnterThreshold = m_gpuEnter->value();
    p.gpuExitThreshold = m_gpuExit->value();
    // debounce values live in the unit shown next to them (s or min)
    p.enterDebounceS = m_enterDebounce->value() * (m_enterUnit->currentIndex() == 1 ? 60 : 1);
    p.exitDebounceS = m_exitDebounce->value() * (m_exitUnit->currentIndex() == 1 ? 60 : 1);
    p.gameProcesses = m_processes->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    m_config->saveSceneParams(p);
    m_controller->scene()->setParams(p);
}

void ThermalPage::onConfigReloaded() {
    for (const auto& entry : m_boostSliders) {
        const int v = m_controller->fanBoost(entry.first);
        entry.second->blockSignals(true);
        entry.second->setValue(v);
        entry.second->blockSignals(false);
        m_boostSaved[entry.first] = v; // resync the dirty-guard: stale entries
        // here would silently skip the next real change's saveFanBoost
        for (const auto& label : m_boostLabels)
            if (label.first == entry.first)
                label.second->setText(v > 0 ? tr("%1%").arg(v) : tr("Auto"));
    }
    onCurveModeChanged(m_curveMode ? m_curveMode->currentIndex() : 0);
    // Scene card: pull the reloaded (possibly reset) params back into the
    // controls. Signals are blocked because the setters would otherwise fire
    // onSceneParamsChanged and re-persist the stale UI values over the reset.
    const SceneParams sp = m_config->loadSceneParams();
    auto resyncSpin = [](QSpinBox* box, int v) {
        box->blockSignals(true);
        box->setValue(v);
        box->blockSignals(false);
    };
    resyncSpin(m_gpuEnter, sp.gpuEnterThreshold);
    resyncSpin(m_gpuExit, sp.gpuExitThreshold);
    m_enterUnit->blockSignals(true);
    m_enterUnit->setCurrentIndex(0); // back to seconds
    m_enterUnit->blockSignals(false);
    resyncSpin(m_enterDebounce, sp.enterDebounceS);
    m_exitUnit->blockSignals(true);
    m_exitUnit->setCurrentIndex(0);
    m_exitUnit->blockSignals(false);
    resyncSpin(m_exitDebounce, sp.exitDebounceS);
    m_processes->blockSignals(true);
    m_processes->setPlainText(sp.gameProcesses.join(QLatin1Char('\n')));
    m_processes->blockSignals(false);
}

void ThermalPage::onSceneReset() {
    const SceneParams defaults;
    m_gpuEnter->setValue(defaults.gpuEnterThreshold);
    m_gpuExit->setValue(defaults.gpuExitThreshold);
    m_enterUnit->setCurrentIndex(0); // back to seconds
    m_exitUnit->setCurrentIndex(0);
    m_enterDebounce->setValue(defaults.enterDebounceS);
    m_exitDebounce->setValue(defaults.exitDebounceS);
    m_processes->setPlainText(defaults.gameProcesses.join(QLatin1Char('\n')));
}

void ThermalPage::onSnapshot(const SystemSnapshot& s) {
    Q_UNUSED(s);
    // Surface mode-write failures (e.g. G-Mode on unsupported machines) and
    // the automatic G-Mode -> Performance fallback.
    const auto failed = m_controller->failedMode();
    const bool gUnsupported = m_hal.thermal && !m_hal.thermal->supportsGMode();
    QString message;
    if (failed && *failed == ThermalMode::GMode)
        message = tr("G-Mode was rejected by the machine and could not be applied - "
                     "Ultra Performance is not active either. Try selecting it manually.");
    else if (gUnsupported && m_config->loadMode() == ThermalMode::GMode)
        message = tr("This machine does not support G-Mode, so the selection was not applied. "
                     "Pick Ultra Performance manually instead.");
    if (message.isEmpty()) {
        m_banner->hide();
    } else {
        m_banner->setText(message);
        m_banner->show();
    }
}

} // namespace dtb::ui
