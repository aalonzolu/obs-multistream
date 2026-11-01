cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  get_target_property(_wrap_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  list(FILTER _wrap_opengl_libs EXCLUDE REGEX "AGL")
  set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_opengl_libs}")
endif()

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets Qt6::Network
)

find_path(MULTISTREAM_FFMPEG_INCLUDE_DIR libavformat/avformat.h REQUIRED)
foreach(_ffmpeg_component IN ITEMS avcodec avformat avutil)
  find_library(MULTISTREAM_${_ffmpeg_component}_LIBRARY NAMES ${_ffmpeg_component} REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${MULTISTREAM_${_ffmpeg_component}_LIBRARY}")
endforeach()
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE "${MULTISTREAM_FFMPEG_INCLUDE_DIR}")

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

set(MULTISTREAM_GOOGLE_CLIENT_ID "" CACHE STRING "Google OAuth desktop client id")
set(MULTISTREAM_GOOGLE_CLIENT_SECRET "" CACHE STRING "Google OAuth desktop client secret (public client)")
set(MULTISTREAM_META_BROKER_URL "" CACHE STRING "Base URL of the Facebook broker service")
include("${CMAKE_CURRENT_SOURCE_DIR}/local-config.cmake" OPTIONAL)
configure_file(src/build-config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build-config.hpp" @ONLY)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/plugin.cpp
    src/plugin.hpp
    src/core/result.hpp
    src/media/encoder-pool.cpp
    src/media/encoder-pool.hpp
    src/media/media-engine.cpp
    src/media/media-engine.hpp
    src/media/media-profile.cpp
    src/media/media-profile.hpp
    src/media/output-runtime.cpp
    src/media/output-runtime.hpp
    src/net/http-client.cpp
    src/net/http-client.hpp
    src/net/loopback-receiver.cpp
    src/net/loopback-receiver.hpp
    src/orchestration/coordinator.cpp
    src/orchestration/coordinator.hpp
    src/providers/local-test.cpp
    src/providers/local-test.hpp
    src/providers/meta-broker.cpp
    src/providers/meta-broker.hpp
    src/providers/provider.cpp
    src/providers/provider.hpp
    src/providers/tiktok-streamlabs.cpp
    src/providers/tiktok-streamlabs.hpp
    src/providers/youtube.cpp
    src/providers/youtube.hpp
    src/radio/radio-config.cpp
    src/radio/radio-config.hpp
    src/radio/radio-station.cpp
    src/radio/radio-station.hpp
    src/radio/radio-stream.cpp
    src/radio/radio-stream.hpp
    src/security/pkce.cpp
    src/security/pkce.hpp
    src/security/secret-store.hpp
    src/storage/journal.cpp
    src/storage/journal.hpp
    src/storage/json-file.cpp
    src/storage/json-file.hpp
    src/storage/settings.cpp
    src/storage/settings.hpp
    src/testing/websocket-vendor.cpp
    src/testing/websocket-vendor.hpp
    src/ui/brand-mark.cpp
    src/ui/brand-mark.hpp
    src/ui/destination-badge.cpp
    src/ui/destination-badge.hpp
    src/ui/destination-row.cpp
    src/ui/destination-row.hpp
    src/ui/palette.cpp
    src/ui/palette.hpp
    src/ui/presentation.cpp
    src/ui/presentation.hpp
    src/ui/radio-dialog.cpp
    src/ui/radio-dialog.hpp
    src/ui/radio-row.cpp
    src/ui/radio-row.hpp
    src/ui/session-controller.cpp
    src/ui/session-controller.hpp
    src/ui/start-dialog.cpp
    src/ui/start-dialog.hpp
    src/ui/status-pill.cpp
    src/ui/status-pill.hpp
    src/ui/status-strip.cpp
    src/ui/status-strip.hpp
    src/ui/status-window.cpp
    src/ui/status-window.hpp
    src/ui/stream-button-bridge.cpp
    src/ui/stream-button-bridge.hpp
    src/ui/widgets.cpp
    src/ui/widgets.hpp
)

if(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE curl)
else()
  find_package(CURL REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)
endif()

if(APPLE)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/security/secret-store-macos.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Security.framework>" "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation.framework>")
elseif(WIN32)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/security/secret-store-windows.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Advapi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(libsecret-1 REQUIRED IMPORTED_TARGET libsecret-1)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/security/secret-store-linux.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::libsecret-1)
else()
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/security/secret-store-unsupported.cpp)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
