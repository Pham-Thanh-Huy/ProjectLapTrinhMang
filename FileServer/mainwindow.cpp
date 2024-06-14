#include "mainwindow.h"

#include "ui_mainwindow.h"

#include <QDebug>
#include <QMessageBox>
#include <QDir>

#include "../FileUtils/utils.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) , ui(new Ui::MainWindow) {
    ui->setupUi(this);

    setWindowFlags(windowFlags() | Qt::MSWindowsFixedSizeDialogHint);

    if (!QDir("data").exists()) {
        QDir().mkdir("data");
    }

    if (!QDir("trash").exists()) {
        QDir().mkdir("trash");
    }

    accounts = new QSettings("accounts.data", QSettings::IniFormat);

    model = new QStringListModel(this);

    ui->lvLogs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->lvLogs->setModel(model);

    server = new QTcpServer();
    if (server->listen(QHostAddress::Any, 2209)) {
        connect(this, &MainWindow::newMessage, this, &MainWindow::insertLog);
        connect(server, &QTcpServer::newConnection, this, &MainWindow::newConnection);
        ui->statusBar->showMessage("Server is listening on port 2209...");
    } else {
        QMessageBox::critical(this, "QTcpServer", QString("Unable to start the server: %1.").arg(server->errorString()));
        exit(EXIT_FAILURE);
    }

    connect(ui->pushButton, &QPushButton::clicked, this, [this]() {
        insertLog("-----------------------------------------------------------------");
    });
}

MainWindow::~MainWindow() {
    foreach (QTcpSocket* socket, clients.keys()) {
        socket->close();
        socket->deleteLater();
    }

    accounts->deleteLater();
    server->close();
    server->deleteLater();
    model->deleteLater();

    delete ui;
}

void MainWindow::insertLog(const QString& log) {
    if(model->insertRow(model->rowCount())) {
        QModelIndex index = model->index(model->rowCount() - 1, 0);
        model->setData(index, log);
        ui->lvLogs->setCurrentIndex(model->index(model->rowCount() - 1));
    } else {
        qDebug() << "Insert log fail: " << log;
    }
}

void MainWindow::newConnection() {
    while (server->hasPendingConnections()) {
        appendSocketConnection(server->nextPendingConnection());
    }
}

void MainWindow::appendSocketConnection(QTcpSocket* socket) {
    QPair<qint64, QString> pair;
    pair.first = socket->socketDescriptor();
    pair.second = QString();
    clients.insert(socket, pair);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onClientReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onClientDisconnected);
    connect(socket, &QAbstractSocket::errorOccurred, this, &MainWindow::onErrorOccurred);
    insertLog(QString("INFO: Client with sockd:%1 has just connected").arg(socket->socketDescriptor()));
}

void MainWindow::onClientReadyRead() {
    QTcpSocket* socket = reinterpret_cast<QTcpSocket*>(sender());

    QByteArray buffer;

    QDataStream socketStream(socket);
    socketStream.setVersion(QDataStream::Qt_5_15);

    socketStream.startTransaction();
    socketStream >> buffer;

    if(!socketStream.commitTransaction()) {
        QString message = QString("%1::Waiting for more data to come..").arg(socket->socketDescriptor());
        emit newMessage(message);
        return;
    }

    handleData(socket, buffer);
}

void MainWindow::onClientDisconnected() {
    QTcpSocket* socket = reinterpret_cast<QTcpSocket*>(sender());
    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator it = clients.find(socket);
    if (it != clients.end()) {
        insertLog(QString("INFO: Client with sockd:%1 has just disconnected").arg(it.value().first));
        clients.erase(it);
    }

    socket->deleteLater();
}

void MainWindow::onErrorOccurred(QAbstractSocket::SocketError error) {
    switch (error) {
        case QAbstractSocket::RemoteHostClosedError:
            break;

        case QAbstractSocket::HostNotFoundError:
            QMessageBox::information(this, "QTcpServer", "The host was not found. Please check the host name and port settings.");
            break;

        case QAbstractSocket::ConnectionRefusedError:
            QMessageBox::information(this, "QTcpServer", "The connection was refused by the peer. Make sure QTcpServer is running, and check that the host name and port settings are correct.");
            break;

        default:
            QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
            QMessageBox::information(this, "QTcpServer", QString("The following error occurred: %1.").arg(socket->errorString()));
            break;
    }
}

