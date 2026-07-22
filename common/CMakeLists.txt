add_library(car_common STATIC
    CanFrame.cpp
    ConfigManager.cpp
    DtcEngine.cpp
    ModuleServer.cpp
)

target_include_directories(car_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
