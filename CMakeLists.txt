cmake_minimum_required(VERSION 3.20)
project(ParkingClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ─── macOS Homebrew path hints ──────────────────────────────────────────────
if(APPLE)
    execute_process(COMMAND brew --prefix qt
        OUTPUT_VARIABLE BREW_QT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND brew --prefix qrencode
        OUTPUT_VARIABLE BREW_QRENCODE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND brew --prefix mosquitto
        OUTPUT_VARIABLE BREW_MOSQ OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND brew --prefix cjson
        OUTPUT_VARIABLE BREW_CJSON OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(BREW_QT)
        list(APPEND CMAKE_PREFIX_PATH "${BREW_QT}")
    endif()
    if(BREW_QRENCODE)
        list(APPEND CMAKE_PREFIX_PATH "${BREW_QRENCODE}")
    endif()
    if(BREW_MOSQ)
        list(APPEND CMAKE_PREFIX_PATH "${BREW_MOSQ}")
    endif()
endif()

# ─── Dependencies ───────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Multimedia)

find_library(QRENCODE_LIB qrencode REQUIRED
    HINTS "${BREW_QRENCODE}/lib")
find_path(QRENCODE_INC qrencode.h REQUIRED
    HINTS "${BREW_QRENCODE}/include")

find_library(MOSQUITTO_LIB mosquitto REQUIRED
    HINTS "${BREW_MOSQ}/lib")
find_path(MOSQUITTO_INC mosquitto.h REQUIRED
    HINTS "${BREW_MOSQ}/include")

# libgpiod v2 — required on Linux for the E3F-DS10C4 vehicle sensor.
# Not installed / stubbed out on macOS.
if(NOT APPLE)
    find_library(GPIOD_LIB gpiod REQUIRED)
    find_path(GPIOD_INC gpiod.h REQUIRED)
    if(NOT GPIOD_LIB OR NOT GPIOD_INC)
        message(FATAL_ERROR
            "libgpiod v2 not found. Install with: "
            "apt install libgpiod-dev (Debian/Ubuntu) or "
            "dnf install libgpiod-devel (Fedora)")
    endif()
endif()

# ─── Sources (one class per file) ───────────────────────────────────────────
set(SOURCES
    src/main.cpp

    # app
    src/app/Application.cpp
    src/app/CommandQueue.cpp
    src/app/Config.cpp
    src/app/DeviceId.cpp
    src/app/Logger.cpp
    src/app/LocalBridge.cpp
    src/app/NetworkInit.cpp

    # mqtt
    src/mqtt/MqttClient.cpp
    src/mqtt/MqttMessageBuilder.cpp
    src/mqtt/MqttMessageParser.cpp

    # hardware orchestration
    src/hardware/AudioPlayer.cpp
    src/hardware/BarrierManager.cpp
    src/hardware/DisplayMonitor.cpp
    src/hardware/NV9Manager.cpp
    src/hardware/VehicleSensorManager.cpp
    src/hardware/HardwareFactory.cpp

    # hardware drivers (real on Linux / stubbed on __APPLE__)
    src/hardware/drivers/BarrierControllerModbus.cpp
    src/hardware/drivers/NV9ValidatorSSP.cpp
    src/hardware/drivers/VehicleSensorGpiod.cpp

    # UI
    src/ui/MainWindow.cpp
    src/ui/screens/ScreenHelpers.cpp
    src/ui/screens/ScreenEntryIdle.cpp
    src/ui/screens/ScreenEntryOpen.cpp
    src/ui/screens/ScreenDisplayMessage.cpp
    src/ui/screens/ScreenExitHourly.cpp
    src/ui/screens/ScreenExitMonthly.cpp
    src/ui/widgets/TopBar.cpp
    src/ui/widgets/QRWidget.cpp
    src/ui/screens/ScreenPaymentSuccess.cpp
)

add_executable(parking_client ${SOURCES})
target_include_directories(parking_client PRIVATE
    src
    ${QRENCODE_INC}
    ${MOSQUITTO_INC}
    "${BREW_CJSON}/include")

if(NOT APPLE)
    target_include_directories(parking_client PRIVATE ${GPIOD_INC})
endif()

target_link_libraries(parking_client PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Network Qt6::Multimedia
    ${QRENCODE_LIB}
    ${MOSQUITTO_LIB})

if(NOT APPLE)
    target_link_libraries(parking_client PRIVATE ${GPIOD_LIB})
endif()

# ── Self-test binary ──────────────────────────────────────────────────────
# Exercises LocalBridge + Config + MessageFilter + Modbus bit layout end-to-
# end with a mock barrier driver. No real MQTT / serial / UI — runs in CI
# and on a laptop in ~50 ms.
add_executable(parking_selftest
    tests/parking_selftest.cpp
    src/app/Config.cpp
    src/app/LocalBridge.cpp
    src/types/MessageTypes.h
)
target_include_directories(parking_selftest PRIVATE
    src tests
    ${MOSQUITTO_INC}
    "${BREW_CJSON}/include")
target_link_libraries(parking_selftest PRIVATE
    Qt6::Core
    ${MOSQUITTO_LIB})
target_compile_options(parking_selftest PRIVATE
    -Wall -Wextra -Wno-unused-parameter)

add_executable(parking_test_server
    server_cpp/main.cpp)
target_include_directories(parking_test_server PRIVATE
    src
    ${MOSQUITTO_INC}
    "${BREW_CJSON}/include")
target_link_libraries(parking_test_server PRIVATE
    Qt6::Core
    ${MOSQUITTO_LIB})

target_compile_options(parking_client PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-unused-private-field>)
target_compile_options(parking_test_server PRIVATE
    -Wall -Wextra -Wno-unused-parameter)
if(BARRIER_DEBUG)
    target_compile_definitions(parking_client PRIVATE
        BARRIER_DEBUG=1 NV9_DEBUG=1)
endif()

# ─── Install ────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS parking_client DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY resources/ DESTINATION ${CMAKE_INSTALL_DATADIR}/parking_client)
install(FILES config.json DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/parking_client)
