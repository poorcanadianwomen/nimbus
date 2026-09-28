cmake_minimum_required(VERSION 3.24)

project(nimbus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(NIMBUS_BUILD_TESTS "Build unit tests" ON)
option(NIMBUS_WERROR "Treat warnings as errors" OFF)

include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network Gui Widgets Svg)

add_library(nimbus_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nimbus_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
  if(NIMBUS_WERROR)
    target_compile_options(nimbus_warnings INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------- core ------
# No GUI dependencies here on purpose: everything in nimbus_core is testable
# headlessly and the event loop owner is injected by the app.
add_library(nimbus_core STATIC
  src/core/log.cpp
  src/core/ws.cpp
  src/core/settings.cpp
  src/core/models.cpp
  src/core/store.cpp
  src/core/rest.cpp
  src/core/session.cpp
  src/core/events.cpp
  src/core/app.cpp
)
target_link_libraries(nimbus_core
  PUBLIC
    Qt6::Core
    Qt6::Network
    nlohmann_json::nlohmann_json
  PRIVATE
    nimbus_warnings
)
target_include_directories(nimbus_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# ------------------------------------------------------------------ ui ------
qt_add_resources(nimbus_resources resources/nimbus.qrc)

add_executable(nimbus
  src/main.cpp
  src/ui/theme.cpp
  src/ui/avatar_cache.cpp
  src/ui/server_rail.cpp
  src/ui/channel_list.cpp
  src/ui/message_model.cpp
  src/ui/message_delegate.cpp
  src/ui/message_list.cpp
  src/ui/composer.cpp
  src/ui/emoji_picker.cpp
  src/ui/settings_window.cpp
  src/ui/chat_window.cpp
  src/ui/login_window.cpp
  src/ui/icons.cpp
  src/ui/motion.cpp
  src/ui/icon_button.cpp
  src/ui/loading_bars.cpp
  ${nimbus_resources}
)
target_link_libraries(nimbus PRIVATE
  nimbus_core
  Qt6::Widgets
  Qt6::Gui
  Qt6::Svg
  nimbus_warnings
)

if(NIMBUS_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(nimbus_tests tests/test_main.cpp)
  target_link_libraries(nimbus_tests PRIVATE nimbus_core nimbus_warnings Qt6::Test)
  add_test(NAME nimbus_tests COMMAND nimbus_tests)

  # Layout regressions need a real QApplication and real widget geometry. The
  # headless suite above cannot see them: the transcript shipped a delegate whose
  # paint() was empty and whose sizeHint() was a constant, and every height
  # assertion agreed with it because nothing compared a predicted pixel height
  # against an actual one, or counted a pixel that was not the background.
  # Runs offscreen; every assertion here blits a real glyph and looks at it.
  add_executable(nimbus_layout_tests
    tests/test_transcript.cpp
    src/ui/theme.cpp
    src/ui/icons.cpp
    src/ui/message_delegate.cpp
    src/ui/message_model.cpp
    src/ui/message_list.cpp
    src/ui/avatar_cache.cpp
    src/ui/motion.cpp
  )
  target_link_libraries(nimbus_layout_tests PRIVATE
    nimbus_core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
    Qt6::Network
    Qt6::Test
    nimbus_warnings
  )
  add_test(NAME nimbus_layout_tests COMMAND nimbus_layout_tests)
  set_tests_properties(nimbus_layout_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # The three panes, not the transcript. Its own target because every suite file
  # carries its own QTEST_MAIN, and two mains in one binary is a link error rather
  # than a merge. Pulls in the whole shell, which is why it is the slowest build.
  # The composer's send control and every rail glyph are loaded from the resource
  # bundle, so this target has to compile it too. Without it the icon lookups miss
  # and the render shows a broken-image placeholder where the send arrow belongs.
  qt_add_resources(nimbus_shell_icon_resources resources/nimbus.qrc)
  add_executable(nimbus_shell_tests
    tests/test_shell.cpp
    ${nimbus_shell_icon_resources}
    src/ui/theme.cpp
    src/ui/icons.cpp
    src/ui/message_delegate.cpp
    src/ui/message_model.cpp
    src/ui/message_list.cpp
    src/ui/avatar_cache.cpp
    src/ui/server_rail.cpp
    src/ui/channel_list.cpp
    src/ui/chat_window.cpp
    src/ui/composer.cpp
    src/ui/emoji_picker.cpp
    src/ui/motion.cpp
  )
  target_link_libraries(nimbus_shell_tests PRIVATE
    nimbus_core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
    Qt6::Network
    Qt6::Test
    nimbus_warnings
  )
  add_test(NAME nimbus_shell_tests COMMAND nimbus_shell_tests)
  set_tests_properties(nimbus_shell_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # The sign-in form. Its own target for the same reason as the shell suite: one
  # QTEST_MAIN per binary.
  add_executable(nimbus_login_tests
    tests/test_login.cpp
    src/ui/theme.cpp
    src/ui/login_window.cpp
  )
  target_link_libraries(nimbus_login_tests PRIVATE
    nimbus_core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
    nimbus_warnings
  )
  add_test(NAME nimbus_login_tests COMMAND nimbus_login_tests)
  set_tests_properties(nimbus_login_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Message formatting. No widgets and no GUI plugin, so it belongs with the core
  # suite's speed and runs headless without a platform.
  add_executable(nimbus_format_tests
    tests/test_format.cpp
    src/ui/message_format.cpp
  )
  target_link_libraries(nimbus_format_tests PRIVATE
    nimbus_core
    Qt6::Core
    Qt6::Test
    nimbus_warnings
  )
  add_test(NAME nimbus_format_tests COMMAND nimbus_format_tests)

  # The icon pipeline is a third-party SVG set recoloured at paint time. Both of
  # its failure modes are silent -- a glyph that renders empty, or a raster built
  # at logical size on a HiDPI screen -- so they are checked by counting pixels.
  # The suite reads the glyphs through the same ":/icons/tabler/..." paths the app
  # uses, so the resource bundle has to be compiled into this binary too. Without
  # it every lookup misses and the suite reports empty pixmaps rather than passing.
  qt_add_resources(nimbus_icon_resources resources/nimbus.qrc)
  add_executable(nimbus_icon_tests
    tests/test_icons.cpp
    src/ui/theme.cpp
    src/ui/icons.cpp
    ${nimbus_icon_resources}
  )
  target_link_libraries(nimbus_icon_tests PRIVATE
    nimbus_core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
    Qt6::Test
    nimbus_warnings
  )
  add_test(NAME nimbus_icon_tests COMMAND nimbus_icon_tests)
  set_tests_properties(nimbus_icon_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Motion primitives. The reason these are tested at all is reduce-motion: it is
  # an accessibility promise, and a promise enforced only by call sites is a
  # promise the next edit can quietly break.
  add_executable(nimbus_motion_tests
    tests/test_motion.cpp
    src/ui/theme.cpp
    src/ui/motion.cpp
  )
  target_link_libraries(nimbus_motion_tests PRIVATE
    nimbus_core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
    nimbus_warnings
  )
  add_test(NAME nimbus_motion_tests COMMAND nimbus_motion_tests)
  set_tests_properties(nimbus_motion_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()