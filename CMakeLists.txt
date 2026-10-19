# SPDX-FileCopyrightText: Copyright 2026 Resident Evil Revelations Custom Server Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.19)
project(resrev1srv VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Network Sql)

add_executable(resrev1srv
    src/admin_service.cpp
    src/admin_service.h
    src/blowfish.cpp
    src/blowfish.h
    src/blowfish_tables.h
    src/config.cpp
    src/config.h
    src/file_log.cpp
    src/file_log.h
    src/http_server.cpp
    src/http_server.h
    src/json_writer.h
    src/main.cpp
    src/revelations_service.cpp
    src/revelations_service.h
    src/shadnet_verifier.cpp
    src/shadnet_verifier.h
    src/storage.cpp
    src/storage.h
    src/upload_decoder.cpp
    src/upload_decoder.h
)

target_link_libraries(resrev1srv PRIVATE Qt6::Core Qt6::Network Qt6::Sql)

if(MSVC)
    target_compile_options(resrev1srv PRIVATE /W4 /permissive-)
else()
    target_compile_options(resrev1srv PRIVATE -Wall -Wextra)
endif()

install(TARGETS resrev1srv RUNTIME DESTINATION bin)


if(WIN32)
    option(GAME_SERVER_DEPLOY_QT "Run windeployqt after building" ON)
    if(GAME_SERVER_DEPLOY_QT)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET resrev1srv POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "$<TARGET_FILE:resrev1srv>"
                COMMENT "Deploying Qt next to resrev1srv.exe"
                VERBATIM)
        else()
            message(WARNING "windeployqt not found in Qt's bin folder: Qt DLLs will not be copied")
        endif()
    endif()
endif()

# revelations-admin: Qt Widgets client for the admin API.
option(REVELATIONS_BUILD_ADMIN "Build the revelations-admin GUI (needs Qt Widgets)" ON)
if(REVELATIONS_BUILD_ADMIN)
    find_package(Qt6 6.10 QUIET COMPONENTS Widgets)
    if(Qt6Widgets_FOUND)
        add_executable(revelations-admin WIN32 MACOSX_BUNDLE
            admin/api_client.cpp
            admin/api_client.h
            admin/event_editor.cpp
            admin/event_editor.h
            admin/main.cpp
            admin/main_window.cpp
            admin/main_window.h
            admin/reward_catalog.h
            admin/reward_dialog.cpp
            admin/reward_dialog.h
            admin/stats_dialog.cpp
            admin/stats_dialog.h
        )
        target_link_libraries(revelations-admin PRIVATE Qt6::Widgets Qt6::Network)
        if(MSVC)
            target_compile_options(revelations-admin PRIVATE /W4 /permissive-)
        else()
            target_compile_options(revelations-admin PRIVATE -Wall -Wextra)
        endif()
        if(WIN32)
            option(GAME_SERVER_DEPLOY_QT "Run windeployqt after building" ON)
            if(GAME_SERVER_DEPLOY_QT)
                find_program(WINDEPLOYQT_EXECUTABLE windeployqt
                    HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin")
                if(WINDEPLOYQT_EXECUTABLE)
                    add_custom_command(TARGET revelations-admin POST_BUILD
                        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                                "$<TARGET_FILE:revelations-admin>"
                        COMMENT "Deploying Qt next to revelations-admin.exe"
                        VERBATIM)
                else()
                    message(WARNING "windeployqt not found in Qt's bin folder: Qt DLLs will not be copied")
                endif()
            endif()
        endif()
        install(TARGETS revelations-admin RUNTIME DESTINATION bin BUNDLE DESTINATION .)
    else()
        message(STATUS "Qt Widgets not found: revelations-admin will not be built")
    endif()
endif()
