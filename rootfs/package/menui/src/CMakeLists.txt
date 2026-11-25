cmake_minimum_required(VERSION 3.10)
project(Menui VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SDL3 REQUIRED)
find_package(SDL3_ttf REQUIRED)

add_library(menui SHARED src/menui.cpp)

target_include_directories(menui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(menui PUBLIC
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
)

target_include_directories(menui PUBLIC ${SDL3_INCLUDE_DIRS})

set_target_properties(menui PROPERTIES PREFIX "lib")

include(GNUInstallDirs)
install(TARGETS menui
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES lua/menui.lua
    DESTINATION share/lua/5.1
)
