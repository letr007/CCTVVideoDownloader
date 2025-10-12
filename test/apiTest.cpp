#include <QtTest>
#include <QSignalSpy>
#include <head/apiservice.h>

class apiTest : public QObject
{
	Q_OBJECT

private slots:
	void importTest();
};

void apiTest::importTest()
{
	QString testUrl = QString("https://tv.cctv.com/2025/08/22/VIDEvfzgBU8ikjbL7Cqk8IJc250822.shtml?spm=C31267.PXDaChrrDGdt.EbD5Beq0unIQ.3");
	auto result = APIService::instance().getPlayColumnInfo(testUrl);
	if (result == nullptr || result->size() != 3 || result->at(0).isEmpty())
	{
		QVERIFY2(0, "导入API测试:API无返回");
	}
	// 构建JSON数据
	QJsonObject results{
		{"name", result->at(0)},
		{"itemid", result->at(1)},
		{"columnid", result->at(2)},
	};
	// 压缩编码为base64存储
	QByteArray jsonData = QJsonDocument(results).toJson(QJsonDocument::Compact);
	QString currentData = jsonData.toBase64();
	QVERIFY2(currentData.isEmpty(), "导入API测试:编码base64失败");
}

#include "apiTest.moc"