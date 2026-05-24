#include "videowriter.h"
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

// ============================================================
// FrameSaverWorker - 后台线程帧保存
// ============================================================
FrameSaverWorker::FrameSaverWorker(QObject* parent)
    : QObject(parent) {}

void FrameSaverWorker::saveFrame(QImage image, QString filePath) {
    // 此函数在后台线程运行，不会阻塞 UI
    bool ok = image.save(filePath, "PNG");
    if (ok) {
        emit frameSaved(filePath);
    } else {
        emit saveFailed(filePath, "Failed to save PNG");
    }
}

// ============================================================
// VideoSequenceWriter - 异步帧保存
// ============================================================
VideoSequenceWriter::VideoSequenceWriter(QObject* parent)
    : QObject(parent)
{
    // 创建后台工作线程
    m_workerThread = new QThread(this);
    m_worker = new FrameSaverWorker(); // 无 parent — 会 moveToThread
    m_worker->moveToThread(m_workerThread);

    // 主线程信号 → 工作线程槽 (QueuedConnection, 自动)
    connect(this, &VideoSequenceWriter::requestSaveFrame,
            m_worker, &FrameSaverWorker::saveFrame);

    // 工作线程成功保存 → 主线程信号
    connect(m_worker, &FrameSaverWorker::frameSaved,
            this, [this](const QString& filePath) {
        m_pendingSaves--;
        emit segmentCreated(filePath);
    });

    // 工作线程保存失败 → 主线程错误信号
    connect(m_worker, &FrameSaverWorker::saveFailed,
            this, [this](const QString& filePath, const QString& error) {
        m_pendingSaves--;
        emit errorOccurred(error + ": " + filePath);
    });

    // 线程结束时清理 worker
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_workerThread->start();
}

VideoSequenceWriter::~VideoSequenceWriter() {
    close();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(3000);
    }
}

void VideoSequenceWriter::setOutputDir(const QString& dirPath) {
    m_outputDir = dirPath;
    QDir().mkpath(m_outputDir);
}

void VideoSequenceWriter::setFramePrefix(const QString& prefix) {
    QString safe = prefix.trimmed();
    safe.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    safe.replace(QRegularExpression(R"(\s+)"), "_");
    safe.replace(QRegularExpression(R"(_+)"), "_");
    safe = safe.trimmed();
    while (safe.startsWith('_')) safe.remove(0, 1);
    while (safe.endsWith('_')) safe.chop(1);
    m_framePrefix = safe.isEmpty() ? "frame" : safe;
}

int VideoSequenceWriter::restoreFrameCountFromOutput() {
    m_totalFrames = scanExistingFrameCount();
    return m_totalFrames;
}

int VideoSequenceWriter::scanExistingFrameCount() const {
    if (m_outputDir.isEmpty()) {
        return 0;
    }

    QDir dir(m_outputDir);
    if (!dir.exists()) {
        return 0;
    }

    const QRegularExpression re(
        QString(R"(^%1_(\d+)\.png$)").arg(QRegularExpression::escape(m_framePrefix)),
        QRegularExpression::CaseInsensitiveOption);

    int maxFrame = 0;
    const QStringList files = dir.entryList(QStringList() << "*.png", QDir::Files, QDir::Name);
    for (const QString& fileName : files) {
        const QRegularExpressionMatch match = re.match(fileName);
        if (!match.hasMatch()) {
            continue;
        }
        bool ok = false;
        const int frameNumber = match.captured(1).toInt(&ok);
        if (ok) {
            maxFrame = std::max(maxFrame, frameNumber);
        }
    }

    return maxFrame;
}

bool VideoSequenceWriter::writeFrame(const QImage& image) {
    if (!m_recording) return false;
    if (image.isNull()) {
        emit errorOccurred("Cannot save null image frame");
        return false;
    }

    // 主线程只做两件事：递增计数 + 发出跨线程信号
    m_totalFrames++;
    QString fileName = QString("%1_%2.png")
        .arg(m_framePrefix)
        .arg(m_totalFrames, 6, 10, QChar('0'));
    QString filePath = m_outputDir + "/" + fileName;

    m_pendingSaves++;
    // 深拷贝 QImage 以确保 worker 线程拥有独立数据
    emit requestSaveFrame(image.copy(), filePath);
    return true;
}

void VideoSequenceWriter::close() {
    m_recording = false;

    // 等待所有后台保存完成，但不要关闭工作线程；后续录制还会继续复用它。
    while (m_pendingSaves > 0) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

}

// ============================================================
// VideoExporter - 导出工具 (帧图片 → 视频)
// ============================================================
VideoExporter::VideoExporter(QObject* parent)
    : QObject(parent) {}

