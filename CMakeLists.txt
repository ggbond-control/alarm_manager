cmake_minimum_required(VERSION 3.8)
project(alarm_manager)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(alarm_aggregator_node src/alarm_aggregator_node.cpp)
ament_target_dependencies(alarm_aggregator_node
  rclcpp
  ament_index_cpp
  rcl_interfaces
  std_msgs
)

install(TARGETS alarm_aggregator_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY audio config launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME atmospheric_audio_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/atmospheric_audio_test.py
      $<TARGET_FILE:alarm_aggregator_node> ${CMAKE_CURRENT_SOURCE_DIR}/audio/atmospheric.wav)
  set_tests_properties(atmospheric_audio_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
