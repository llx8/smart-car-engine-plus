set(CMAKE_AUTOMOC ON)

add_executable(car_dashboard
    main.cpp
    MainWindow.cpp
    MainWindow.hpp
    AcPanel.cpp
    AcPanel.hpp
    SpeedGauge.cpp
    TachGauge.cpp
    ShmReader.cpp
    TempWidget.cpp
    TempWidget.hpp
    FaultLamp.cpp
    FaultLamp.hpp
    DoorPanel.cpp
    DoorPanel.hpp
    DtcListWidget.cpp
    DtcListWidget.hpp
)

target_link_libraries(car_dashboard PRIVATE
    car_common
    Qt5::Widgets
)
