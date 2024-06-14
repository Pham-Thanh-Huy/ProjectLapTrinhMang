#include "mainwindow.h"

#include "ui_mainwindow.h"

#include <QDebug>
#include <QDir>
#include <QQueue>
#include <QMessageBox>
#include <QInputDialog>
#include <QListWidgetItem>
#include <QJsonValue>
#include <QFileDialog>
#include <QStandardPaths>

#include "itemwidget.h"

#include "../FileUtils/utils.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) , ui(new Ui::MainWindow) {
    ui->setupUi(this);

    setWindowFlags(windowFlags() | Qt::MSWindowsFixedSizeDialogHint);

    model = new QStringListModel(this);

    // ui->lvLogs->setModel(model);

    socket = new QTcpSocket(this);

    connect(this, &MainWindow::newMessage, this, &MainWindow::displayMessage);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &MainWindow::onSocketDisconnected);
    connect(socket, &QAbstractSocket::errorOccurred, this, &MainWindow::onErrorOccurred);

    socket->connectToHost(QHostAddress::LocalHost, 2209);
    if (socket->waitForConnected()) {
        qDebug() << "Connected to Server";
    } else {
        QMessageBox::critical(this, "QTcpClient", QString("The following error occurred: %1.").arg(socket->errorString()));
        exit(EXIT_FAILURE);
    }

    connect(ui->btnSignIn, &QPushButton::clicked, this, [this]() {
        if(socket) {
            if(socket->isOpen()) {
                QString username = ui->edtUsername->text();
                QString password = ui->edtPassword->text();
                if (username.isEmpty() || password.isEmpty()) {
                    QMessageBox::warning(this, "Warning", "Please fill all required fields");
                    return;
                }

                QDataStream socketStream(socket);
                socketStream.setVersion(QDataStream::Qt_5_15);

                Request type = Request::RequestSignIn;
                QByteArray typeArray = QByteArray::number(type);
                typeArray.resize(8);

                QString str = username + ";" + password;
                QByteArray byteArray = str.toUtf8();
                byteArray.prepend(typeArray);

                socketStream << byteArray;
            } else {
                QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
            }
        } else {
            QMessageBox::critical(this, "QTcpClient", "Not connected");
        }
    });

    connect(ui->btnSignUp, &QPushButton::clicked, this, [this]() {
        if(socket) {
            if(socket->isOpen()) {
                QString username = ui->edtUsername->text();
                QString password = ui->edtPassword->text();
                if (username.isEmpty() || password.isEmpty()) {
                    QMessageBox::warning(this, "Warning", "Please fill all required fields");
                    return;
                }

                QDataStream socketStream(socket);
                socketStream.setVersion(QDataStream::Qt_5_15);

                Request type = Request::RequestSignUp;
                QByteArray typeArray = QByteArray::number(type);
                typeArray.resize(8);

                QString str = username + ";" + password;
                QByteArray byteArray = str.toUtf8();
                byteArray.prepend(typeArray);

                socketStream << byteArray;
            } else {
                QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
            }
        } else {
            QMessageBox::critical(this, "QTcpClient", "Not connected");
        }
    });

    connect(ui->btnSignOut, &QPushButton::clicked, this, [this]() {
        if(socket) {
            if(socket->isOpen()) {
                QString str = currentUser;

                QDataStream socketStream(socket);
                socketStream.setVersion(QDataStream::Qt_5_15);

                Request type = Request::RequestSignOut;
                QByteArray typeArray = QByteArray::number(type);
                typeArray.resize(8);

                QByteArray byteArray = str.toUtf8();
                byteArray.prepend(typeArray);

                socketStream << byteArray;
            } else {
                QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
            }
        } else {
            QMessageBox::critical(this, "QTcpClient", "Not connected");
        }
    });

    connect(ui->btnRefresh, &QPushButton::clicked, this, [this]() {
        sendGetData();
    });

    connect(ui->btnDelete, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* item = ui->listWidget->currentItem();
        if (item) {
            for (int i = 0; i < ui->listWidget->count(); i++) {
                if (ui->listWidget->item(i) == item) {
                    sendDelete(items[i]->getData());
                    break;
                }
            }
        } else {
            displayMessage("Delete: Please select an item");
            QMessageBox::information(this, "Information", "Please select an item");
        }
    });

    connect(ui->btnDownload, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* item = ui->listWidget->currentItem();
        if (item) {
            for (int i = 0; i < ui->listWidget->count(); i++) {
                if (ui->listWidget->item(i) == item) {
                    sendDownload(items[i]->getData());
                    break;
                }
            }
        } else {
            displayMessage("Download: Please select a file");
            QMessageBox::information(this, "Information", "Please select a file");
        }
    });

    connect(ui->btnUpload, &QPushButton::clicked, this, &MainWindow::sendFile);

    connect(ui->btnCreateFolder, &QPushButton::clicked, this, [this]() {
        bool ok;
        QString name;
        do {
            name = QInputDialog::getText(0, "Create folder", "Folder name:", QLineEdit::Normal, "", &ok);
            if (!ok) {
                return;
            }
        } while (ok && name.isEmpty());

        if(socket) {
            if(socket->isOpen()) {
                QString str = this->current.value("path").toString() + ";" + name;

                QDataStream socketStream(socket);
                socketStream.setVersion(QDataStream::Qt_5_15);

                Request type = Request::RequestAddFolder;
                QByteArray typeArray = QByteArray::number(type);
                typeArray.resize(8);

                QByteArray byteArray = str.toUtf8();
                byteArray.prepend(typeArray);

                socketStream << byteArray;
            } else {
                QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
            }
        } else {
            QMessageBox::critical(this, "QTcpClient", "Not connected");
        }
    });

    connect(ui->btnBack, &QPushButton::clicked, this, [this]() {
        QString path = current.value("path").toString();
        QString parentPath;
        if (path.lastIndexOf(QDir::separator()) != -1) {
            parentPath = path.mid(0, path.lastIndexOf(QDir::separator()));
        } else if (path.lastIndexOf("/") != -1) {
            parentPath = path.mid(0, path.lastIndexOf("/"));
        } else if (path.lastIndexOf("\\") != -1) {
            parentPath = path.mid(0, path.lastIndexOf("\\"));
        } else {
            return;
        }

        QQueue<QJsonObject> queue;
        queue.enqueue(jsonData);
        while (!queue.isEmpty()) {
            QJsonObject object = queue.dequeue();
            if (object.value("path").toString() == parentPath) {
                updateListWidget(object);
                break;
            }

            QJsonArray children = object.value("children").toArray();
            for (int i = 0; i < children.count(); i++) {
                QJsonObject child = children.at(i).toObject();
                if (child.value("type").toString() == "dir") {
                    queue.enqueue(child);
                }
            }
        }

        displayMessage("Back to " + parentPath);
    });

    connect(ui->listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        for (int i = 0; i < ui->listWidget->count(); i++) {
            if (ui->listWidget->item(i) == item) {
                displayMessage(QString("Double click on ") + items[i]->getData().value("path").toString());
                if (items[i]->getData().value("type").toString() == "dir") {
                    updateListWidget(items[i]->getData());
                }
                break;
            }
        }
    });
}

