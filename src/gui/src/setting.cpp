#include "../include/setting.h"
#include "config.h"

#include <QFileDialog>
#include <QDir>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFrame>
#include <QPushButton>
#include <QStyle>

Setting::Setting(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	auto* saveButton = ui.buttonBox->button(QDialogButtonBox::Ok);
	auto* cancelButton = ui.buttonBox->button(QDialogButtonBox::Cancel);
	saveButton->setText(QStringLiteral("保存"));
	saveButton->setProperty("buttonRole", "primary");
	saveButton->setFixedSize(88, 36);
	saveButton->style()->unpolish(saveButton);
	saveButton->style()->polish(saveButton);
	cancelButton->setText(QStringLiteral("取消"));
	cancelButton->setFixedSize(88, 36);
	ui.pushButton_open->setFixedSize(80, 32);
	ui.label_2->setFixedWidth(120);
	ui.label_3->setFixedWidth(120);
	ui.label_4->setFixedWidth(120);
	ui.label_7->setFixedWidth(120);

	for (QComboBox* combo : { ui.comboBox_quality, ui.comboBox_log }) {
		combo->view()->setFrameShape(QFrame::NoFrame);
		combo->view()->setAlternatingRowColors(false);
		combo->setMaxVisibleItems(8);
	}

	// 锁定线程数上下限
	ui.spinBox_thread->setMaximum(10);
	ui.spinBox_thread->setMinimum(1);
	// 设定默认值
	setDefault();
	// 连接信号槽
	connect(ui.pushButton_open, &QPushButton::clicked, this, &Setting::openFileSavePath);
	connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &Setting::saveSettings);
}

Setting::~Setting()
{
}

void Setting::setDefault()
// 填充默认值
{
	g_settings->beginGroup("settings");
	ui.lineEdit_file_save_path->setText(g_settings->value("save_dir", defaultSaveDirectory()).toString());
	ui.spinBox_thread->setValue(g_settings->value("thread_num", 1).toInt());
	const bool transcode = g_settings->value("transcode", true).toBool();
	ui.radioButton_mp4->setChecked(transcode);
	ui.radioButton_ts->setChecked(!transcode);
	//ui.spinBox_program_1->setValue(g_settings->value("display_min", 1).toInt());
	//ui.spinBox_program_2->setValue(g_settings->value("display_max", 100).toInt());
	ui.comboBox_quality->setCurrentIndex(g_settings->value("quality", 1).toInt());
	ui.comboBox_log->setCurrentIndex(g_settings->value("log_level", 1).toInt());
	ui.checkBox_highlights->setChecked(g_settings->value("show_highlights", false).toBool());
	g_settings->endGroup();
}

void Setting::openFileSavePath()
// 打开文件保存位置
{
	auto dir = ui.lineEdit_file_save_path->text();
	dir = QFileDialog::getExistingDirectory(
		this,
		QString("选择保存路径"),
		dir.isEmpty() ? QDir::homePath() : dir,
		QFileDialog::ShowDirsOnly
		| QFileDialog::DontResolveSymlinks
	);
	if (!dir.isEmpty())
	{
		ui.lineEdit_file_save_path->setText(dir);
	}
}

void Setting::saveSettings()
// 保存设置项
{
	g_settings->beginGroup("settings");
	g_settings->setValue("save_dir", ui.lineEdit_file_save_path->text());
	g_settings->setValue("thread_num", ui.spinBox_thread->value());
	g_settings->setValue("transcode", ui.radioButton_mp4->isChecked());
	g_settings->setValue("quality", ui.comboBox_quality->currentIndex());
	g_settings->setValue("log_level", ui.comboBox_log->currentIndex());
	g_settings->setValue("show_highlights", ui.checkBox_highlights->isChecked());
	g_settings->endGroup();
	g_settings->sync();
}
