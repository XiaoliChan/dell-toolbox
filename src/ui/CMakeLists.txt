set(DTB_UI_HEADERS
    MainWindow.h
    widgets/AreaChart.h
    widgets/Card.h
    widgets/CircularGauge.h
    widgets/NavIcons.h
    widgets/Sparkline.h
    widgets/FanCurveEditor.h
    pages/DashboardPage.h
    pages/ThermalPage.h
    pages/PerformancePage.h
    pages/BatteryPage.h
    pages/SettingsPage.h
)

set(DTB_UI_SOURCES
    MainWindow.cpp
    Theme.cpp
    widgets/Sparkline.cpp
    widgets/FanCurveEditor.cpp
    widgets/CircularGauge.cpp
    widgets/Card.cpp
    widgets/AreaChart.cpp
    widgets/NavIcons.cpp
    pages/BatteryPage.cpp
    pages/DashboardPage.cpp
    pages/PerformancePage.cpp
    pages/SettingsPage.cpp
    pages/ThermalPage.cpp
)

target_sources(dell-toolbox PRIVATE ${DTB_UI_SOURCES} ${DTB_UI_HEADERS})
target_include_directories(dell-toolbox PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dell-toolbox PRIVATE dtb_core)