MainWindow::~MainWindow() {
    if (socket->isOpen()) {
        socket->close();
        socket->deleteLater();
    }

    model->deleteLater();

    delete ui;
}

void MainWindow::displayMessage(const QString& msg) {
    if(model->insertRow(model->rowCount())) {
        QModelIndex index = model->index(model->rowCount() - 1, 0);
        model->setData(index, msg);
        // ui->lvLogs->setCurrentIndex(model->index(model->rowCount() - 1));
    } else {
        qDebug() << "Insert log fail: " << msg;
    }
}

void MainWindow::displayError(const QString& msg) {
    QMessageBox::critical(this, "Error", msg);
}

void MainWindow::updateListWidget(const QJsonObject& data) {
    current = data;

    items.clear();

    ui->listWidget->clear();

    QString path = data.value("path").toString().replace("/", " > ").replace("\\", " > ");
    ui->lbPath->setText(QString("> ") + path);
    ui->btnBack->setEnabled(path.contains(" > "));

    QJsonArray children = data.value("children").toArray();
    for (int i = 0; i < children.count(); i++) {
        auto widget = new ItemWidget(this);
        widget->setData(children.at(i).toObject());

        items.append(widget);

        auto item = new QListWidgetItem();
        item->setSizeHint(QSize(480, 48));

        ui->listWidget->addItem(item);
        ui->listWidget->setItemWidget(item, widget);
    }
}

void MainWindow::onReadyRead() {
    QByteArray buffer;

    QDataStream socketStream(socket);
    socketStream.setVersion(QDataStream::Qt_5_15);

    socketStream.startTransaction();
    socketStream >> buffer;

    if(!socketStream.commitTransaction()) {
        QString message = QString("%1 :: Waiting for more data to come..").arg(socket->socketDescriptor());
        emit newMessage(message);
        return;
    }

    handleData(buffer);
}

