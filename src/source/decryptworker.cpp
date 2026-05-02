#include "../head/decryptworker.h"
#include "../head/mediafinalizer.h"
#include "../head/mediacontainervalidator.h"
#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#include <QStringConverter>
#include <QCoreApplication>

namespace {

int normalizeProcessTimeoutMs(int timeoutMs)
{
    return timeoutMs > 0 ? timeoutMs : 30000;
}

bool canCreateAndRemoveProbeFile(const QString& directoryPath)
{
    const QString probeFilePath = QDir(directoryPath).filePath(
        QStringLiteral(".decrypt_write_probe_%1.tmp").arg(QString::number(QCoreApplication::applicationPid()))
    );

    QFile probeFile(probeFilePath);
    if (!probeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    if (probeFile.write("ok") != 2) {
        probeFile.close();
        QFile::remove(probeFilePath);
        return false;
    }

    probeFile.close();
    return QFile::remove(probeFilePath);
}

QString cappedDiagnosticText(const QString& text)
{
    QString trimmed = text.trimmed();
    if (trimmed.size() > 2048) {
        trimmed.truncate(2048);
    }
    return trimmed;
}

QString preferredProcessDiagnostic(const DecryptProcessResult& result)
{
    const QString stderrText = cappedDiagnosticText(result.stderrText);
    if (!stderrText.isEmpty()) {
        return stderrText;
    }

    const QString stdoutText = cappedDiagnosticText(result.stdoutText);
    if (!stdoutText.isEmpty()) {
        return stdoutText;
    }

    return QStringLiteral("cbox 退出失败");
}

void logProcessDiagnostics(const DecryptProcessResult& result)
{
    qInfo() << "CBOX stdout:" << cappedDiagnosticText(result.stdoutText);
    qInfo() << "CBOX stderr:" << cappedDiagnosticText(result.stderrText);
}

bool restoreConvertedInputToTs(const QString& cboxPath, const QString& tsPath)
{
	if (QFileInfo::exists(tsPath) || !QFileInfo::exists(cboxPath)) {
		return true;
	}

	if (QFile::rename(cboxPath, tsPath)) {
		return true;
	}

	if (!QFile::copy(cboxPath, tsPath)) {
		return false;
	}

	if (!QFile::remove(cboxPath)) {
		QFile::remove(tsPath);
		return false;
	}

    return true;
}

void cleanupProcessFailureArtifacts(const QString& savePath, bool removeCopiedLicense)
{
    const QString outputTxtPath = QDir(savePath).filePath("output.txt");
    if (QFile::exists(outputTxtPath) && !QFile::remove(outputTxtPath)) {
        qWarning() << "清理 output.txt 失败:" << outputTxtPath;
    }

    if (!removeCopiedLicense) {
        return;
    }

    const QString licensePath = QDir(savePath).filePath("UDRM_LICENSE.v1.0");
    if (QFile::exists(licensePath) && !QFile::remove(licensePath)) {
        qWarning() << "清理许可证文件失败:" << licensePath;
    }
}

DecryptProcessResult runDecryptProcess(const DecryptProcessRequest& request)
{
    QProcess cbox;
    cbox.setWorkingDirectory(request.workingDirectory);
    cbox.start(request.program, request.arguments);

    DecryptProcessResult result;
    if (!cbox.waitForStarted(request.timeoutMs)) {
        result.errorString = cbox.errorString();
        return result;
    }

    result.started = true;
    if (!cbox.waitForFinished(request.timeoutMs)) {
        result.timedOut = true;
        result.stdoutText = QString::fromLocal8Bit(cbox.readAllStandardOutput());
        result.stderrText = QString::fromLocal8Bit(cbox.readAllStandardError());
        result.errorString = cbox.errorString();

        cbox.terminate();
        if (!cbox.waitForFinished(1000) && cbox.state() != QProcess::NotRunning) {
            cbox.kill();
            cbox.waitForFinished(1000);
        }

        result.exitCode = cbox.exitCode();
        result.exitStatus = cbox.exitStatus();
        result.errorString = cbox.errorString();
        return result;
    }

    result.exitCode = cbox.exitCode();
    result.exitStatus = cbox.exitStatus();
    result.stdoutText = QString::fromLocal8Bit(cbox.readAllStandardOutput());
    result.stderrText = QString::fromLocal8Bit(cbox.readAllStandardError());
    result.errorString = cbox.errorString();
    return result;
}

} // namespace

bool removeDirectory(const QString& path) {
    qDebug() << "删除目录:" << path;
    
	QDir dir(path);
	if (!dir.exists()) {
        qDebug() << "目录不存在，无需删除";
		return true;
    }

	// 递归删除所有文件和子文件夹
	dir.setFilter(QDir::NoDotAndDotDot | QDir::AllEntries);
    QFileInfoList entries = dir.entryInfoList();
    qDebug() << "目录包含" << entries.size() << "个条目";
    
	for (const QFileInfo& info : entries) {
		if (info.isDir()) {
            qDebug() << "删除子目录:" << info.absoluteFilePath();
			if (!removeDirectory(info.absoluteFilePath()))
				return false;
		}
		else {
            qDebug() << "删除文件:" << info.absoluteFilePath();
			if (!QFile::remove(info.absoluteFilePath())) {
				qWarning() << "删除文件失败:" << info.absoluteFilePath();
				return false;
			}
		}
	}

	// 删除空文件夹本身
    qDebug() << "删除空目录:" << path;
	bool result = dir.rmdir(path);
    if (!result) {
        qWarning() << "删除目录失败:" << path;
    } else {
        qDebug() << "目录删除成功:" << path;
    }
    
	return result;
}

DecryptWorker::DecryptWorker(QObject* parent)
{
}

void DecryptWorker::setProcessTimeoutMs(int timeoutMs)
{
    m_processTimeoutMs = normalizeProcessTimeoutMs(timeoutMs);
}

QString DecryptWorker::decryptAssetsDir() const
{
#ifdef CORE_REGRESSION_TESTS
    if (!m_testDecryptAssetsDir.isEmpty()) {
        return m_testDecryptAssetsDir;
    }
#endif

    return QDir(QCoreApplication::applicationDirPath()).filePath("decrypt");
}

void DecryptWorker::doDecrypt()
{
    qInfo() << "开始视频解密 - 视频名称:" << m_name << "保存路径:" << m_savePath;

    const QString trimmedName = m_name.trimmed();
    const QString trimmedSavePath = m_savePath.trimmed();
    if (trimmedName.isEmpty() || trimmedSavePath.isEmpty()) {
        emit decryptFinished(false, "解密失败 [code=invalid_params]: 解密参数无效");
        return;
    }

    QFileInfo savePathInfo(trimmedSavePath);
    if (!savePathInfo.exists() || !savePathInfo.isDir()) {
        emit decryptFinished(false, "解密失败 [code=output_unwritable]: 输出目录不可写");
        return;
    }
    
	auto nameHash = QString(
		QCryptographicHash::hash(m_name.toUtf8(), QCryptographicHash::Sha256)
		.toHex()
	);
	auto filePath = QDir::cleanPath(trimmedSavePath + "/" + nameHash);
    qInfo() << "临时文件路径:" << filePath;

	QFileInfo tempDirectoryInfo(filePath);
	if (!tempDirectoryInfo.exists() || !tempDirectoryInfo.isDir()) {
		emit decryptFinished(false, "解密失败 [code=input_missing]: result.ts 不存在");
		return;
	}

	QString stagingInputPath = QDir(filePath).filePath("result.ts");
	QString cboxPath = QDir(filePath).filePath("input.cbox");
	const QString stagedDecryptedTsPath = stagingInputPath;
	qInfo() << "TS文件路径:" << stagingInputPath << "CBOX文件路径:" << cboxPath << "解密输出路径:" << stagedDecryptedTsPath;

	QFileInfo stagingInfo(stagingInputPath);
	if (!stagingInfo.exists() || !stagingInfo.isFile()) {
		emit decryptFinished(false, "解密失败 [code=input_missing]: result.ts 不存在");
		return;
	}

	const MediaContainerType desiredContainer = m_transcodeToMp4 ? MediaContainerType::Mp4 : MediaContainerType::MpegTs;
	const QString assetsDir = decryptAssetsDir();
	QString cboxExe = QDir(assetsDir).filePath("cbox.exe");
	qInfo() << "输出文件路径:" << stagedDecryptedTsPath << "CBOX执行文件:" << cboxExe;

	QFileInfo cboxExeInfo(cboxExe);
	if (!cboxExeInfo.exists() || !cboxExeInfo.isFile()) {
		emit decryptFinished(false, "解密失败 [code=cbox_missing]: decrypt/cbox.exe 不存在");
		return;
	}

	QString licenseSource = QDir(assetsDir).filePath("UDRM_LICENSE.v1.0");
	QString licenseTarget = QDir(trimmedSavePath).filePath("UDRM_LICENSE.v1.0");
	qInfo() << "许可证源文件路径:" << licenseSource << "，目标路径:" << licenseTarget;

	QFileInfo licenseSourceInfo(licenseSource);
	if (!licenseSourceInfo.exists() || !licenseSourceInfo.isFile()) {
		emit decryptFinished(false, "解密失败 [code=license_missing]: 解密所需的许可证文件不存在");
		return;
	}

	if (!canCreateAndRemoveProbeFile(trimmedSavePath)) {
		emit decryptFinished(false, "解密失败 [code=output_unwritable]: 输出目录不可写");
		return;
	}

	if (QFile::exists(cboxPath)) {
        qInfo() << "删除已存在的CBOX文件";
		QFile::remove(cboxPath);
    }

    qInfo() << "重命名TS文件为CBOX文件";
	if (!QFile::rename(stagingInputPath, cboxPath)) {
        qWarning() << "重命名失败，尝试复制方式";
		if (!QFile::copy(stagingInputPath, cboxPath) || !QFile::remove(stagingInputPath)) {
			qCritical() << "重命名/复制 result.ts -> input.cbox 失败";
			emit decryptFinished(false, "重命名TS->CBOX失败");
			return;
		}
        qInfo() << "通过复制方式完成文件转换";
	} else {
        qInfo() << "重命名成功";
	}
	
	bool licenseCopiedByThisRun = false;
	// 检查目标文件是否已存在，仅当不存在时才复制
	if (!QFile::exists(licenseTarget)) {
		if (QFile::copy(licenseSource, licenseTarget)) {
			licenseCopiedByThisRun = true;
			qInfo() << "成功复制许可证文件到输出目录。";
		}
		else {
			qWarning() << "复制许可证文件失败。解密进程因缺少许可证而失败。";
		}
	}
	else {
		qInfo() << "目标许可证文件已存在，跳过复制。";
	}

	DecryptProcessRequest request;
	request.program = cboxExe;
	request.arguments = { cboxPath, stagedDecryptedTsPath };
	request.workingDirectory = trimmedSavePath;
	request.timeoutMs = m_processTimeoutMs;

    qInfo() << "启动CBOX解密进程，参数:" << request.arguments;

	DecryptProcessResult processResult;
#ifdef CORE_REGRESSION_TESTS
	if (m_testProcessRunner) {
		processResult = m_testProcessRunner(request);
	}
	else {
		processResult = runDecryptProcess(request);
	}
#else
	processResult = runDecryptProcess(request);
#endif

    logProcessDiagnostics(processResult);

	if (!processResult.started)
	{
		const QString errorString = cappedDiagnosticText(processResult.errorString);
		qCritical() << "无法启动cbox进程" << errorString;
		if (!restoreConvertedInputToTs(cboxPath, stagingInputPath)) {
			qWarning() << "回滚 input.cbox -> result.ts 失败:" << cboxPath << stagingInputPath;
		}
		cleanupProcessFailureArtifacts(trimmedSavePath, licenseCopiedByThisRun);
		emit decryptFinished(false, errorString.isEmpty()
			? QStringLiteral("解密失败 [code=start_failed]: 无法启动cbox")
			: QStringLiteral("解密失败 [code=start_failed]: 无法启动cbox: ") + errorString);
		return;
	}

	if (processResult.timedOut)
	{
		qCritical() << "CBOX解密超时，超时时间:" << request.timeoutMs << "ms";
		if (!restoreConvertedInputToTs(cboxPath, stagingInputPath)) {
			qWarning() << "回滚 input.cbox -> result.ts 失败:" << cboxPath << stagingInputPath;
		}
		cleanupProcessFailureArtifacts(trimmedSavePath, licenseCopiedByThisRun);
		emit decryptFinished(false, QStringLiteral("解密失败 [code=timeout]: cbox 超时 %1 ms").arg(request.timeoutMs));
		return;
	}

	if (!processResult.timedOut && (processResult.exitCode != 0 || processResult.exitStatus != QProcess::NormalExit))
	{
		const QString diagnostic = preferredProcessDiagnostic(processResult);
		qCritical() << "CBOX解密失败，退出码:" << processResult.exitCode << "错误信息:" << diagnostic;
		if (!restoreConvertedInputToTs(cboxPath, stagingInputPath)) {
			qWarning() << "回滚 input.cbox -> result.ts 失败:" << cboxPath << stagingInputPath;
		}
		cleanupProcessFailureArtifacts(trimmedSavePath, licenseCopiedByThisRun);
		emit decryptFinished(false, QStringLiteral("解密失败 [code=process_failed; exit_code=%1]: %2")
			.arg(processResult.exitCode)
			.arg(diagnostic));
		return;
	}

    qInfo() << "CBOX解密成功完成，退出码:" << processResult.exitCode;

	const MediaContainerValidationResult validation =
		MediaContainerValidator::validateFile(stagedDecryptedTsPath, MediaContainerType::MpegTs);
	if (!validation.ok) {
		qCritical() << "CBOX输出不是有效的TS文件:" << validation.code << validation.message;
		QFile::remove(stagedDecryptedTsPath);
		if (!restoreConvertedInputToTs(cboxPath, stagingInputPath)) {
			qWarning() << "回滚 input.cbox -> result.ts 失败:" << cboxPath << stagingInputPath;
		}
		cleanupProcessFailureArtifacts(trimmedSavePath, licenseCopiedByThisRun);
		emit decryptFinished(false, QStringLiteral("解密失败 [code=invalid_cbox_output]: [%1] %2")
			.arg(validation.code, validation.message));
		return;
	}

	MediaFinalizer finalizer;
	finalizer.setProcessTimeoutMs(m_processTimeoutMs);
	const MediaFinalizeResult finalizeResult = finalizer.finalize(stagedDecryptedTsPath,
		m_name,
		trimmedSavePath,
		desiredContainer);
	if (!finalizeResult.ok) {
		qCritical() << "MediaFinalizer 发布失败:" << finalizeResult.code << finalizeResult.message;
		cleanupProcessFailureArtifacts(trimmedSavePath, licenseCopiedByThisRun);
		emit decryptFinished(false, QStringLiteral("解密失败 [code=%1]: %2")
			.arg(finalizeResult.code, finalizeResult.message));
		return;
	}

	const QString finalPath = finalizeResult.finalPath;
	qInfo() << "最终文件路径:" << finalPath;
	if (QFile::exists(stagedDecryptedTsPath) && !QFile::remove(stagedDecryptedTsPath)) {
		qWarning() << "清理解密阶段 result.ts 失败:" << stagedDecryptedTsPath;
	}

	if (!removeDirectory(filePath))
	{
		qWarning() << "移除临时文件夹失败，请手动清理:" << filePath;
		emit decryptFinished(false, "移除临时文件夹失败\n请手动清理");
		return;
	} else {
        qInfo() << "临时文件夹清理成功";
    }

    qInfo() << "清理临时文件";
	QFile::remove(QDir(trimmedSavePath).filePath("output.txt"));
	if (licenseCopiedByThisRun) {
		QFile::remove(QDir(trimmedSavePath).filePath("UDRM_LICENSE.v1.0"));
	}

    qInfo() << "视频解密全部完成，输出文件:" << finalPath;
	emit decryptFinished(true, "解密完成，输出 " + m_name);
}

#ifdef CORE_REGRESSION_TESTS
void DecryptWorker::setTestProcessRunner(const std::function<DecryptProcessResult(const DecryptProcessRequest&)>& runner)
{
    m_testProcessRunner = runner;
}

void DecryptWorker::clearTestProcessRunner()
{
    m_testProcessRunner = {};
}

void DecryptWorker::setTestDecryptAssetsDir(const QString& decryptAssetsDir)
{
    m_testDecryptAssetsDir = decryptAssetsDir;
}

void DecryptWorker::clearTestDecryptAssetsDir()
{
    m_testDecryptAssetsDir.clear();
}
#endif
