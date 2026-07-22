#include "MainWindow.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    int shmid   = (argc >= 2) ? std::atoi(argv[1]) : -1;
    int evfd    = (argc >= 3) ? std::atoi(argv[2]) : -1;

    MainWindow w(shmid, evfd);
    w.show();
    return app.exec();
}