add_executable(car_core main.cpp)
target_link_libraries(car_core PRIVATE car_common)

find_package(Threads REQUIRED)
target_link_libraries(car_core PRIVATE Threads::Threads)
