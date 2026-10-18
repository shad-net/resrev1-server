# SPDX-FileCopyrightText: Copyright 2026 shadNet Custom Server Project
# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.19)
project(game-server-skeleton VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network)

add_executable(game-server
    src/config.cpp src/config.h
    src/file_log.cpp src/file_log.h
    src/handler.cpp src/handler.h
    src/http_server.cpp src/http_server.h
    src/main.cpp
)
target_link_libraries(game-server PRIVATE Qt6::Core Qt6::Network)

if(MSVC)
    target_compile_options(game-server PRIVATE /W4 /permissive-)
else()
    target_compile_options(game-server PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    option(GAME_SERVER_DEPLOY_QT "Run windeployqt after building" ON)
    if(GAME_SERVER_DEPLOY_QT)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET game-server POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "$<TARGET_FILE:game-server>"
                COMMENT "Deploying Qt next to game-server.exe"
                VERBATIM)
        else()
            message(WARNING "windeployqt not found in Qt's bin folder: Qt DLLs will not be copied")
        endif()
    endif()
endif()

install(TARGETS game-server RUNTIME DESTINATION bin)
