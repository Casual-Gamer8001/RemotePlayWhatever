# CMakeList.txt : CMake project for RemotePlayWhatever, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

if(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    set(SYSTEM_OS "Win")
elseif(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    set(SYSTEM_OS "Linux")
elseif(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Darwin")
    set(SYSTEM_OS "Mac")
else()
    message(FATAL_ERROR "Unsupported platform!")
endif()

if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    set(SYSTEM_ARCH "64")
else()
    set(SYSTEM_ARCH "32")
endif()

set(OSW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../open-steamworks")
set(OSW_INCLUDE_PATH "${OSW_ROOT}/OpenSteamworks")
set(OSW_LIB_ROOT "${OSW_ROOT}/OpenSteamAPI")
# open-steamworks' projects pin the VS 2019 toolset (v142). Build them with the
# MSVC toolset configuring this project, so Visual Studio 2022 and newer work
# without the VS 2019 build tools (from the Rammy-3 fork).
if(MSVC AND MSVC_TOOLSET_VERSION)
    set(OSW_MSBUILD_TOOLSET "/p:PlatformToolset=v${MSVC_TOOLSET_VERSION}")
endif()
if(${SYSTEM_OS} STREQUAL "Win")
    add_custom_target(OpenSteamAPI)
    if(${SYSTEM_ARCH} STREQUAL "64")
        set(OSW_LIB_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI64.lib")
        set(OSW_DLL_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI64.dll")
        add_custom_command(TARGET OpenSteamAPI
            WORKING_DIRECTORY "${OSW_ROOT}"
            COMMAND msbuild /p:Platform=x64 /p:Configuration=Release ${OSW_MSBUILD_TOOLSET}
            VERBATIM
        )
    else()
        set(OSW_LIB_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI.lib")
        set(OSW_DLL_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI.dll")
        add_custom_command(TARGET OpenSteamAPI
            WORKING_DIRECTORY "${OSW_ROOT}"
            COMMAND msbuild /p:Platform=Win32 /p:Configuration=Release ${OSW_MSBUILD_TOOLSET}
            VERBATIM
        )
    endif()
else()
    # OpenSteamAPI's own Makefile only knows Linux and g++ (and writes into the
    # submodule), so its sources are built here instead. On macOS a small
    # Objective-C++ file finds ~/Library/Application Support for it.
    set(OSW_SRCS "${OSW_LIB_ROOT}/src/CCallbackMgr.cpp"
                 "${OSW_LIB_ROOT}/src/ClientWrap.cpp"
                 "${OSW_LIB_ROOT}/src/Interface_OSW.cpp")
    if(APPLE)
        enable_language(OBJCXX)
        list(APPEND OSW_SRCS "${OSW_LIB_ROOT}/src/OSXPathHelper.mm")
    endif()
    add_library(OpenSteamAPI STATIC ${OSW_SRCS})
    target_include_directories(OpenSteamAPI PRIVATE "${OSW_INCLUDE_PATH}")
    # Steam's client library is loaded with dlopen.
    target_link_libraries(OpenSteamAPI PUBLIC ${CMAKE_DL_LIBS})
    if(APPLE)
        target_link_libraries(OpenSteamAPI PUBLIC "-framework Foundation")
    endif()
    set(OSW_LIB_PATH OpenSteamAPI)
endif()



if(${SYSTEM_OS} STREQUAL "Win")
    # Release builds update themselves over HTTPS; wxWebRequest is in wx's net library.
    find_package(wxWidgets COMPONENTS core base adv net)
else()
    find_package(wxWidgets COMPONENTS core base adv)
endif()
if(NOT wxWidgets_FOUND)
    message(FATAL_ERROR "wxWidgets not found!")
endif()

include(${wxWidgets_USE_FILE})

# Release builds made by GitHub Actions know their build number and whose
# releases they come from, so they can offer updates. Local builds (0) don't.
set(RPW_BUILD_NUMBER 0 CACHE STRING "Release build number (0 = local build, no update checks)")
set(RPW_UPDATE_REPO "" CACHE STRING "GitHub owner/repo whose releases this build updates itself from")
if(NOT RPW_BUILD_NUMBER MATCHES "^[0-9]+$")
    message(FATAL_ERROR "RPW_BUILD_NUMBER must be a number, not '${RPW_BUILD_NUMBER}'")
endif()
if(NOT RPW_UPDATE_REPO MATCHES "^([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)?$")
    message(FATAL_ERROR "RPW_UPDATE_REPO must look like owner/repo, not '${RPW_UPDATE_REPO}'")
endif()
configure_file(BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.h" @ONLY)

set(APPSRCs "RemotePlayWhatever.cpp"
            "ClientVTableByName.h"
            "SteamStuff.h"
            "SteamStuff.cpp"
            "RemoteApp.h"
            "RemoteApp.cpp"
            "RemotePlayInviteHandler.h"
            "RemotePlayInviteHandler.cpp"
            "QueueInviter.cpp"
            "QueueInviter.h"
            "FriendsListFrame.h"
            "FriendsListFrame.cpp"
            "ControlFrame.h"
            "ControlFrame.cpp"
            "UiWidgets.h"
            "UiWidgets.cpp"
            "FriendPanel.h"
            "FriendPanel.cpp"
            "wxSteamStuff.h"
            "wxSteamStuff.cpp"
            "GameInfo.h"
            "GameInfo.cpp"
            "UiHelpers.h"
            "UiHelpers.cpp"
            "TrayIcon.h"
            "TrayIcon.cpp"
            "Updater.h"
            "Updater.cpp"
            "UpdateNeededDialog.h"
            "UpdateNeededDialog.cpp"
            "QrCodeDialog.h"
            "QrCodeDialog.cpp"
            "qrcodegen.hpp"
            "qrcodegen.cpp"
            "GameProcess.h"
            "GameProcess.cpp"
)


if(${SYSTEM_OS} STREQUAL "Win")
    # The .manifest is merged into the exe's manifest (DPI awareness).
    add_executable(RemotePlayWhatever WIN32 ${APPSRCs} "appresource.rc" "RemotePlayWhatever.manifest")
    add_custom_command(TARGET RemotePlayWhatever POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${OSW_DLL_PATH} $<TARGET_FILE_DIR:RemotePlayWhatever>
    )
elseif(${SYSTEM_OS} STREQUAL "Mac")
    # An app bundle, with its icon made from appicon.png by macOS's own tools.
    set(RPW_ICONSET "${CMAKE_CURRENT_BINARY_DIR}/RemotePlayWhatever.iconset")
    set(RPW_ICNS "${CMAKE_CURRENT_BINARY_DIR}/RemotePlayWhatever.icns")
    set(RPW_ICON_COMMANDS "")
    foreach(icon "16x16:16" "16x16@2x:32" "32x32:32" "32x32@2x:64" "128x128:128" "128x128@2x:256" "256x256:256")
        string(REPLACE ":" ";" parts "${icon}")
        list(GET parts 0 name)
        list(GET parts 1 px)
        list(APPEND RPW_ICON_COMMANDS COMMAND sips -z ${px} ${px} "${CMAKE_CURRENT_SOURCE_DIR}/appicon.png"
                                      --out "${RPW_ICONSET}/icon_${name}.png")
    endforeach()
    add_custom_command(OUTPUT "${RPW_ICNS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RPW_ICONSET}"
        ${RPW_ICON_COMMANDS}
        COMMAND iconutil -c icns -o "${RPW_ICNS}" "${RPW_ICONSET}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.png"
        VERBATIM
    )
    set_source_files_properties("${RPW_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    add_executable(RemotePlayWhatever MACOSX_BUNDLE ${APPSRCs} "${RPW_ICNS}")
    set_target_properties(RemotePlayWhatever PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Remote Play Whatever"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.casualgamer8001.remoteplaywhatever"
        MACOSX_BUNDLE_ICON_FILE "RemotePlayWhatever.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${RPW_BUILD_NUMBER}"
        MACOSX_BUNDLE_COPYRIGHT "MIT License"
    )
else()
    add_executable(RemotePlayWhatever ${APPSRCs})
endif()
add_dependencies(RemotePlayWhatever OpenSteamAPI)

target_compile_definitions(RemotePlayWhatever PRIVATE STEAMWORKS_CLIENT_INTERFACES)
target_include_directories(RemotePlayWhatever PUBLIC ${OSW_INCLUDE_PATH} ${wxWidgets_INCLUDE_DIRS})
target_include_directories(RemotePlayWhatever PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(RemotePlayWhatever ${OSW_LIB_PATH} ${wxWidgets_LIBRARIES})
if(${SYSTEM_OS} STREQUAL "Win")
    # PlaySound for the guest join/leave sounds; BCrypt (SHA-256) to check updates.
    target_link_libraries(RemotePlayWhatever winmm bcrypt)
endif()
if(${SYSTEM_OS} STREQUAL "Linux")
    target_link_libraries(RemotePlayWhatever ${CMAKE_DL_LIBS})
    set_target_properties(RemotePlayWhatever PROPERTIES OUTPUT_NAME "remoteplaywhatever")
    install(TARGETS RemotePlayWhatever DESTINATION "bin")
    # Menu entry and icon (also what the AppImage is made from).
    install(FILES "remoteplaywhatever.desktop" DESTINATION "share/applications")
    install(FILES "appicon.png" DESTINATION "share/icons/hicolor/256x256/apps" RENAME "remoteplaywhatever.png")

    file(READ "/etc/issue" ISSUE)
    string(REGEX MATCH "Debian|Ubuntu" DISTRO ${ISSUE})
    if(DISTRO MATCHES "Ubuntu" OR DISTRO MATCHES "Debian")
        set(CPACK_GENERATOR "DEB")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lets you force steam remote play together for any game")
        set(CPACK_PACKAGE_DESCRIPTION "Tiny app that lets you force steam remote play together for any game")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "m4dEngi")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        include(CPack)
    endif()
endif()

