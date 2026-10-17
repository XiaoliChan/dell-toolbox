#include <QtTest>
#include <QAbstractItemView>
#include <QTemporaryDir>
#include <QProcess>
#include <QDir>
#include "hal/mock/MockHal.h"
#include "ui/Theme.h"
#include "ui/pages/PerformancePage.h"
#include "ui/pages/ThermalPage.h"
#include "ui/pages/BatteryPage.h"
#include "ui/pages/SettingsPage.h"

namespace dtb::ui {
class UiRegressionTest : public QObject {
    Q_OBJECT
    static void settleReadQueries(QWidget& page) {
        // Constructors only query /list. Finish before page destruction.
        for (auto* process : page.findChildren<QProcess*>()) {
            if (!process->waitForFinished(10000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
private slots:
    void initTestCase() {
        applyTheme(*qobject_cast<QApplication*>(qApp));
    }
    void popupFlags() {
        QWidget root;
        QComboBox combo(&root);
        combo.addItems({"Balanced", "High performance"});
        softenComboPopups(&root);
        QVERIFY(combo.view()->window()->testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(combo.view()->window()->windowFlags().testFlag(Qt::FramelessWindowHint));
    }
    void powerPlanRefreshPreservesPopup() {
        QTemporaryDir dir;
        ConfigStore cfg(dir.filePath("test.ini"));
        MockHal mock;
        const auto hal = mock.makeSet();
        Controller controller(hal, &cfg);
        controller.setPowerCfgProgram(QString::fromUtf8(DTB_FAKE_POWERCFG_PATH));
        PerformancePage page(hal, &controller, &cfg);
        settleReadQueries(page);
        softenComboPopups(&page);
        QVector<PowerPlanEntry> plans{{"a", "Balanced"}, {"b", "High performance"}};
        page.updatePowerPlans(plans, "a");
        page.show();
        page.m_planCombo->showPopup();
        page.m_planCombo->view()->setCurrentIndex(page.m_planCombo->model()->index(1, 0));
        page.updatePowerPlans(plans, "a");
        QCOMPARE(page.m_planCombo->view()->currentIndex().row(), 1);
        page.grab().save(QDir::temp().filePath(QStringLiteral("dell-toolbox-performance-review.png")));
        page.m_planCombo->view()->window()->grab().save(QDir::temp().filePath(QStringLiteral("dell-toolbox-popup-review.png")));
        page.m_planCombo->hidePopup();
        page.updatePowerPlans(plans, "B");
        QCOMPARE(page.m_planCombo->currentData().toString(), QString("b"));
    }
    void draftsDoNotActivateOverride() {
        QTemporaryDir dir;
        ConfigStore cfg(dir.filePath("test.ini"));
        cfg.saveManualProfiles({{"Draft", 45, 54}});
        MockHal mock;
        const auto hal = mock.makeSet();
        Controller controller(hal, &cfg);
        controller.setPowerCfgProgram(QString::fromUtf8(DTB_FAKE_POWERCFG_PATH));
        PerformancePage page(hal, &controller, &cfg);
        settleReadQueries(page);
        page.m_saveProfile->click();
        QVERIFY(controller.manualProfileName().isEmpty());
        QVERIFY(page.m_activeLabel->text().contains("not applied"));
        QVERIFY(mock.writes.isEmpty());
    }
    void sceneUnitsAndReset() {
        QTemporaryDir dir;
        ConfigStore cfg(dir.filePath("test.ini"));
        MockHal mock;
        const auto hal = mock.makeSet();
        Controller controller(hal, &cfg);
        controller.setPowerCfgProgram(QString::fromUtf8(DTB_FAKE_POWERCFG_PATH));
        ThermalPage page(hal, &controller, &cfg);
        settleReadQueries(page);
        QComboBox* unit = nullptr;
        for (auto* combo : page.findChildren<QComboBox*>())
            if (combo->count() == 2 && combo->itemText(1) == "min") { unit = combo; break; }
        QVERIFY(unit);
        auto* spin = unit->parentWidget()->findChild<QSpinBox*>();
        QVERIFY(spin);
        spin->setValue(120);
        unit->setCurrentIndex(1);
        QCOMPARE(spin->value(), 2);
        QCOMPARE(cfg.loadSceneParams().enterDebounceS, 120);
        unit->setCurrentIndex(0);
        QCOMPARE(cfg.loadSceneParams().enterDebounceS, 120);
        unit->setCurrentIndex(1);
        cfg.resetToDefaults();
        controller.reloadFromConfig();
        QCOMPARE(unit->currentIndex(), 0);
        QCOMPARE(spin->maximum(), 600);
        QCOMPARE(unit->property("wasMin").toBool(), false);
    }
    void failedChargeWriteRestoresRadio() {
        QTemporaryDir dir;
        ConfigStore cfg(dir.filePath("test.ini"));
        MockHal mock;
        mock.chargeWritesSucceed = false;
        const auto hal = mock.makeSet();
        Controller controller(hal, &cfg);
        controller.setPowerCfgProgram(QString::fromUtf8(DTB_FAKE_POWERCFG_PATH));
        BatteryPage page(hal, &controller, &cfg);
        QRadioButton *adaptive = nullptr, *standard = nullptr;
        for (auto* radio : page.findChildren<QRadioButton*>()) {
            if (radio->property("chargeKey") == "adaptive") adaptive = radio;
            if (radio->property("chargeKey") == "standard") standard = radio;
        }
        QVERIFY(adaptive && standard);
        standard->click();
        QVERIFY(adaptive->isChecked());
        QVERIFY(!standard->isChecked());
    }
};
}
using dtb::ui::UiRegressionTest;
QTEST_MAIN(UiRegressionTest)
#include "test_ui.moc"
