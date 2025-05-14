cmake_minimum_required(VERSION 3.30)
project(ethercat_example)

set(CMAKE_CXX_STANDARD 20)
find_package(kaylordut REQUIRED)

add_executable(ethercat_example main.cpp
        ethercat_node.cpp
        app.cpp)
target_link_libraries(ethercat_example ethercat ${kaylordut_LIBS})
