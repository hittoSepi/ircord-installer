cmake_minimum_required(VERSION 3.20)

project(ircord-installer VERSION 0.1.0 DESCRIPTION "IRCord Linux installer" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG        v5.0.0
    GIT_SHALLOW    TRUE
)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(ftxui nlohmann_json)

set(INSTALLER_SOURCES
    src/main.cpp
    src/app.cpp
    src/manifest.cpp
    src/system/system.cpp
    src/install/installer.cpp
)

add_executable(ircord-installer ${INSTALLER_SOURCES})

target_include_directories(ircord-installer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ircord-installer PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(ircord-installer PRIVATE /W4 /WX-)
else()
    target_compile_options(ircord-installer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(ircord-installer PRIVATE pthread)
endif()

add_custom_command(TARGET ircord-installer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "$<TARGET_FILE_DIR:ircord-installer>/README.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/installer-manifest.example.json"
        "$<TARGET_FILE_DIR:ircord-installer>/installer-manifest.example.json"
    COMMENT "Copying installer companion files"
)

string(TOLOWER "${CMAKE_SYSTEM_NAME}" IRCORD_INSTALLER_SYSTEM)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" IRCORD_INSTALLER_PROCESSOR)

if(IRCORD_INSTALLER_SYSTEM STREQUAL "linux")
    if(IRCORD_INSTALLER_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(IRCORD_INSTALLER_PLATFORM "linux-x64")
    elseif(IRCORD_INSTALLER_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(IRCORD_INSTALLER_PLATFORM "linux-arm64")
    else()
        set(IRCORD_INSTALLER_PLATFORM "linux-${IRCORD_INSTALLER_PROCESSOR}")
    endif()
else()
    set(IRCORD_INSTALLER_PLATFORM "${IRCORD_INSTALLER_SYSTEM}-${IRCORD_INSTALLER_PROCESSOR}")
endif()

set(RELEASE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/release" CACHE PATH "Directory where release artifacts are generated")
set(RELEASE_BINARY_NAME "ircord-installer-${IRCORD_INSTALLER_PLATFORM}" CACHE STRING "Installer binary release artifact name")
set(RELEASE_SHA_NAME "${RELEASE_BINARY_NAME}.sha256" CACHE STRING "Installer checksum file name")

add_custom_command(
    OUTPUT
        "${RELEASE_OUTPUT_DIR}/${RELEASE_BINARY_NAME}"
        "${RELEASE_OUTPUT_DIR}/${RELEASE_SHA_NAME}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RELEASE_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ircord-installer>" "${RELEASE_OUTPUT_DIR}/${RELEASE_BINARY_NAME}"
    COMMAND ${CMAKE_COMMAND} -E sha256sum "${RELEASE_OUTPUT_DIR}/${RELEASE_BINARY_NAME}" > "${RELEASE_OUTPUT_DIR}/${RELEASE_SHA_NAME}"
    DEPENDS ircord-installer
    COMMENT "Creating installer release artifact ${RELEASE_BINARY_NAME}"
    VERBATIM
)

add_custom_target(ircord-installer_release
    DEPENDS
        "${RELEASE_OUTPUT_DIR}/${RELEASE_BINARY_NAME}"
        "${RELEASE_OUTPUT_DIR}/${RELEASE_SHA_NAME}"
)
