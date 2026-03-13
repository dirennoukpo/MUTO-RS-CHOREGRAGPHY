cmake_minimum_required(VERSION 3.8)
project(pkg_battery_level)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(behaviortree_cpp_v3 REQUIRED)
find_package(std_msgs REQUIRED)

add_library(batterylevel SHARED src/batterylevel.cpp)
target_include_directories(batterylevel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(batterylevel PUBLIC cxx_std_17)
target_compile_definitions(batterylevel PRIVATE BT_PLUGIN_EXPORT)
set_target_properties(batterylevel PROPERTIES POSITION_INDEPENDENT_CODE ON)
ament_target_dependencies(batterylevel
  rclcpp
  behaviortree_cpp_v3
  std_msgs)

install(TARGETS batterylevel
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)

install(DIRECTORY include/
  DESTINATION include)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(
  rclcpp
  behaviortree_cpp_v3
  std_msgs)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
