#pragma once

#include <QtWidgets/QDialog>
#include "ui_ctvd_setting.h"

class Setting : public QDialog
{
	Q_OBJECT;

public:
	Setting(QWidget* parent);
	~Setting();

	void setDefault();

	void openFileSavePath();

	void saveSettings();

private:
	Ui::SettingDialog ui;
};