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
    QAction *action_StartMonitorringClipboard = new QAction("Enable rewritting", trayMenu);
    QAction *action_StopMonitorringClipboard = new QAction("Disable rewritting", trayMenu);

    trayMenu->addAction(exitAction);
    trayMenu->addAction(action_StartMonitorringClipboard);
    trayMenu->addAction(action_StopMonitorringClipboard);

    QObject::connect(exitAction, &QAction::triggered, &a, &QApplication::quit);
    QObject::connect(action_StartMonitorringClipboard, &QAction::triggered, &w, [&w](){w.startClipboardMonitoring();});
    QObject::connect(action_StopMonitorringClipboard, &QAction::triggered, &w, [&w](){w.stopClipboardMonitoring();});

    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();
    return a.exec();
}
