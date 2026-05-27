cmake_minimum_required(VERSION 3.8)
project(muto_bringup)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(hardware_interface REQUIRED)
find_package(pluginlib REQUIRED)
find_package(sensor_msgs REQUIRED)

# -- muto_link (low-level communication library) --
add_library(muto_link SHARED
  src/muto_link/transport.cpp
  src/muto_link/protocol.cpp
  src/muto_link/driver.cpp
  src/muto_link/sensor.cpp
  src/muto_link/c_api.cpp
)

target_include_directories(muto_link PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_definitions(muto_link PRIVATE MUTO_LINK_CPP_EXPORTS)
target_compile_features(muto_link PUBLIC cxx_std_17)

# -- muto_hardware (ros2_control plugin) --
add_library(muto_hardware SHARED
  src/muto_hardware.cpp
)

target_include_directories(muto_hardware PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(muto_hardware muto_link)

ament_target_dependencies(muto_hardware
  rclcpp
  rclcpp_lifecycle
  hardware_interface
  pluginlib
  sensor_msgs
)

target_compile_features(muto_hardware PUBLIC cxx_std_17)

pluginlib_export_plugin_description_file(hardware_interface muto_hardware_plugin.xml)

# -- Install --
install(TARGETS muto_link muto_hardware
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
install(FILES muto_hardware_plugin.xml DESTINATION share/${PROJECT_NAME})

# AJOUT CRITIQUE : Permet à "ros2 launch" de localiser gazebo.launch.py et bringup.launch.py
install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_libraries(muto_link muto_hardware)
ament_export_targets(export_${PROJECT_NAME})

ament_package()
