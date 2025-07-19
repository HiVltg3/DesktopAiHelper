#include "mainwindow.h"

#include <QApplication>
#include <QSystemTrayIcon>
#include <QObject>
#include <QAction>
#include <QMenu>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    QSystemTrayIcon* trayIcon=new QSystemTrayIcon(QIcon(":/icons/icons/mainIcon.png"));
    QMenu *trayMenu = new QMenu();
    QAction *exitAction = new QAction("Exit", trayMenu);
    trayMenu->addAction(exitAction);
    QObject::connect(exitAction, &QAction::triggered, &a, &QApplication::quit);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();
    return a.exec();
}
