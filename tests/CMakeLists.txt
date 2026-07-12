add_executable(test_can_frame test_can_frame.cpp)
target_link_libraries(test_can_frame PRIVATE car_common GTest::gtest_main)
add_test(NAME test_can_frame COMMAND test_can_frame)

add_executable(test_car_msg test_car_msg.cpp)
target_link_libraries(test_car_msg PRIVATE car_common GTest::gtest_main)
add_test(NAME test_car_msg COMMAND test_car_msg)

add_executable(test_shm_layout test_shm_layout.cpp)
target_link_libraries(test_shm_layout PRIVATE car_common GTest::gtest_main)
add_test(NAME test_shm_layout COMMAND test_shm_layout)

add_executable(test_dtc_engine test_dtc_engine.cpp)
target_link_libraries(test_dtc_engine PRIVATE car_common GTest::gtest_main)
add_test(NAME test_dtc_engine COMMAND test_dtc_engine)