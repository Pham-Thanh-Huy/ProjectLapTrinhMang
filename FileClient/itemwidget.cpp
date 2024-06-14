#include "itemwidget.h"

#include "ui_itemwidget.h"

#include <QPixmap>
#include <QJsonValue>
#include <QJsonArray>

ItemWidget::ItemWidget(QWidget* parent) : QWidget(parent), ui(new Ui::ItemWidget) {
    ui->setupUi(this);
}

ItemWidget::~ItemWidget() {
    delete ui;
}

void ItemWidget::setData(const QJsonObject& data) {
    this->data = data;

    QJsonValue name = data.value("name");
    ui->lbName->setText(name.toString());

    QJsonValue type = data.value("type");

    QPixmap pic(":images/folder.png");
    if (type == "file") {
        pic = QPixmap(":images/document.png");
    } else {
        QJsonArray children = data.value("children").toArray();
        if (children.isEmpty()) {
            pic= QPixmap(":images/folder_empty.png");
        }
    }

    ui->icon->setPixmap(pic);
}

QJsonObject ItemWidget::getData() const {
    return data;
}
