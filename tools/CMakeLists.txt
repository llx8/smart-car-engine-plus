find_package(Threads REQUIRED)

add_executable(can_sim can_sim.cpp)
target_link_libraries(can_sim PRIVATE car_common Threads::Threads)

add_executable(car_ctl car_ctl.cpp)
target_link_libraries(car_ctl PRIVATE car_common)

find_package(CURL QUIET)
if(CURL_FOUND)
    add_executable(car_ai car_ai.cpp)
    target_link_libraries(car_ai PRIVATE
        car_common
        ${CURL_LIBRARIES}
        nlohmann_json::nlohmann_json
    )
    target_include_directories(car_ai PRIVATE ${CURL_INCLUDE_DIRS})
endif()
