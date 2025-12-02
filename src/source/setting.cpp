#include "../head/setting.h"
#include "../head/config.h"

#include <QFileDialog>
#include <QDir>

Setting::Setting(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	// 锁定转码按钮
	ui.radioButton_ts->setChecked(true);
	ui.radioButton_ts->setEnabled(false);
	ui.radioButton_mp4->setEnabled(false);
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
	ui.lineEdit_file_save_path->setText(g_settings->value("save_dir", "C:\\Video").toString());
	ui.spinBox_thread->setValue(g_settings->value("thread_num", 10).toInt());
	//ui.spinBox_program_1->setValue(g_settings->value("display_min", 1).toInt());
	//ui.spinBox_program_2->setValue(g_settings->value("display_max", 100).toInt());
	ui.dateEdit_1->setDate(QDateTime::fromString(g_settings->value("date_beg", "202501").toString(), "yyyyMM").date());
	ui.dateEdit_2->setDate(QDateTime::fromString(g_settings->value("date_end", "202501").toString(), "yyyyMM").date());
	ui.comboBox_quality->setCurrentIndex(g_settings->value("quality", 1).toInt());
	ui.comboBox_log->setCurrentIndex(g_settings->value("log_level", 1).toInt());
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
	g_settings->setValue("date_beg", ui.dateEdit_1->date().toString("yyyyMM"));
	g_settings->setValue("date_end", ui.dateEdit_2->date().toString("yyyyMM"));
	g_settings->setValue("quality", ui.comboBox_quality->currentIndex());
	g_settings->setValue("log_level", ui.comboBox_log->currentIndex());
	g_settings->endGroup();
	g_settings->sync();
}////