QJsonObject MainWindow::getData(const QString& path) {
    QString tmpPath = path;
    QJsonObject object;

    QFileInfo info(path);
    object.insert("name", info.fileName());
    object.insert("path", tmpPath.replace(QString("data") + QDir::separator(), ""));

    if (info.isDir()) {
        object.insert("type", "dir");
        QDir dir(path);
        QJsonArray children;
        foreach (const QFileInfo& file, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::DirsFirst | QDir::Name)) {
            children.push_back(getData(file.filePath().replace("/", QDir::separator()).replace("\\", QDir::separator())));
        }
        object.insert("children", children);
    } else {
        object.insert("type", "file");
        object.insert("size", info.size());
    }

    return object;
}

void MainWindow::sendResponse(QTcpSocket* socket, QByteArray data) {
    if(socket) {
        if(socket->isOpen()) {
            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_15);

            socketStream << data;
        } else {
            QMessageBox::critical(this,"QTcpServer","Socket doesn't seem to be opened");
        }
    } else {
        QMessageBox::critical(this,"QTcpServer","Not connected");
    }
}

void MainWindow::sendFile(QTcpSocket* client, QString filePath) {
    QByteArray typeSuccessArray = QByteArray::number(ResponseDownloadSuccess);
    typeSuccessArray.resize(8);
    QByteArray typeErrorArray = QByteArray::number(ResponseDownloadError);
    typeErrorArray.resize(8);

    if(client) {
        if(client->isOpen()) {
            QFile file(filePath);
            if(file.open(QIODevice::ReadOnly)){
                insertLog(QString("%1::sendFile: ").arg(client->socketDescriptor()) + "OK!");

                QFileInfo fileInfo(filePath);
                QString fileName(fileInfo.fileName());

                QDataStream socketStream(client);
                socketStream.setVersion(QDataStream::Qt_5_15);

                QByteArray header;
                header.prepend(QString("%1,%2").arg(fileName).arg(file.size()).toUtf8());
                header.resize(128);

                QByteArray byteArray = file.readAll();
                byteArray.prepend(header);

                byteArray.prepend(typeSuccessArray);

                socketStream << byteArray;
            } else {
                QString msg = "Couldn't open the file";
                insertLog(QString("%1::sendFile: ").arg(client->socketDescriptor()) + msg);

                QByteArray byteArray = msg.toUtf8();
                byteArray.prepend(typeErrorArray);
                sendResponse(client, byteArray);
                return;
            }
        } else {
            QMessageBox::critical(this,"QTcpServer","Socket doesn't seem to be opened");
        }
    } else {
        QMessageBox::critical(this,"QTcpServer","Not connected");
    }
}

void MainWindow::handleData(QTcpSocket* sender, QByteArray data) {
    int type = data.mid(0, 8).toInt();
    data = data.mid(8);

    switch (type) {
        case RequestNone:
            insertLog(QString("%1::RequestNone: ").arg(sender->socketDescriptor()) + QString::fromStdString(data.toStdString()));
            break;

        case RequestSignIn:
            processSignIn(sender, data);
            break;

        case RequestSignUp:
            processSignUp(sender, data);
            break;

        case RequestSignOut:
            processSignOut(sender, data);
            break;

        case RequestGetData:
            processGetData(sender, data);
            break;

        case RequestDelete:
            processDelete(sender, data);
            break;

        case RequestAddFolder:
            processAddFolder(sender, data);
            break;

        case RequestRenameFolder:
            processRenameFolder(sender, data);
            break;

        case RequestAddFile:
            processAddFile(sender, data);
            break;

        case RequestRenameFile:
            processRenameFile(sender, data);
            break;

        case RequestDownload:
            processDownloadFile(sender, data);
            break;

        default:
            break;
    }
}

