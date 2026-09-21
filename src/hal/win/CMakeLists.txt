add_library(dtb_hal_win STATIC
    WmiConnection.cpp
    WinThermalHAL.cpp
    WinBatteryHAL.cpp
    WinGpuHAL.cpp
    WinLoadHAL.cpp
    WinChargeHAL.cpp
    DellAcpiChannel.cpp
    DpmServiceClient.cpp
    AwccConflict.cpp
    WinSystemInfo.cpp
    WinHalFactory.cpp
)

target_include_directories(dtb_hal_win PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(dtb_hal_win PUBLIC dtb_core Qt6::Core)
target_link_libraries(dtb_hal_win PRIVATE wbemuuid pdh shell32 setupapi)
