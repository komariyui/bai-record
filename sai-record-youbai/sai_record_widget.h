#ifndef SAI_RECORD_WIDGET_H
#define SAI_RECORD_WIDGET_H

#include <QElapsedTimer>
#include <QImage>
#include <QWidget>

#include "mouselistener.h"
#include "saiengine.h"

class AppSettings;
class QLabel;
class QCloseEvent;
class QComboBox;
class QEvent;
class QGraphicsOpacityEffect;
class QLineEdit;
class QParallelAnimationGroup;
class QPushButton;
class QResizeEvent;
class QSpinBox;
class QThread;
class QTimer;
class VideoSequenceWriter;

class SaiRecordWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SaiRecordWidget(QWidget *parent = nullptr);
    ~SaiRecordWidget() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *m_sideBar = nullptr;
    QWidget *m_bottomBar = nullptr;
    QWidget *m_centerPanel = nullptr;

    QPushButton *m_homeBtn = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    QComboBox *m_versionOverrideCombo = nullptr;
    QSpinBox *m_imageSizeLimitSpin = nullptr;
    QComboBox *m_canvasCombo = nullptr;
    QPushButton *m_quickExportBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_versionTitleLabel = nullptr;
    QLabel *m_sizeTitleLabel = nullptr;
    QLabel *m_canvasTitleLabel = nullptr;
    QLabel *m_outputTitleLabel = nullptr;
    QLineEdit *m_framesPathEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;

    QLabel *m_canvasPreview = nullptr;
    QLabel *m_versionStatusLabel = nullptr;
    QLabel *m_versionHintPopup = nullptr;
    QGraphicsOpacityEffect *m_versionHintOpacity = nullptr;
    QParallelAnimationGroup *m_versionHintAnimation = nullptr;

    AppSettings *m_settings = nullptr;
    SAIManager *m_saiManager = nullptr;
    VideoSequenceWriter *m_videoWriter = nullptr;
    MouseStrokeListener *m_mouseListener = nullptr;
    QThread *m_captureThread = nullptr;
    QObject *m_captureContext = nullptr;

    QTimer *m_saiPollTimer = nullptr;
    QTimer *m_windowCheckTimer = nullptr;

    bool m_isRecording = false;
    bool m_exportInProgress = false;
    HWND m_targetHwnd = nullptr;
    QImage m_lastSaiFrame;
    CanvasInfo m_currentCanvas;
    QElapsedTimer m_saiDisconnectTime;
    bool m_captureInFlight = false;
    bool m_previewCaptureInFlight = false;
    int m_pendingCaptureRequests = 0;
    int m_captureGeneration = 0;

    void setupUi();
    void setupSideBar();
    void setupCenterPanel();
    void setupBottomBar();
    void setupVersionHintPopup();
    void updatePanelGeometry();
    void showVersionHintPopup();
    void hideVersionHintPopup();
    QString uiText(const QString &key) const;
    void applyLanguage();

    void populateVersionOverride();
    void startSaiPolling();
    void stopSaiPolling();
    void checkSaiProcess();
    void onSaiConnected();
    void onSaiDisconnected();
    void refreshCanvasList();
    void updateCanvasPreview();
    void restoreFrameCountFromOutput();

    void chooseFramesPath();
    void openSettingsDialog();
    void quickExportVideo();
    void toggleRecording();
    void startRecording();
    void stopRecording();
    void requestCaptureFrame();
    void handleCapturedFrame(int generation, const QImage &frame, const QString &error);
    void requestPreviewFrame();
    void handlePreviewFrame(int generation, const QImage &frame, const QString &error);
    void onStrokeEnded();
    void onTargetWindowLost();
    void setStatusText(const QString &text, bool ok = false);
    void updateFrameCount();
};

#endif // SAI_RECORD_WIDGET_H
