set(CMAKE_AUTOMOC ON)

add_executable(car_dashboard
    main.cpp
    SpeedGauge.cpp
    TachGauge.cpp
    TempWidget.cpp
    FaultLamp.cpp
    DoorPanel.cpp
    ShmReader.cpp
)

target_link_libraries(car_dashboard PRIVATE
    car_common
    Qt5::Widgets
)
