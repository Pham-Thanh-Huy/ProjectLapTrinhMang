#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSettings>
#include <QStringListModel>
#include <QMap>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void newMessage(QString msg);

private slots:
    void insertLog(const QString& log);

    void newConnection();
    void appendSocketConnection(QTcpSocket* socket);

    void onClientReadyRead();
    void onClientDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError error);

    QJsonObject getData(const QString& path);
    void sendResponse(QTcpSocket* socket, QByteArray data);
    void sendFile(QTcpSocket* client, QString filePath);

    void handleData(QTcpSocket* sender, QByteArray data);
    void processSignIn(QTcpSocket* sender, QByteArray data);
    void processSignUp(QTcpSocket* sender, QByteArray data);
    void processSignOut(QTcpSocket* sender, QByteArray data);
    void processGetData(QTcpSocket* sender, QByteArray data);
    void processDelete(QTcpSocket* sender, QByteArray data);
    void processAddFolder(QTcpSocket* sender, QByteArray data);
    void processRenameFolder(QTcpSocket* sender, QByteArray data);
    void processAddFile(QTcpSocket* sender, QByteArray data);
    void processRenameFile(QTcpSocket* sender, QByteArray data);
    void processDownloadFile(QTcpSocket* sender, QByteArray data);

private:
    Ui::MainWindow* ui;

    QSettings* accounts;
    QStringListModel* model;
    QTcpServer* server;
    QMap<QTcpSocket*, QPair<qint64, QString>> clients;
};

#endif // !MAINWINDOW_H
