# SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(ocarina2-updater main.cpp ../frontend_common/update_installer.cpp)
target_include_directories(ocarina2-updater PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(ocarina2-updater PRIVATE cxx_std_20)
target_link_libraries(ocarina2-updater PRIVATE user32 shell32)
set_target_properties(ocarina2-updater PROPERTIES WIN32_EXECUTABLE ON)

# The helper runs from a temporary folder, away from the DLLs next to Ocarina2.exe.
if (MSVC)
    set_property(TARGET ocarina2-updater PROPERTY MSVC_RUNTIME_LIBRARY
                 "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(ocarina2-updater PRIVATE -static)
endif()
