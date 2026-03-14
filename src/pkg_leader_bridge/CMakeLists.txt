cmake_minimum_required(VERSION 3.8)
project(pkg_leader_bridge)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(leader_bridge src/leader_bridge.cpp)
target_compile_features(leader_bridge PUBLIC cxx_std_17)
ament_target_dependencies(
  leader_bridge
  rclcpp
  geometry_msgs
  nav_msgs
  sensor_msgs
  std_msgs)

install(TARGETS leader_bridge
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()