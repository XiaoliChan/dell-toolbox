add_subdirectory(core)

qt_add_executable(dell-toolbox main.cpp)

target_include_directories(dell-toolbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dell-toolbox PRIVATE Qt6::Widgets dtb_core)

add_subdirectory(ui)

# AUTORCC compiles this qrc (see top-level CMAKE_AUTORCC). qt_add_resources
# silently no-ops with a bare .qrc positional in this setup - the arrow image
# and autostart task XML were never embedded before this.
target_sources(dell-toolbox PRIVATE ${CMAKE_SOURCE_DIR}/res/resources.qrc)

if(DTB_HAL STREQUAL "win")
    add_subdirectory(hal/win)
    target_link_libraries(dell-toolbox PRIVATE dtb_hal_win)
    set_target_properties(dell-toolbox PROPERTIES WIN32_EXECUTABLE TRUE)
    # UAC lives in the linker-generated manifest (/MANIFESTUAC); a hand-written
    # rc manifest caused "side-by-side configuration is incorrect" on real
    # machines even though it parsed as valid XML.
    if(DTB_REQUIRE_ADMIN)
        target_sources(dell-toolbox PRIVATE ${CMAKE_SOURCE_DIR}/res/app.rc)
        target_link_options(dell-toolbox PRIVATE "/MANIFESTUAC:level='requireAdministrator'")
    endif()
endif()