void MainWindow::onSocketDisconnected() {
    socket->deleteLater();
    socket=nullptr;

    qDebug() << "Disconnected";
}

void MainWindow::onErrorOccurred(QAbstractSocket::SocketError error) {
    switch (error) {
        case QAbstractSocket::RemoteHostClosedError:
            break;

        case QAbstractSocket::HostNotFoundError:
            QMessageBox::information(this, "QTcpClient", "The host was not found. Please check the host name and port settings.");
            break;

        case QAbstractSocket::ConnectionRefusedError:
            QMessageBox::information(this, "QTcpClient", "The connection was refused by the peer. Make sure QTCPServer is running, and check that the host name and port settings are correct.");
            break;

        default:
            QMessageBox::information(this, "QTcpClient", QString("The following error occurred: %1.").arg(socket->errorString()));
            break;
    }
}

void MainWindow::sendGetData() {
    if(socket) {
        if(socket->isOpen()) {
            QString str = currentUser;

            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_15);

            Request type = Request::RequestGetData;
            QByteArray typeArray = QByteArray::number(type);
            typeArray.resize(8);

            QByteArray byteArray = str.toUtf8();
            byteArray.prepend(typeArray);

            socketStream << byteArray;
        } else {
            QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
        }
    } else {
        QMessageBox::critical(this, "QTcpClient", "Not connected");
    }
}

void MainWindow::sendDelete(QJsonObject object) {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Delete", "Are you sure to delete this item?", QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    QJsonDocument jsonDoc;
    jsonDoc.setObject(object);
    QString data = jsonDoc.toJson(QJsonDocument::Compact);
    displayMessage(QString("Delete ") + data);

    if(socket) {
        if(socket->isOpen()) {
            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_15);

            Request type = Request::RequestDelete;
            QByteArray typeArray = QByteArray::number(type);
            typeArray.resize(8);

            QByteArray byteArray = data.toUtf8();
            byteArray.prepend(typeArray);

            socketStream << byteArray;
        } else {
            QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
        }
    } else {
        QMessageBox::critical(this, "QTcpClient", "Not connected");
    }
}

void MainWindow::sendDownload(QJsonObject object) {
    if (object.value("type").toString() != "file") {
        displayMessage("Download: Please select a file");
        QMessageBox::information(this, "Information", "Please select a file");
        return;
    }

    QJsonDocument jsonDoc;
    jsonDoc.setObject(object);
    QString data = jsonDoc.toJson(QJsonDocument::Compact);
    displayMessage(QString("Download ") + data);

    if(socket) {
        if(socket->isOpen()) {
            QDataStream socketStream(socket);
            socketStream.setVersion(QDataStream::Qt_5_15);

            Request type = Request::RequestDownload;
            QByteArray typeArray = QByteArray::number(type);
            typeArray.resize(8);

            QByteArray byteArray = data.toUtf8();
            byteArray.prepend(typeArray);

            socketStream << byteArray;
        } else {
            QMessageBox::critical(this, "QTcpClient", "Socket doesn't seem to be opened");
        }
    } else {
        QMessageBox::critical(this, "QTcpClient", "Not connected");
    }
}

void MainWindow::sendFile() {
    QString filename =  QFileDialog::getOpenFileName(this, "Select File", QDir::currentPath(), "All files (*.*)");
    if (filename.isNull() || filename.isEmpty()) {
        displayMessage(QString("sendFile: Cancel"));
        return;
    }

    QFileInfo info(filename);
    if (!info.exists()) {
        displayMessage(QString("sendFile: file not exists"));
        return;
    }

    QFile file(info.filePath());
    if(file.open(QIODevice::ReadOnly)){
        QString fileName(info.fileName());

        QDataStream socketStream(socket);
        socketStream.setVersion(QDataStream::Qt_5_15);

        Request type = Request::RequestAddFile;
        QByteArray typeArray = QByteArray::number(type);
        typeArray.resize(8);

        QByteArray header;
        header.prepend(QString("%1;%2").arg(current.value("path").toString(), fileName).toUtf8());
        header.resize(256);

        QByteArray byteArray = file.readAll();
        byteArray.prepend(header);
        byteArray.prepend(typeArray);

        socketStream << byteArray;
    } else {
        QMessageBox::critical(this, "File Client", "File is not readable!");
    }
}

