#include "../head/about.h"

#include <QPixmap>
#include <QSize>
#include <QUrl>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>

About::About(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	ui.label_version->setText(tr("版本：%1").arg(QCoreApplication::applicationVersion()));

	const QPixmap appIcon(QStringLiteral(":/cctvvideodownload.png"));
	ui.label_img->setPixmap(appIcon.scaled(
		QSize(112, 112), Qt::KeepAspectRatio, Qt::SmoothTransformation));

	ui.buttonBox->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
	// 设置链接
	ui.label_link->setOpenExternalLinks(true);
	connect(ui.label_link, &QLabel::linkActivated, this,
		[](const QString& link) {
		QDesktopServices::openUrl(QUrl(link));
		});
}

About::~About()
{
}
