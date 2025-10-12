#include <QtTest>
#include <QCoreApplication>
#include <head/cctvvideodownloader.h>
#include <head/setting.h>

#include "apiTest.cpp"

class mainWindowTest : public QObject
{
    Q_OBJECT

private slots:

    //void initTestCase_data();

    //void initTestCase();

    //void init();

    void mainShowTest();

    //void cleanup();

    //void cleanupTestCase();
};

void mainWindowTest::mainShowTest()
{
    // 创建窗口
    CCTVVideoDownloader mainWindow;
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    mainWindow.close();
    QVERIFY(mainWindow.isHidden());
}
// 主函数

int main(int argc,char *argv[])
{
    QApplication app(argc, argv);

    int status = 0;

    apiTest test1;
    status |= QTest::qExec(&test1, argc, argv);

    mainWindowTest test2;
    status |= QTest::qExec(&test2, argc, argv);

    return status;
}

#include "mainWindowTest.moc"
