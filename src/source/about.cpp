#include "../head/about.h"

#include <QMovie>
#include <QSize>
#include <QUrl>
#include <QDesktopservices>

About::About(QWidget* parent) : QDialog(parent)
{
	ui.setupUi(this);
	// 设置图片
	QMovie *movie = new QMovie(":/resources/afraid.gif", QByteArray(), this);
	movie->setCacheMode(QMovie::CacheNone);
	movie->setScaledSize(QSize(100, 100));
	ui.label_img->setMovie(movie);
	movie->start();
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
