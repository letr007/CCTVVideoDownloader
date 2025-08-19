#pragma once

#include <QObject>
#include <QPointer>
#include <QMutex>
#include <QString>
#include <QHash>

// 视频信息
struct VideoItem {
	QString guid;
	QString time;
	QString title;
	QString image;
	QString brief;

	// 构造函数
	VideoItem(QString g, QString t, QString ti, QString i, QString b)
		: guid(std::move(g)), time(std::move(t)), title(std::move(ti)),
		image(std::move(i)), brief(std::move(b)) {
	}
};

// M3U8质量信息
struct QualityInfo {
	int bandwidth;
	QString resolution;
};

class APIService : public QObject
{
	Q_OBJECT;

public:
	// 线程安全的单例访问
	static APIService& instance();
	// 删除拷贝构造和赋值
	APIService(const APIService&) = delete;
	APIService& operator=(const APIService&) = delete;

	// API接口
	QSharedPointer<QStringList> getPlayColumnInfo(const QString& url);

	QMap<int, VideoItem> getVideoList(const QString& column_id, const QString& item_id, int start_index, int end_index);

	QImage getImage(const QString& url);

	QStringList getEncryptM3U8Urls(const QString& GUID, const QString& quality);

	//int getVideoList(const QString& column_id, const QString& item_id, const int& start_index, const int& end_index);

signals:
	void PlayColumnInfoCallback(const QStringList& data);

private:
	explicit APIService(QObject* parent = nullptr);
	~APIService();

	QMutex m_mutex;
};