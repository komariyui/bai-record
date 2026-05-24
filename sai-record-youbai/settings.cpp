#include "settings.h"
#include <QStandardPaths>
#include <QDir>

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(configDir);
    QString configPath = configDir + "/sai-record-youbai.json";
    m_settings = new QSettings(configPath, QSettings::IniFormat, this);
}

AppSettings::~AppSettings() {
    sync();
}

void AppSettings::sync() {
    m_settings->sync();
}

template<typename T>
T AppSettings::get(const QString& key, const T& defaultValue) const {
    QVariant v = m_settings->value(key);
    if (!v.isValid()) return defaultValue;
    return v.value<T>();
}

template<typename T>
void AppSettings::set(const QString& key, const T& value) {
    m_settings->setValue(key, QVariant::fromValue(value));
    emit settingsChanged();
}

// ============================================================
// 显式模板实例化 (QSettings 支持 QVariant 常用类型)
// ============================================================

QRect AppSettings::windowGeometry() const {
    return get<QRect>("window/geometry", QRect(100, 100, 700, 550));
}

void AppSettings::setWindowGeometry(const QRect& geo) {
    set("window/geometry", geo);
}

int AppSettings::selectedTab() const {
    return get<int>("ui/selected_tab", 0);
}

void AppSettings::setSelectedTab(int index) {
    set("ui/selected_tab", index);
}

QString AppSettings::framesPath() const {
    return get<QString>("recording/frames_path", "");
}

void AppSettings::setFramesPath(const QString& path) {
    set("recording/frames_path", path);
}

int AppSettings::imageSizeLimit() const {
    return get<int>("recording/image_size_limit", 1000);
}

void AppSettings::setImageSizeLimit(int limit) {
    set("recording/image_size_limit", limit);
}

int AppSettings::autoSplitSai() const {
    return get<int>("recording/auto_split_sai", 500);
}

void AppSettings::setAutoSplitSai(int count) {
    set("recording/auto_split_sai", count);
}

int AppSettings::autoSplitScreen() const {
    return get<int>("recording/auto_split_screen", 500);
}

void AppSettings::setAutoSplitScreen(int count) {
    set("recording/auto_split_screen", count);
}

int AppSettings::recordingType() const {
    return get<int>("recording/type", 0);
}

void AppSettings::setRecordingType(int type) {
    set("recording/type", type);
}

QString AppSettings::recordingCustomContainer() const {
    return get<QString>("recording/custom_container", "");
}

void AppSettings::setRecordingCustomContainer(const QString& c) {
    set("recording/custom_container", c);
}

QString AppSettings::recordingCustomCodec() const {
    return get<QString>("recording/custom_codec", "");
}

void AppSettings::setRecordingCustomCodec(const QString& c) {
    set("recording/custom_codec", c);
}

int AppSettings::exportType() const {
    return get<int>("export/type", 0);
}

void AppSettings::setExportType(int type) {
    set("export/type", type);
}

QString AppSettings::exportCustomContainer() const {
    return get<QString>("export/custom_container", "");
}

void AppSettings::setExportCustomContainer(const QString& c) {
    set("export/custom_container", c);
}

QString AppSettings::exportCustomCodec() const {
    return get<QString>("export/custom_codec", "");
}

void AppSettings::setExportCustomCodec(const QString& c) {
    set("export/custom_codec", c);
}

QString AppSettings::exportFilePath() const {
    return get<QString>("export/file_path", "");
}

void AppSettings::setExportFilePath(const QString& path) {
    set("export/file_path", path);
}

int AppSettings::exportTimeLimit() const {
    return get<int>("export/time_limit", 60);
}

void AppSettings::setExportTimeLimit(int seconds) {
    set("export/time_limit", seconds);
}

int AppSettings::exportFps() const {
    return get<int>("export/fps", 30);
}

void AppSettings::setExportFps(int fps) {
    set("export/fps", fps);
}

bool AppSettings::exportUseRecordingType() const {
    return get<bool>("export/use_recording_type", true);
}

void AppSettings::setExportUseRecordingType(bool use) {
    set("export/use_recording_type", use);
}

int AppSettings::saiVersionOverride() const {
    return get<int>("sai/version_override", -1);
}

void AppSettings::setSaiVersionOverride(int index) {
    set("sai/version_override", index);
}

QString AppSettings::theme() const {
    return get<QString>("ui/theme", "light");
}

void AppSettings::setTheme(const QString& name) {
    set("ui/theme", name);
}

QString AppSettings::language() const {
    return get<QString>("ui/language", "zh_CN");
}

void AppSettings::setLanguage(const QString& language) {
    set("ui/language", language);
}

int AppSettings::saiCanvasIndex() const {
    return get<int>("sai/canvas_index", 0);
}

void AppSettings::setSaiCanvasIndex(int index) {
    set("sai/canvas_index", index);
}
