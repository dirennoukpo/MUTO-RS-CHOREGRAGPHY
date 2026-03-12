cmake_minimum_required(VERSION 3.8)
project(location)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(behaviortree_cpp_v3 REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)

add_library(compute_offset_goal SHARED src/compute_offset_goal.cpp)
target_include_directories(compute_offset_goal PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(compute_offset_goal PUBLIC cxx_std_17)
target_compile_definitions(compute_offset_goal PRIVATE BT_PLUGIN_EXPORT)
set_target_properties(compute_offset_goal PROPERTIES POSITION_INDEPENDENT_CODE ON)
ament_target_dependencies(compute_offset_goal
  rclcpp
  behaviortree_cpp_v3
  std_msgs
  geometry_msgs)

add_library(get_leader_pose SHARED src/get_leader_pose.cpp)
target_include_directories(get_leader_pose PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(get_leader_pose PUBLIC cxx_std_17)
target_compile_definitions(get_leader_pose PRIVATE BT_PLUGIN_EXPORT)
set_target_properties(get_leader_pose PROPERTIES POSITION_INDEPENDENT_CODE ON)
ament_target_dependencies(get_leader_pose
  rclcpp
  behaviortree_cpp_v3
  std_msgs
  geometry_msgs)

install(TARGETS get_leader_pose compute_offset_goal
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
  std_msgs
  geometry_msgs)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
