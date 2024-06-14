#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QHostAddress>
#include <QStringListModel>
#include <QMap>
#include <QPair>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "itemwidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

signals:
    void newMessage(QString);

private slots:
    void displayMessage(const QString& msg);
    void displayError(const QString& msg);
    void updateListWidget(const QJsonObject& data);

    void onReadyRead();
    void onSocketDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError error);

    void sendGetData();
    void sendDelete(QJsonObject object);
    void sendDownload(QJsonObject object);
    void sendFile();

    void handleData(QByteArray data);
    void processGetDataSuccess(QByteArray data);
    void processUpdateData(QByteArray data);
    void processDownloadFile(QByteArray data);

private:
    Ui::MainWindow* ui;

    QStringListModel* model;
    QTcpSocket* socket;
    QList<ItemWidget*> items;
    QJsonObject jsonData, current;
    QString currentUser;
};

#endif // !MAINWINDOW_H
