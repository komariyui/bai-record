#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QRect>
#include <QVariant>

// ============================================================
// AppSettings - 应用设置管理（JSON 持久化）
// ============================================================
class AppSettings : public QObject {
    Q_OBJECT
public:
    explicit AppSettings(QObject* parent = nullptr);
    ~AppSettings() override;

    // 基本设置
    QRect windowGeometry() const;
    void setWindowGeometry(const QRect& geo);

    int selectedTab() const;
    void setSelectedTab(int index);

    // 帧保存路径
    QString framesPath() const;
    void setFramesPath(const QString& path);

    // 图像尺寸限制
    int imageSizeLimit() const;
    void setImageSizeLimit(int limit);

    // SAI 自动分片计数
    int autoSplitSai() const;
    void setAutoSplitSai(int count);

    // 屏幕自动分片计数
    int autoSplitScreen() const;
    void setAutoSplitScreen(int count);

    // 录制视频类型索引 (0=mp4/avc1, 1=webm/vp80, 2=custom)
    int recordingType() const;
    void setRecordingType(int type);

    // 自定义容器/编码器
    QString recordingCustomContainer() const;
    void setRecordingCustomContainer(const QString& c);
    QString recordingCustomCodec() const;
    void setRecordingCustomCodec(const QString& c);

    // 导出设置
    int exportType() const;
    void setExportType(int type);
    QString exportCustomContainer() const;
    void setExportCustomContainer(const QString& c);
    QString exportCustomCodec() const;
    void setExportCustomCodec(const QString& c);

    QString exportFilePath() const;
    void setExportFilePath(const QString& path);

    int exportTimeLimit() const;
    void setExportTimeLimit(int seconds);

    int exportFps() const;
    void setExportFps(int fps);

    bool exportUseRecordingType() const;
    void setExportUseRecordingType(bool use);

    // SAI 版本覆盖索引 (-1 = 不覆盖)
    int saiVersionOverride() const;
    void setSaiVersionOverride(int index);

    // 主题
    QString theme() const;
    void setTheme(const QString& name);
    QString language() const;
    void setLanguage(const QString& language);

    // 画布选择
    int saiCanvasIndex() const;
    void setSaiCanvasIndex(int index);

    // 保存所有设置
    void sync();

signals:
    void settingsChanged();

private:
    QSettings* m_settings;

    template<typename T>
    T get(const QString& key, const T& defaultValue) const;

    template<typename T>
    void set(const QString& key, const T& value);
};

#endif // SETTINGS_H