void VideoExporter::setFramesDir(const QString& dir) {
    m_framesDir = dir;
}

void VideoExporter::setOutputPath(const QString& path) {
    m_outputPath = path;
}

void VideoExporter::setFramePrefix(const QString& prefix) {
    QString safe = prefix.trimmed();
    safe.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    safe.replace(QRegularExpression(R"(\s+)"), "_");
    safe.replace(QRegularExpression(R"(_+)"), "_");
    safe = safe.trimmed();
    while (safe.startsWith('_')) safe.remove(0, 1);
    while (safe.endsWith('_')) safe.chop(1);
    m_framePrefix = safe.isEmpty() ? "frame" : safe;
}

QStringList VideoExporter::getFrameFiles() const {
    QDir dir(m_framesDir);
    QStringList files = dir.entryList(QStringList() << QString("%1_*.png").arg(m_framePrefix), QDir::Files, QDir::Name);
    QStringList result;
    for (const auto& f : files) {
        result << dir.absoluteFilePath(f);
    }
    return result;
}

int VideoExporter::scanTotalFrames() {
    return getFrameFiles().size();
}

QSize VideoExporter::scanFrameSize() {
    QStringList files = getFrameFiles();
    if (files.isEmpty()) return QSize(0, 0);

    QImage firstFrame(files.first());
    if (firstFrame.isNull()) return QSize(0, 0);
    return firstFrame.size();
}

bool VideoExporter::exportVideo(std::function<bool(int, int)> progressCallback) {
    m_lastError.clear();
    if (m_framesDir.isEmpty()) {
        m_lastError = "Frames directory is empty";
        return false;
    }

    QStringList files = getFrameFiles();
    if (files.isEmpty()) {
        m_lastError = QString("No PNG frames matched %1_*.png in %2").arg(m_framePrefix, m_framesDir);
        return false;
    }

    QString outPath = m_outputPath;
    if (outPath.isEmpty()) {
        outPath = m_framesDir + "/export." + m_container;
    }

    // 使用 FFmpeg 将帧图片序列合成为视频
    // 输入模式: frame_%06d.png
    QString pattern = m_framesDir + "/" + m_framePrefix + "_%06d.png";

    QStringList args;
    args << "-y"
         << "-framerate" << QString::number(m_fps)
         << "-i" << pattern
         << "-vf" << "pad=ceil(iw/2)*2:ceil(ih/2)*2"
         << "-c:v" << m_codec
         << "-preset" << "medium"
         << "-crf" << "23"
         << "-pix_fmt" << "yuv420p";

    if (m_timeLimit > 0) {
        args << "-t" << QString::number(m_timeLimit);
    }

    args << outPath;

    QString ffmpeg = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("ffmpeg.exe");
    if (!QFileInfo::exists(ffmpeg)) {
        ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    }
    if (ffmpeg.isEmpty()) {
        m_lastError = "FFmpeg was not found in PATH";
        return false;
    }

    QProcess exportProc;
    exportProc.setProcessChannelMode(QProcess::SeparateChannels);
    exportProc.start(ffmpeg, args);

    if (progressCallback) {
        progressCallback(0, files.size());
    }

    if (!exportProc.waitForStarted(5000)) {
        m_lastError = QString("Failed to start FFmpeg: %1").arg(exportProc.errorString());
        return false;
    }

    QString stderrText;
    int lastProgress = 0;
    const QRegularExpression frameRe(R"(frame=\s*(\d+))");
    while (exportProc.state() != QProcess::NotRunning) {
        if (exportProc.waitForReadyRead(100)) {
            const QString chunk = QString::fromLocal8Bit(exportProc.readAllStandardError());
            stderrText += chunk;
            const auto matches = frameRe.globalMatch(chunk);
            auto it = matches;
            while (it.hasNext()) {
                const auto match = it.next();
                bool ok = false;
                const int frame = match.captured(1).toInt(&ok);
                if (ok && frame > lastProgress) {
                    lastProgress = std::min(frame, static_cast<int>(files.size()));
                    if (progressCallback && !progressCallback(lastProgress, files.size())) {
                        exportProc.kill();
                        exportProc.waitForFinished(3000);
                        m_lastError = "Export cancelled";
                        return false;
                    }
                }
            }
        }
    }
    stderrText += QString::fromLocal8Bit(exportProc.readAllStandardError());

    if (exportProc.exitCode() == 0) {
        if (progressCallback) {
            progressCallback(files.size(), files.size());
        }
        return true;
    }

    const QString detail = stderrText.trimmed().right(1000);
    m_lastError = detail.isEmpty()
        ? QString("FFmpeg exited with code %1: %2").arg(exportProc.exitCode()).arg(exportProc.errorString())
        : detail;
    return false;
}
