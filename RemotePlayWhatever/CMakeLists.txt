# CMakeList.txt : CMake project for RemotePlayWhatever, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

if(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Windows")
    set(SYSTEM_OS "Win")
elseif(${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
    set(SYSTEM_OS "Linux")
else()
    message(FATAL_ERROR "Unsupported platform!")
endif()

if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
    set(SYSTEM_ARCH "64")
else()
    set(SYSTEM_ARCH "32")
endif()

add_custom_target(OpenSteamAPI)
set(OSW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../open-steamworks")
set(OSW_INCLUDE_PATH "${OSW_ROOT}/OpenSteamworks")
set(OSW_LIB_ROOT "${OSW_ROOT}/OpenSteamAPI")
if(${SYSTEM_OS} STREQUAL "Win")
    if(${SYSTEM_ARCH} STREQUAL "64")
        set(OSW_LIB_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI64.lib")
        set(OSW_DLL_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI64.dll")
        add_custom_command(TARGET OpenSteamAPI
            WORKING_DIRECTORY "${OSW_ROOT}"
            COMMAND msbuild /p:Platform=x64 /p:Configuration=Release
            VERBATIM
        )
    else()
        set(OSW_LIB_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI.lib")
        set(OSW_DLL_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI.dll")
        add_custom_command(TARGET OpenSteamAPI
            WORKING_DIRECTORY "${OSW_ROOT}"
            COMMAND msbuild /p:Platform=Win32 /p:Configuration=Release
            VERBATIM
        )
    endif()
else()
    set(OSW_LIB_PATH "${OSW_LIB_ROOT}/lib/${SYSTEM_OS}${SYSTEM_ARCH}/OpenSteamAPI.a")
    add_custom_command(TARGET OpenSteamAPI
        PRE_BUILD
        WORKING_DIRECTORY "${OSW_LIB_ROOT}/src"
        COMMAND make
        VERBATIM
    )
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
            "ClickablePanel.h"
            "ClickablePanel.cpp"
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
)


if(${SYSTEM_OS} STREQUAL "Win")
    add_executable(RemotePlayWhatever WIN32 ${APPSRCs} "appresource.rc")
    add_custom_command(TARGET RemotePlayWhatever POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${OSW_DLL_PATH} $<TARGET_FILE_DIR:RemotePlayWhatever>
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