void MainWindow::handleData(QByteArray data) {
    int type = data.mid(0, 8).toInt();
    data = data.mid(8);

    switch (type) {
        case ResponseNone:
            displayMessage(QString("ResponseNone: ") + QString::fromStdString(data.toStdString()));
            break;

        case ResponseSignInSuccess:
            displayMessage(QString("ResponseSignInSuccess: ") + QString::fromStdString(data.toStdString()));
            currentUser = ui->edtUsername->text();
            ui->edtPassword->setText("");
            ui->stackedWidget->setCurrentIndex(1);
            sendGetData();
            break;

        case ResponseSignInError:
            displayMessage(QString("ResponseSignInError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseSignUpSuccess:
            displayMessage(QString("ResponseSignUpSuccess: ") + QString::fromStdString(data.toStdString()));
            QMessageBox::information(this, "Information", "Your sign up was successful");
            break;

        case ResponseSignUpError:
            displayMessage(QString("ResponseSignUpError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseSignOutSuccess:
            displayMessage(QString("ResponseSignOutSuccess: ") + QString::fromStdString(data.toStdString()));
            currentUser = QString();
            ui->stackedWidget->setCurrentIndex(0);
            break;

        case ResponseSignOutError:
            displayMessage(QString("ResponseSignOutError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseGetDataSuccess:
            displayMessage(QString("ResponseGetDataSuccess: ") + QString::fromStdString(data.toStdString()));
            processGetDataSuccess(data);
            break;

        case ResponseGetDataError:
            displayMessage(QString("ResponseGetDataError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseDeleteSuccess:
            displayMessage(QString("ResponseDeleteSuccess: ") + QString::fromStdString(data.toStdString()));
            processUpdateData(data);
            break;

        case ResponseDeleteError:
            displayMessage(QString("ResponseDeleteError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseAddFolderSuccess:
            displayMessage(QString("ResponseAddFolderSuccess: ") + QString::fromStdString(data.toStdString()));
            processUpdateData(data);
            break;

        case ResponseAddFolderError:
            displayMessage(QString("ResponseAddFolderError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseAddFileSuccess:
            displayMessage(QString("ResponseAddFolderSuccess: ") + QString::fromStdString(data.toStdString()));
            processUpdateData(data);
            break;

        case ResponseAddFileError:
            displayMessage(QString("ResponseAddFolderError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        case ResponseDownloadSuccess:
            displayMessage(QString("ResponseDownloadSuccess: OK"));
            processDownloadFile(data);
            break;

        case ResponseDownloadError:
            displayMessage(QString("ResponseDownloadError: ") + QString::fromStdString(data.toStdString()));
            displayError(QString::fromStdString(data.toStdString()));
            break;

        default:
            break;
    }
}

void MainWindow::processGetDataSuccess(QByteArray data) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    this->jsonData = jsonDoc.object();
    updateListWidget(jsonData);
}

void MainWindow::processUpdateData(QByteArray data) {
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
    this->jsonData = jsonDoc.object();

    QQueue<QJsonObject> queue;
    queue.enqueue(jsonData);
    while (!queue.isEmpty()) {
        QJsonObject object = queue.dequeue();
        if (object.value("path").toString() == current.value("path").toString()) {
            updateListWidget(object);
            break;
        }

        QJsonArray children = object.value("children").toArray();
        for (int i = 0; i < children.count(); i++) {
            QJsonObject child = children.at(i).toObject();
            if (child.value("type").toString() == "dir") {
                queue.enqueue(child);
            }
        }
    }
}

void MainWindow::processDownloadFile(QByteArray data) {
    QString header = data.mid(0, 128);
    data = data.mid(128);

    QStringList list = header.split(",");
    if (list.size() < 2) {
        displayMessage("processDownloadFile: Invalid data");
        QMessageBox::warning(this, "Download", "Invalid data");
        return;
    }

    QString filename = list[0];
    QString size = list[1];

    displayMessage("Download file " + filename);

    QString filePath = QFileDialog::getSaveFileName(this, tr("Save File"), QDir::currentPath() + QDir::separator() + filename);
    displayMessage("Download save on " + filePath);
    if (filePath.isEmpty()) {
        QMessageBox::information(this,"Download", QString("File %1 discarded.").arg(filename));
        return;
    }

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        QString message = QString("Download file successfully stored on disk under the path %2").arg(QString(filePath));
        emit newMessage(message);
    } else {
        QMessageBox::critical(this,"Download", "An error occurred while trying to write the file.");
    }
}
