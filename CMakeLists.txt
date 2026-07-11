cmake_minimum_required(VERSION 3.22)
project(ethercat_example)

set(CMAKE_CXX_STANDARD 20)
find_package(kaylordut REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(ethercat_example main.cpp
        ethercat_node.cpp
        app.cpp)
target_link_libraries(ethercat_example ethercat ${kaylordut_LIBS})

# DC AssignActivate scanner - tests a single activation code
add_executable(test_dc_scan test_dc_scan.cpp)
target_link_libraries(test_dc_scan ethercat)

# Motor continuous rotation test - position + 100 each cycle
add_executable(motor_continuous motor_continuous.cpp)
target_link_libraries(motor_continuous ethercat ${kaylordut_LIBS} yaml-cpp)
