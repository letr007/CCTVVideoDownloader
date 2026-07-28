#include "downloadjob.h"

#include <QtTest>

class CoreHeadlessTests final : public QObject
{
    Q_OBJECT

private slots:
    void validatesJobTransitions();
    void classifiesFailurePolicies();
    void defaultsAreHeadlessSafe();
};

void CoreHeadlessTests::validatesJobTransitions()
{
    QVERIFY(isValidTransition(DownloadJobState::Created, DownloadJobState::Queued));
    QVERIFY(isValidTransition(DownloadJobState::Downloading, DownloadJobState::Failed));
    QVERIFY(isValidTransition(DownloadJobState::DirectFinalizing, DownloadJobState::Completed));
    QVERIFY(!isValidTransition(DownloadJobState::Completed, DownloadJobState::Downloading));
    QVERIFY(!isValidTransition(DownloadJobState::Failed, DownloadJobState::Queued));
}

void CoreHeadlessTests::classifiesFailurePolicies()
{
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::NetworkError), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::ValidationError), BatchFailurePolicy::SkipVideo);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::FileSystemError), BatchFailurePolicy::StopBatch);
    QCOMPARE(classifyFailurePolicy(DownloadErrorCategory::Unknown), BatchFailurePolicy::StopBatch);
}

void CoreHeadlessTests::defaultsAreHeadlessSafe()
{
    DownloadJob job;
    QCOMPARE(job.state, DownloadJobState::Created);
    QCOMPARE(job.stage, DownloadJobStage::None);
    QCOMPARE(job.errorCategory, DownloadErrorCategory::Unknown);
    QCOMPARE(job.request.threadCount, 2);
    QVERIFY(!job.request.transcodeToMp4);
}

QTEST_APPLESS_MAIN(CoreHeadlessTests)
#include "core_headless_tests.moc"
