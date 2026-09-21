add_library(dtb_core STATIC
    FanCurve.cpp
    PolicyChain.cpp
    SceneDetector.cpp
    ScenePolicy.cpp
    FailsafePolicy.cpp
    DynamicPolicy.cpp
    ManualOverride.cpp
    BaselinePolicy.cpp
    ConfigStore.cpp
    ../hal/mock/MockHal.cpp
    Controller.cpp
    Logger.cpp
    Autostart.cpp
)

target_include_directories(dtb_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dtb_core PUBLIC Qt6::Core)