void MainWindow::processSignIn(QTcpSocket* sender, QByteArray data) {
    QByteArray typeErrorArray = QByteArray::number(ResponseSignInError);
    typeErrorArray.resize(8);
    QByteArray typeSuccessArray = QByteArray::number(ResponseSignInSuccess);
    typeSuccessArray.resize(8);

    QString dataStr = data;
    QStringList list = dataStr.split(";");
    if (list.size() < 2 || list[0].isEmpty() || list[1].isEmpty()) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processSignIn: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    if (!accounts->allKeys().contains(list[0], Qt::CaseInsensitive)) {
        QString msg = list[0] + " doesn't exist";
        insertLog(QString("%1::processSignIn: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    if (QString::compare(list[1], accounts->value(list[0], QString()).toString()) != 0) {
        QString msg = list[0] + "The password is incorrect";
        insertLog(QString("%1::processSignIn: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QMapIterator<QTcpSocket*, QPair<qint64, QString>> iter(clients);
    while(iter.hasNext()) {
        iter.next();
        if (QString::compare(list[0], iter.value().second) == 0) {
            QString msg = list[0] + " already signed in";
            insertLog(QString("%1::processSignIn: ").arg(sender->socketDescriptor()) + msg);

            QByteArray byteArray = msg.toUtf8();
            byteArray.prepend(typeErrorArray);
            sendResponse(sender, byteArray);
            return;
        }
    }

    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator it = clients.find(sender);
    if (it != clients.end()) {
        it.value().second = list[0];
    }

    insertLog(QString("%1::processSignIn: ").arg(sender->socketDescriptor()) + "OK!");

    QString msg = "SignIn success";
    QByteArray byteArray = msg.toUtf8();
    byteArray.prepend(typeSuccessArray);
    sendResponse(sender, byteArray);
}

void MainWindow::processSignUp(QTcpSocket* sender, QByteArray data) {
    QByteArray typeErrorArray = QByteArray::number(ResponseSignUpError);
    typeErrorArray.resize(8);
    QByteArray typeSuccessArray = QByteArray::number(ResponseSignUpSuccess);
    typeSuccessArray.resize(8);

    QString dataStr = data;
    QStringList list = dataStr.split(";");
    if (list.size() < 2 || list[0].isEmpty() || list[1].isEmpty()) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processSignUp: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    if (accounts->allKeys().contains(list[0], Qt::CaseInsensitive)) {
        QString msg = list[0] + " already exist";
        insertLog(QString("%1::processSignUp: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    accounts->setValue(list[0], list[1]);

    QDir dir(QString("data") + QDir::separator() + list[0]);
    if (dir.exists()) {
        dir.removeRecursively();
    }

    QDir().mkdir(QString("data") + QDir::separator() + list[0]);

    insertLog(QString("%1::processSignUp: (%2, %3) ").arg(sender->socketDescriptor()).arg(list[0], list[1]) + "OK!");

    QString msg = "SignUp success";
    QByteArray byteArray = msg.toUtf8();
    byteArray.prepend(typeSuccessArray);
    sendResponse(sender, byteArray);
}

void MainWindow::processSignOut(QTcpSocket* sender, QByteArray data) {
    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator it = clients.find(sender);
    if (it == clients.end()/* || QString::compare(it.value().second, username, Qt::CaseInsensitive) != 0*/) {
        QString msg = "An error occurred";
        insertLog(QString("%1::processSignOut: ").arg(sender->socketDescriptor()) + msg);

        QByteArray typeErrorArray = QByteArray::number(ResponseSignOutError);
        typeErrorArray.resize(8);
        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    it.value().second = QString();

    insertLog(QString("%1::processSignOut: ").arg(sender->socketDescriptor()) + "OK!");

    QString msg = "SignOut success";
    QByteArray typeArray = QByteArray::number(ResponseSignOutSuccess);
    typeArray.resize(8);
    QByteArray byteArray = msg.toUtf8();
    byteArray.prepend(typeArray);
    sendResponse(sender, byteArray);
}

void MainWindow::processGetData(QTcpSocket* sender, QByteArray data) {
    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator iter = clients.find(sender);
    if (iter == clients.end()) {
        QString msg = "An error occurred";
        insertLog(QString("%1::processGetData: ").arg(sender->socketDescriptor()) + msg);

        QByteArray typeArray = QByteArray::number(ResponseGetDataError);
        typeArray.resize(8);
        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeArray);
        sendResponse(sender, byteArray);
        return;
    }

    if (iter.value().second.isEmpty()) {
        QString msg = "Finish signing to continue";
        insertLog(QString("%1::processGetData: ").arg(sender->socketDescriptor()) + "client not authenticated");

        QByteArray typeArray = QByteArray::number(ResponseGetDataError);
        typeArray.resize(8);
        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeArray);
        sendResponse(sender, byteArray);
        return;
    }

    insertLog(QString("%1::processGetData: ").arg(sender->socketDescriptor()) + " OK!");

    QJsonDocument jsonDoc;
    jsonDoc.setObject(getData(QString("data") + QDir::separator() + iter.value().second));
    QString responseData = jsonDoc.toJson(QJsonDocument::Compact);

    QByteArray typeArray = QByteArray::number(ResponseGetDataSuccess);
    typeArray.resize(8);
    QByteArray byteArray = responseData.toUtf8();
    byteArray.prepend(typeArray);
    sendResponse(sender, byteArray);
}

void MainWindow::processDelete(QTcpSocket* sender, QByteArray data) {
    QByteArray typeErrorArray = QByteArray::number(ResponseDeleteError);
    typeErrorArray.resize(8);
    QByteArray typeSuccessArray = QByteArray::number(ResponseDeleteSuccess);
    typeSuccessArray.resize(8);

    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator iter = clients.find(sender);
    if (iter == clients.end()) {
        QString msg = "An error occurred";
        insertLog(QString("%1::processDelete: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (jsonDoc.isObject() == false) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processDelete: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QJsonObject object = jsonDoc.object();
    QString path = object.value("path").toString();
    if (!path.startsWith(iter.value().second)) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processDelete: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QFileInfo info(QString("data") + QDir::separator() + path);
    if (info.exists()) {
        if (info.isFile()) {
            if (!QFile(info.filePath()).remove()) {
                QString msg = "Cannot delete file";
                insertLog(QString("%1::processDelete: ").arg(sender->socketDescriptor()) + msg);

                QByteArray byteArray = msg.toUtf8();
                byteArray.prepend(typeErrorArray);
                sendResponse(sender, byteArray);
                return;
            }
        } else if (info.isDir()) {
            if (!QDir(info.filePath()).removeRecursively()) {
                QString msg = "Cannot delete folder";
                insertLog(QString("%1::processDelete: ").arg(sender->socketDescriptor()) + msg);

                QByteArray byteArray = msg.toUtf8();
                byteArray.prepend(typeErrorArray);
                sendResponse(sender, byteArray);
                return;
            }
        }
    }

    insertLog(QString("%1::processDelete: ").arg(sender->socketDescriptor()) + "Delete success");

    jsonDoc.setObject(getData(QString("data") + QDir::separator() + iter.value().second));
    QString responseData = jsonDoc.toJson(QJsonDocument::Compact);

    QByteArray byteArray = responseData.toUtf8();
    byteArray.prepend(typeSuccessArray);
    sendResponse(sender, byteArray);
}

void MainWindow::processAddFolder(QTcpSocket* sender, QByteArray data) {
    QByteArray typeErrorArray = QByteArray::number(ResponseAddFolderError);
    typeErrorArray.resize(8);
    QByteArray typeSuccessArray = QByteArray::number(ResponseAddFolderSuccess);
    typeSuccessArray.resize(8);

    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator iter = clients.find(sender);
    if (iter == clients.end()) {
        QString msg = "An error occurred";
        insertLog(QString("%1::processAddFolder: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QString str = data;
    QStringList list = str.split(";");
    if (list.size() < 2 || list[0].isEmpty() || list[1].isEmpty() || !list[0].startsWith(iter.value().second)) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processAddFolder: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QDir dir(QString("data") + QDir::separator() + list[0] + QDir::separator() + list[1]);
    if (dir.exists()) {
        QString msg = "Folder already exists";
        insertLog(QString("%1::processAddFolder: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    if (!QDir().mkdir(QString("data") + QDir::separator() + list[0] + QDir::separator() + list[1])) {
        QString msg = "Cannot create folder";
        insertLog(QString("%1::processAddFolder: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    insertLog(QString("%1::processAddFolder: ").arg(sender->socketDescriptor()) + "Create folder success");

    QJsonDocument jsonDoc;
    jsonDoc.setObject(getData(QString("data") + QDir::separator() + iter.value().second));
    QString responseData = jsonDoc.toJson(QJsonDocument::Compact);

    QByteArray byteArray = responseData.toUtf8();
    byteArray.prepend(typeSuccessArray);
    sendResponse(sender, byteArray);
}

void MainWindow::processRenameFolder(QTcpSocket* sender, QByteArray data) {

}

void MainWindow::processAddFile(QTcpSocket* sender, QByteArray data) {
    QByteArray typeSuccessArray = QByteArray::number(ResponseAddFileSuccess);
    typeSuccessArray.resize(8);
    QByteArray typeErrorArray = QByteArray::number(ResponseAddFileError);
    typeErrorArray.resize(8);

    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator iter = clients.find(sender);
    if (iter == clients.end()) {
        QString msg = "An error occurred";
        insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QString header = data.mid(0, 256);
    data = data.mid(256);
    QStringList list = header.split(";");
    if (list.size() < 2 || list[0].isEmpty() || list[1].isEmpty() || !list[0].startsWith(iter.value().second)) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QDir dir(QString("data") + QDir::separator() + list[0]);
    if (!dir.exists()) {
        QString msg = "Folder not exists";
        insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QFileInfo info(QString("data") + QDir::separator() + list[0] + QDir::separator() + list[1]);
    if (info.exists()) {
        if (info.isDir()) {
            QString msg = "Invalid filename";
            insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + msg);

            QByteArray byteArray = msg.toUtf8();
            byteArray.prepend(typeErrorArray);
            sendResponse(sender, byteArray);
            return;
        } else {
            if (!QDir().rename(info.filePath(), QString("trash") + QDir::separator() + info.fileName())) {
                QString msg = "Cannot write file";
                insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + msg);

                QByteArray byteArray = msg.toUtf8();
                byteArray.prepend(typeErrorArray);
                sendResponse(sender, byteArray);
                return;
            }
        }
    }

    QFile file = info.filePath();
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);

        insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + "Add file success");

        QJsonDocument jsonDoc;
        jsonDoc.setObject(getData(QString("data") + QDir::separator() + iter.value().second));
        QString responseData = jsonDoc.toJson(QJsonDocument::Compact);

        QByteArray byteArray = responseData.toUtf8();
        byteArray.prepend(typeSuccessArray);
        sendResponse(sender, byteArray);

        QFile(QString("trash") + QDir::separator() + info.fileName()).remove();
    } else {
        QDir().rename(QString("trash") + QDir::separator() + info.fileName(), info.filePath());

        QString msg = "An error occurred while trying to write the file";
        insertLog(QString("%1::processAddFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }
}

void MainWindow::processRenameFile(QTcpSocket* sender, QByteArray data) {

}

void MainWindow::processDownloadFile(QTcpSocket* sender, QByteArray data) {
    QByteArray typeSuccessArray = QByteArray::number(ResponseDownloadSuccess);
    typeSuccessArray.resize(8);
    QByteArray typeErrorArray = QByteArray::number(ResponseDownloadError);
    typeErrorArray.resize(8);

    QMap<QTcpSocket*, QPair<qint64, QString>>::iterator iter = clients.find(sender);
    if (iter == clients.end()) {
        QString msg = "An error occurred";
        insertLog(QString("%1::processDownloadFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    if (jsonDoc.isObject() == false) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processDownloadFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QJsonObject object = jsonDoc.object();
    QString path = object.value("path").toString();
    if (!path.startsWith(iter.value().second)) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processDownloadFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    QFileInfo info(QString("data") + QDir::separator() + path);
    if (!info.exists() || !info.isFile()) {
        QString msg = "Invalid data";
        insertLog(QString("%1::processDownloadFile: ").arg(sender->socketDescriptor()) + msg);

        QByteArray byteArray = msg.toUtf8();
        byteArray.prepend(typeErrorArray);
        sendResponse(sender, byteArray);
        return;
    }

    sendFile(sender, info.filePath());
}
