cmake_minimum_required(VERSION 3.8)
project(muto_policy)

# Options de compilation
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 1. Dépendances ROS 2
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)

# 2. ONNX Runtime (installé manuellement dans /usr/local)
set(ONNXRUNTIME_ROOT "/usr/local" CACHE PATH "ONNX Runtime install root")

find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h
  PATHS ${ONNXRUNTIME_ROOT}/include ${ONNXRUNTIME_ROOT}/include/onnxruntime /usr/include
  REQUIRED
)

find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
  PATHS ${ONNXRUNTIME_ROOT}/lib /usr/lib /usr/lib/aarch64-linux-gnu
  REQUIRED
)

# 3. Déclaration de l'exécutable
add_executable(muto_policy_node src/muto_policy_node.cpp)

# 4. Répertoires d'inclusion et version C++
target_include_directories(muto_policy_node PRIVATE
  ${ONNXRUNTIME_INCLUDE_DIR}
  include
)
target_compile_features(muto_policy_node PUBLIC cxx_std_17)

# 5. Lier les dépendances ROS 2 et ONNX Runtime
ament_target_dependencies(muto_policy_node
  rclcpp
  sensor_msgs
  std_msgs
  geometry_msgs
  ament_index_cpp
)
target_link_libraries(muto_policy_node ${ONNXRUNTIME_LIBRARY})

# 6. Installation
install(TARGETS muto_policy_node
  DESTINATION lib/${PROJECT_NAME}
)

# 7. Tests optionnels
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
