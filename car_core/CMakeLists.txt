add_executable(car_core main.cpp)
target_link_libraries(car_core PRIVATE car_common)

find_package(Threads REQUIRED)
target_link_libraries(car_core PRIVATE Threads::Threads)

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    target_include_directories(car_core PRIVATE ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(car_core PRIVATE ${SQLite3_LIBRARIES})
    target_compile_definitions(car_core PRIVATE HAS_SQLITE3)
endif()
