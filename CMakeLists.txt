cmake_minimum_required(VERSION 3.8)
project(alarm_manager)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(monitor_interfaces REQUIRED)

add_executable(alarm_aggregator_node src/alarm_aggregator_node.cpp)
ament_target_dependencies(alarm_aggregator_node
  rclcpp
  rclcpp_action
  ament_index_cpp
  std_msgs
  diagnostic_msgs
  monitor_interfaces
)

install(TARGETS alarm_aggregator_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY audio config launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
