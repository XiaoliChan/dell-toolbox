find_package(Qt6 REQUIRED COMPONENTS Test)

function(dtb_add_test NAME)
    qt_add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE dtb_core Qt6::Test)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

dtb_add_test(test_snapshot)
dtb_add_test(test_fancurve)
dtb_add_test(test_policychain)
dtb_add_test(test_scenedetector)
dtb_add_test(test_dynamic)
dtb_add_test(test_configstore)
dtb_add_test(test_mockhal)
dtb_add_test(test_controller)
dtb_add_test(test_logger)

dtb_add_test(test_ui)
target_sources(test_ui PRIVATE
    ../src/ui/Theme.cpp
    ../src/ui/widgets/Card.cpp
    ../src/ui/widgets/FanCurveEditor.cpp
    ../src/ui/pages/PerformancePage.cpp
    ../src/ui/pages/ThermalPage.cpp
    ../src/ui/pages/BatteryPage.cpp
    ../src/ui/pages/SettingsPage.cpp
)
target_link_libraries(test_ui PRIVATE Qt6::Widgets)
set_tests_properties(test_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

dtb_add_test(test_hal_contracts)

# Isolate subprocess-based tests from the host Windows power configuration.
qt_add_executable(fake_powercfg fake_powercfg.cpp)
target_link_libraries(fake_powercfg PRIVATE Qt6::Core)
set_target_properties(fake_powercfg PROPERTIES
    OUTPUT_NAME powercfg
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake-powercfg")
foreach(test_target test_controller test_ui)
    add_dependencies(${test_target} fake_powercfg)
    target_compile_definitions(${test_target} PRIVATE
        DTB_FAKE_POWERCFG_PATH="$<TARGET_FILE:fake_powercfg>")
endforeach()
