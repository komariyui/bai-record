#ifndef VIDEOWRITER_H
#define VIDEOWRITER_H

#include <QObject>
#include <QImage>
#include <QDir>
#include <QString>
#include <QThread>
#include <memory>
#include <atomic>

// ============================================================
// FrameSaverWorker - 在后台线程中保存帧图片
// 通过跨线程信号/槽接收 QImage，避免阻塞 UI 线程
// ============================================================
class FrameSaverWorker : public QObject {
    Q_OBJECT
public:
    explicit FrameSaverWorker(QObject* parent = nullptr);

public slots:
    // 在后台线程保存帧（通过跨线程队列信号调用）
    void saveFrame(QImage image, QString filePath);

signals:
    void frameSaved(const QString& filePath);
    void saveFailed(const QString& filePath, const QString& error);
};

// ============================================================
// VideoSequenceWriter - 逐帧保存为图片文件 (PNG)
// 帧保存在线程中异步进行，主线程不阻塞
// ============================================================
class VideoSequenceWriter : public QObject {
    Q_OBJECT
public:
    explicit VideoSequenceWriter(QObject* parent = nullptr);
    ~VideoSequenceWriter() override;

    // 初始化输出目录
    void setOutputDir(const QString& dirPath);
    void setFramePrefix(const QString& prefix);
    int restoreFrameCountFromOutput();

    // 写入一帧（立即返回，后台异步保存 PNG）
    bool writeFrame(const QImage& image);

    // 停止录制，等待后台保存完成
    void close();

    bool isRecording() const { return m_recording; }
    void setRecording(bool r) { m_recording = r; }

    QString currentDir() const { return m_outputDir; }
    int frameCount() const { return m_totalFrames; }

signals:
    void errorOccurred(const QString& message);
    void segmentCreated(const QString& filePath); // 保存完成时发射

    // 内部信号：跨线程请求后台保存
    void requestSaveFrame(QImage image, QString filePath);

private:
    QString m_outputDir;
    QString m_framePrefix = "frame";
    int m_totalFrames = 0;
    bool m_recording = false;

    int scanExistingFrameCount() const;

    QThread* m_workerThread = nullptr;
    FrameSaverWorker* m_worker = nullptr;
    std::atomic<int> m_pendingSaves{0}; // 等待保存的帧数
};

// ============================================================
// VideoExporter - 将帧序列导出为视频（使用 FFmpeg）
// ============================================================
class VideoExporter : public QObject {
    Q_OBJECT
public:
    explicit VideoExporter(QObject* parent = nullptr);

    void setFramesDir(const QString& dir);
    void setOutputPath(const QString& path);
    void setFramePrefix(const QString& prefix);
    void setContainer(const QString& c) { m_container = c; }
    void setCodec(const QString& c) { m_codec = c; }
    void setFps(int fps) { m_fps = fps; }
    void setTimeLimit(int seconds) { m_timeLimit = seconds; }

    int scanTotalFrames();
    QSize scanFrameSize();

    bool exportVideo(std::function<bool(int, int)> progressCallback = nullptr);
    QString lastError() const { return m_lastError; }

    QStringList getFrameFiles() const;

signals:
    void errorOccurred(const QString& message);
    void progressUpdated(int current, int total);

private:
    QString m_framesDir;
    QString m_outputPath;
    QString m_framePrefix = "frame";
    QString m_container = "mp4";
    QString m_codec = "libx264";
    QString m_lastError;
    int m_fps = 30;
    int m_timeLimit = 60;
};

#endif // VIDEOWRITER_H
