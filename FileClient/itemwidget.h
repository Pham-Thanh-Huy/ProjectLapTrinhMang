#ifndef ITEMWIDGET_H
#define ITEMWIDGET_H

#include <QWidget>
#include <QJsonObject>

namespace Ui {
class ItemWidget;
}

class ItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit ItemWidget(QWidget* parent = nullptr);
    ~ItemWidget();

    void setData(const QJsonObject& data);
    QJsonObject getData() const;

private:
    Ui::ItemWidget* ui;

    QJsonObject data;
};

#endif // !ITEMWIDGET_H
