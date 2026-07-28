#pragma once

#include <QtWidgets/QDialog>
#include "ui_about.h"

class About : public QDialog
{
	Q_OBJECT;

public:
	About(QWidget *parent);
	~About();

private:
	Ui::AboutDialog ui;

};