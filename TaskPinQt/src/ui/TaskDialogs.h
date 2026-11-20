#pragma once
#include <QString>
class Store;
class QWidget;
void showTaskDetails(Store *store, const QString &id, QWidget *parent);
void showCompletedTasks(Store *store, QWidget *parent);
