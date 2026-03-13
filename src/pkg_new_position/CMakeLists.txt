cmake_minimum_required(VERSION 3.8)
project(pkg_new_position)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(behaviortree_cpp_v3 REQUIRED)
find_package(geometry_msgs REQUIRED)

add_library(new_position SHARED src/new_position.cpp)
target_include_directories(new_position PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(new_position PUBLIC cxx_std_17)
target_compile_definitions(new_position PRIVATE BT_PLUGIN_EXPORT)
set_target_properties(new_position PROPERTIES POSITION_INDEPENDENT_CODE ON)
ament_target_dependencies(new_position
  rclcpp
  behaviortree_cpp_v3
  geometry_msgs)

install(TARGETS new_position
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
  geometry_msgs)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
