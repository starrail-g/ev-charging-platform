cmake_minimum_required(VERSION 3.16)
project(ev_user_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(ev-user-client
    src/main.cpp
    src/client_service.h
    src/client_service.cpp
)
target_link_libraries(ev-user-client PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_include_directories(ev-user-client PRIVATE src)

install(TARGETS ev-user-client RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  enable_testing()
  find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test)
  if(Qt${QT_VERSION_MAJOR}Test_FOUND)
    add_executable(ev-user-client-tests tests/client_service_test.cpp src/client_service.cpp src/client_service.h)
    target_link_libraries(ev-user-client-tests PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test)
    target_include_directories(ev-user-client-tests PRIVATE src)
    add_test(NAME ev-user-client-tests COMMAND ev-user-client-tests)
  endif()
endif()
