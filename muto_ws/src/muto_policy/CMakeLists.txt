cmake_minimum_required(VERSION 3.8)
project(muto_policy)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 1. Importer les dépendances ROS 2 indispensables
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)

# 2. Recherche manuelle et robuste d'ONNX Runtime (Installé via le Dockerfile dans /usr/local)
find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h 
  PATHS /usr/local/include /usr/local/include/onnxruntime /usr/include
)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime 
  PATHS /usr/local/lib /usr/lib /usr/lib/aarch64-linux-gnu
)

# 3. Déclaration de l'exécutable du nœud d'inférence (Une seule fois !)
add_executable(muto_policy_node src/muto_policy_node.cpp)

# 4. Spécifier les répertoires d'inclusion et la version C++
target_include_directories(muto_policy_node PRIVATE 
  ${ONNXRUNTIME_INCLUDE_DIR}
  include
)
target_compile_features(muto_policy_node PUBLIC cxx_std_17)

# 5. Lier les dépendances ROS 2 et la bibliothèque ONNX Runtime
ament_target_dependencies(muto_policy_node 
  rclcpp 
  sensor_msgs 
  std_msgs 
  geometry_msgs 
  ament_index_cpp
)
target_link_libraries(muto_policy_node ${ONNXRUNTIME_LIBRARY})

# 6. Installation de l'exécutable au bon endroit dans ROS 2
install(TARGETS muto_policy_node
  DESTINATION lib/${PROJECT_NAME}
)

# Configuration des tests (Optionnel, conservé propre)
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
