#include "sai_record_widget.h"

#include "settings.h"
#include "videowriter.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEasingCurve>
#include <QEvent>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace {

constexpr int kBottomMargin = 20;
constexpr int kBottomHeight = 96;
constexpr int kSideWidth = 56;
constexpr int kSideHeight = 144;
constexpr int kPreviewWidth = 760;
constexpr int kPreviewHeight = 430;
constexpr int kControlHeight = 40;
constexpr int kFieldHeight = 62;

const QPixmap &settingsBodyPixmap();
const QPixmap &settingsLegsPixmap();
const QPixmap &settingsQrPixmap();
void prewarmSettingsResources();

class RoundedPanel : public QWidget
{
public:
    explicit RoundedPanel(QWidget *parent = nullptr,
                          int radius = 12,
                          const QColor &bg = QColor("#ffffff"))
        : QWidget(parent), m_radius(radius), m_bg(bg)
    {
        setAttribute(Qt::WA_TranslucentBackground);

        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(28);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(0, 0, 0, 18));
        setGraphicsEffect(shadow);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QRectF r = rect();
        r.adjust(0.5, 0.5, -0.5, -0.5);

        QPainterPath path;
        path.addRoundedRect(r, m_radius, m_radius);
        painter.fillPath(path, m_bg);
    }

private:
    int m_radius;
    QColor m_bg;
};

class IconButton : public QPushButton
{
public:
    explicit IconButton(const QString &iconPath,
                        const QString &tipText,
                        const QString &buttonText = QString(),
                        QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFixedSize(44, 38);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        if (!iconPath.isEmpty()) {
            setIcon(QIcon(iconPath));
            setIconSize(QSize(20, 20));
        }
        setText(buttonText);
        setToolTip(tipText);
        setStyleSheet(R"(
            QPushButton {
                border: none;
                background: transparent;
                border-radius: 10px;
                color: #333333;
                font-size: 13px;
                font-weight: 700;
            }
            QPushButton:hover { background: #f0f0f0; }
            QPushButton:pressed { background: #e6e6e6; }
        )");
    }
};

class CanvasPreview : public QLabel
{
public:
    explicit CanvasPreview(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(420, 260);
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet("background: transparent; border: none;");
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setText(QString::fromUtf8(u8"请打开SAI并创建画布"));
        setCursor(Qt::ArrowCursor);
    }

    void setImage(const QImage &image)
    {
        const bool sizeChanged = m_image.size() != image.size();
        m_image = image;
        if (sizeChanged) {
            m_zoom = 1.0;
            m_offset = QPointF();
        }
        setToolTip(QString::fromUtf8(u8"滚轮缩放，按住左键拖动图片"));
        setCursor(Qt::OpenHandCursor);
        update();
    }

    void clearImage()
    {
        m_image = QImage();
        m_offset = QPointF();
        m_dragging = false;
        setText(QString::fromUtf8(u8"请打开SAI并创建画布"));
        setToolTip(QString());
        setCursor(Qt::ArrowCursor);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_image.isNull()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::TextAntialiasing, true);

            QFont hintFont = font();
            hintFont.setPixelSize(16);
            hintFont.setWeight(QFont::DemiBold);
            painter.setFont(hintFont);
            painter.setPen(QColor(96, 103, 115));
            painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, text());
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(imageRect(), m_image);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (m_image.isNull()) {
            event->ignore();
            return;
        }

        const double step = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        m_zoom = std::clamp(m_zoom * step, 0.25, 8.0);
        update();
        event->accept();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_image.isNull() && event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging) {
            const QPoint delta = event->pos() - m_lastMousePos;
            m_offset += QPointF(delta);
            m_lastMousePos = event->pos();
            update();
            event->accept();
            return;
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragging && event->button() == Qt::LeftButton) {
            m_dragging = false;
            setCursor(m_image.isNull() ? Qt::ArrowCursor : Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

private:
    QRectF imageRect() const
    {
        if (m_image.isNull() || width() <= 0 || height() <= 0) {
            return QRectF();
        }

        const double fit = std::min(width() / static_cast<double>(m_image.width()),
                                    height() / static_cast<double>(m_image.height()));
        const double scale = fit * m_zoom;
        const QSizeF target(std::max(1.0, m_image.width() * scale),
                            std::max(1.0, m_image.height() * scale));
        const QPointF topLeft((width() - target.width()) / 2.0 + m_offset.x(),
                              (height() - target.height()) / 2.0 + m_offset.y());
        return QRectF(topLeft, target);
    }

    QImage m_image;
    double m_zoom = 1.0;
    QPointF m_offset;
    QPoint m_lastMousePos;
    bool m_dragging = false;
};

class SettingsCharacterView : public QWidget
{
public:
    explicit SettingsCharacterView(QWidget *parent = nullptr)
        : QWidget(parent),
          m_body(settingsBodyPixmap()),
          m_legs(settingsLegsPixmap())
    {
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter.drawPixmap(toViewRect(QRectF(-168.0, 20.0, 750.0, 376.0)),
                           m_body, QRectF(m_body.rect()));
        painter.drawPixmap(toViewRect(QRectF(-27.4643, -22.3952, 635.195, 555.524)),
                           m_legs, QRectF(m_legs.rect()));
    }

private:
    double viewScale() const
    {
        return width() / 594.0;
    }

    QRectF toViewRect(const QRectF &rect) const
    {
        const double scale = viewScale();
        return QRectF(rect.x() * scale, rect.y() * scale,
                      rect.width() * scale, rect.height() * scale);
    }

    QPixmap m_body;
    QPixmap m_legs;
};

class CaptureContext : public QObject
{
};

QLabel *makeFieldTitle(const QString &title, QWidget *parent)
{
    auto *label = new QLabel(title, parent);
    label->setFixedHeight(15);
    label->setStyleSheet(R"(
        QLabel {
            color: #333333;
            background: transparent;
            font-weight: 700;
            font-size: 12px;
        }
    )");
    return label;
}

QWidget *makeField(const QString &title, QWidget *control, QWidget *parent, QLabel **titleLabel = nullptr)
{
    auto *field = new QWidget(parent);
    field->setAttribute(Qt::WA_TranslucentBackground);
    field->setStyleSheet("background: transparent;");
    field->setFixedHeight(kFieldHeight);
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    control->setFixedHeight(kControlHeight);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *label = makeFieldTitle(title, field);
    if (titleLabel) {
        *titleLabel = label;
    }

    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    layout->addWidget(label);
    layout->addWidget(control);
    return field;
}

QString controlStyle()
{
    return R"(
        QComboBox, QSpinBox, QLineEdit {
            background: rgba(247, 247, 247, 215);
            border: none;
            border-radius: 6px;
            padding-left: 12px;
            padding-right: 12px;
            color: #333333;
            font-size: 13px;
        }
        QComboBox:hover, QSpinBox:hover, QLineEdit:hover {
            background: rgba(240, 240, 240, 235);
        }
        QComboBox QAbstractItemView {
            color: #222222;
            background-color: #ffffff;
            selection-color: #ffffff;
            selection-background-color: #333333;
            border: 1px solid #d8d8d8;
            outline: none;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            width: 0;
            border: none;
        }
    )";
}

QString buttonStyle()
{
    return R"(
        QPushButton {
            background: #242424;
            border: none;
            border-radius: 8px;
            color: #ffffff;
            font-size: 14px;
            font-weight: 700;
            padding-left: 16px;
            padding-right: 16px;
        }
        QPushButton:hover { background: #111111; }
        QPushButton:pressed { background: #000000; }
        QPushButton#recordButton[recording="true"] {
            background: #e13d3d;
            color: #ffffff;
            font-weight: 700;
        }
        QPushButton#recordButton[recording="true"]:hover { background: #c92d2d; }
    )";
}

QString utilityButtonStyle()
{
    return R"(
        QPushButton {
            background: rgba(247, 247, 247, 215);
            border: none;
            border-radius: 6px;
            color: #222222;
            font-size: 13px;
            padding-left: 12px;
            padding-right: 12px;
        }
        QPushButton:hover { background: rgba(240, 240, 240, 235); }
        QPushButton:pressed { background: rgba(232, 232, 232, 245); }
    )";
}

QString canvasLabel(const CanvasInfo &canvas)
{
    const QString name = canvas.name.isEmpty()
        ? QString("Canvas %1x%2").arg(canvas.width).arg(canvas.height)
        : canvas.name;
    return QString("%1 (%2x%3)").arg(name).arg(canvas.width).arg(canvas.height);
}

QString canvasFramePrefix(const CanvasInfo &canvas)
{
    return canvas.name.isEmpty()
        ? QString("Canvas_%1x%2").arg(canvas.width).arg(canvas.height)
        : canvas.name;
}

QString safePathName(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    value.replace(QRegularExpression(R"(\s+)"), "_");
    value.replace(QRegularExpression(R"(_+)"), "_");
    while (value.startsWith('_')) value.remove(0, 1);
    while (value.endsWith('_')) value.chop(1);
    return value.isEmpty() ? QString("frame") : value;
}

struct LanguageOption {
    const char *code;
    const char *label;
};

const LanguageOption kLanguageOptions[] = {
    {"zh_CN", "中文"},
    {"ja_JP", "日本語"},
    {"en_US", "English"},
    {"fr_FR", "Français"},
    {"hi_IN", "हिन्दी"},
};

QString languageFamily(const QString &language)
{
    if (language.startsWith("ja")) return "ja";
    if (language.startsWith("en")) return "en";
    if (language.startsWith("fr")) return "fr";
    if (language.startsWith("hi")) return "hi";
    return "zh";
}

const QPixmap &settingsBodyPixmap()
{
    static const QPixmap pixmap(QStringLiteral(":/settings/character_body.png"));
    return pixmap;
}

const QPixmap &settingsLegsPixmap()
{
    static const QPixmap pixmap(QStringLiteral(":/settings/character_legs.png"));
    return pixmap;
}

const QPixmap &settingsQrPixmap()
{
    static const QPixmap pixmap(QStringLiteral(":/settings/qr.png"));
    return pixmap;
}

void prewarmSettingsResources()
{
    settingsBodyPixmap();
    settingsLegsPixmap();
    settingsQrPixmap();

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(18);
    QFontMetrics metrics(font);

    QImage scratch(900, 180, QImage::Format_ARGB32_Premultiplied);
    scratch.fill(Qt::transparent);
    QPainter painter(&scratch);
    painter.setFont(font);
    painter.setPen(Qt::black);

    int y = 24;
    for (const auto &option : kLanguageOptions) {
        const QString text = QString::fromUtf8(option.label);
        metrics.horizontalAdvance(text);
        painter.drawText(QPoint(8, y), text);
        y += 28;
    }
    painter.end();
}

} // namespace

SaiRecordWidget::SaiRecordWidget(QWidget *parent)
    : QWidget(parent)
{
    m_settings = new AppSettings(this);
    m_saiManager = new SAIManager(this);
    m_videoWriter = new VideoSequenceWriter(this);
    m_captureThread = new QThread(this);
    m_captureContext = new CaptureContext();
    m_captureContext->moveToThread(m_captureThread);
    m_captureThread->start();

    setupUi();

    QTimer::singleShot(200, this, []() {
        prewarmSettingsResources();
    });

    connect(m_videoWriter, &VideoSequenceWriter::segmentCreated,
            this, &SaiRecordWidget::updateFrameCount);
    connect(m_videoWriter, &VideoSequenceWriter::errorOccurred,
            this, [this](const QString &message) { setStatusText(message); });

    startSaiPolling();
}

SaiRecordWidget::~SaiRecordWidget()
{
    stopRecording();
    stopSaiPolling();
    if (m_captureContext) {
        m_captureContext->deleteLater();
        m_captureContext = nullptr;
    }
    if (m_captureThread) {
        m_captureThread->quit();
        m_captureThread->wait(3000);
    }
}

void SaiRecordWidget::setupUi()
{
    resize(1265, 768);
    setMinimumSize(960, 620);
    setWindowTitle(QStringLiteral("由白记录 - SAI"));


    QFont font("Microsoft YaHei UI");
    font.setPixelSize(12);
    setFont(font);

    setObjectName("saiRecordRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
        QWidget#saiRecordRoot { background: #f6f6f6; }
        QToolTip {
            color: #f8fafc;
            background-color: #20242b;
            border: 1px solid #596273;
            border-radius: 4px;
            padding: 5px;
        }
    )");
    if (qApp) {
        qApp->setStyleSheet(qApp->styleSheet() + R"(
            QToolTip {
                color: #f8fafc;
                background-color: #20242b;
                border: 1px solid #596273;
                border-radius: 4px;
                padding: 5px;
            }
        )");
    }

    setupSideBar();
    setupCenterPanel();
    setupBottomBar();
    setupVersionHintPopup();
    applyLanguage();
    updatePanelGeometry();
}

void SaiRecordWidget::setupSideBar()
{
    m_sideBar = new RoundedPanel(this, 0, QColor("#ffffff"));

    m_homeBtn = new IconButton(":/icons/home.svg", QString(), QString(), m_sideBar);
    m_quickExportBtn = new IconButton(":/icons/expand.svg", QString(), QString(), m_sideBar);
    m_settingsBtn = new IconButton(":/icons/settings.svg", QString(), QString(), m_sideBar);

    auto *layout = new QVBoxLayout(m_sideBar);
    layout->setContentsMargins(6, 8, 6, 8);
    layout->setSpacing(2);
    layout->addWidget(m_homeBtn);
    layout->addWidget(m_quickExportBtn);
    layout->addWidget(m_settingsBtn);

    connect(m_quickExportBtn, &QPushButton::clicked,
            this, &SaiRecordWidget::quickExportVideo);
    connect(m_settingsBtn, &QPushButton::clicked,
            this, &SaiRecordWidget::openSettingsDialog);
}

void SaiRecordWidget::setupCenterPanel()
{
    m_centerPanel = new QWidget(this);
    m_centerPanel->setAttribute(Qt::WA_TranslucentBackground);
    m_centerPanel->setStyleSheet("background: transparent;");

    m_versionStatusLabel = new QLabel(this);
    m_versionStatusLabel->hide();

    m_canvasPreview = new CanvasPreview(m_centerPanel);

    auto *layout = new QVBoxLayout(m_centerPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_canvasPreview, 1);
}

void SaiRecordWidget::setupBottomBar()
{
    m_bottomBar = new RoundedPanel(this, 0, QColor(255, 255, 255, 220));

    m_versionOverrideCombo = new QComboBox(m_bottomBar);
    m_versionOverrideCombo->setStyleSheet(controlStyle());
    m_versionOverrideCombo->setToolTip(QString());
    m_versionOverrideCombo->installEventFilter(this);
    populateVersionOverride();

    m_imageSizeLimitSpin = new QSpinBox(m_bottomBar);
    m_imageSizeLimitSpin->setRange(100, 10000);
    m_imageSizeLimitSpin->setSingleStep(100);
    m_imageSizeLimitSpin->setSuffix(" px");
    m_imageSizeLimitSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_imageSizeLimitSpin->setValue(m_settings->imageSizeLimit());
    m_imageSizeLimitSpin->setStyleSheet(controlStyle());
    m_imageSizeLimitSpin->setToolTip("Image limit");

    m_canvasCombo = new QComboBox(m_bottomBar);
    m_canvasCombo->setMinimumWidth(150);
    m_canvasCombo->setMaximumWidth(210);
    m_canvasCombo->setStyleSheet(controlStyle());
    m_canvasCombo->setToolTip("Canvas");
    m_canvasCombo->addItem("未检测到画布");

    m_recordBtn = new QPushButton("开始录制", m_bottomBar);
    m_recordBtn->setObjectName("recordButton");
    m_recordBtn->setCursor(Qt::PointingHandCursor);
    m_recordBtn->setMinimumWidth(104);
    m_recordBtn->setFixedHeight(40);
    m_recordBtn->setStyleSheet(buttonStyle());
    m_recordBtn->setToolTip("开始录制");

    m_frameCountLabel = makeFieldTitle("已记录 0 帧", m_bottomBar);
    m_frameCountLabel->setAlignment(Qt::AlignCenter);

    auto *recordField = new QWidget(m_bottomBar);
    recordField->setAttribute(Qt::WA_TranslucentBackground);
    recordField->setStyleSheet("background: transparent;");
    recordField->setFixedHeight(kFieldHeight);
    recordField->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_recordBtn->setFixedHeight(kControlHeight);
    m_recordBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *recordLayout = new QVBoxLayout(recordField);
    recordLayout->setContentsMargins(0, 0, 0, 0);
    recordLayout->setSpacing(7);
    recordLayout->addWidget(m_frameCountLabel);
    recordLayout->addWidget(m_recordBtn);

    m_framesPathEdit = new QLineEdit(m_settings->framesPath(), m_bottomBar);
    m_framesPathEdit->setPlaceholderText("Select folder for saving frames...");
    m_framesPathEdit->setFixedHeight(40);
    m_framesPathEdit->setStyleSheet(controlStyle());
    m_framesPathEdit->setToolTip("Frames path");

    m_browseBtn = new QPushButton("选择", m_bottomBar);
    m_browseBtn->setMinimumWidth(64);
    m_browseBtn->setFixedHeight(40);
    m_browseBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    m_browseBtn->setStyleSheet(utilityButtonStyle());
    m_browseBtn->setToolTip("选择输出目录");

    auto *pathControl = new QWidget(m_bottomBar);
    pathControl->setAttribute(Qt::WA_TranslucentBackground);
    pathControl->setFixedHeight(40);
    pathControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *pathLayout = new QHBoxLayout(pathControl);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    pathLayout->addWidget(m_framesPathEdit, 1);
    pathLayout->addWidget(m_browseBtn, 1);

    auto *layout = new QHBoxLayout(m_bottomBar);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(14);
    auto *versionField = makeField("当前使用SAI版本", m_versionOverrideCombo, m_bottomBar);
    auto *sizeField = makeField("录制尺寸（宽为基准）", m_imageSizeLimitSpin, m_bottomBar);
    auto *canvasField = makeField("录制画布", m_canvasCombo, m_bottomBar);
    canvasField->setMinimumWidth(170);
    canvasField->setMaximumWidth(230);
    auto *outputField = makeField("输出目录", pathControl, m_bottomBar);

    layout->addWidget(versionField, 24, Qt::AlignTop);
    layout->addWidget(sizeField, 14, Qt::AlignTop);
    layout->addWidget(canvasField, 0, Qt::AlignTop);
    layout->addWidget(recordField, 12, Qt::AlignTop);
    layout->addWidget(outputField, 30, Qt::AlignTop);

    connect(m_versionOverrideCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_settings->setSaiVersionOverride(index - 1);
                if (!m_isRecording) {
                    m_saiManager->disconnect();
                    m_saiDisconnectTime.invalidate();
                    checkSaiProcess();
                }
            });
    connect(m_imageSizeLimitSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
                m_settings->setImageSizeLimit(value);
                updateCanvasPreview();
            });
    connect(m_canvasCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                m_settings->setSaiCanvasIndex(index);
                restoreFrameCountFromOutput();
                updateCanvasPreview();
            });
    connect(m_framesPathEdit, &QLineEdit::textChanged,
            this, [this](const QString &path) {
                m_settings->setFramesPath(path);
            });
    connect(m_framesPathEdit, &QLineEdit::editingFinished,
            this, &SaiRecordWidget::restoreFrameCountFromOutput);
    connect(m_browseBtn, &QPushButton::clicked,
            this, &SaiRecordWidget::chooseFramesPath);
    connect(m_recordBtn, &QPushButton::clicked,
            this, &SaiRecordWidget::toggleRecording);
}

void SaiRecordWidget::setupVersionHintPopup()
{
    if (m_versionHintPopup) {
        return;
    }

    m_versionHintPopup = new QLabel(QString::fromUtf8(u8"一般保持自动选择就好"), this);
    m_versionHintPopup->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_versionHintPopup->setAlignment(Qt::AlignCenter);
    m_versionHintPopup->setStyleSheet(R"(
        QLabel {
            color: #ffffff;
            background: rgba(32, 36, 43, 232);
            border: 1px solid rgba(255, 255, 255, 42);
            border-radius: 8px;
            padding: 8px 12px;
            font-size: 12px;
            font-weight: 600;
        }
    )");

    m_versionHintOpacity = new QGraphicsOpacityEffect(m_versionHintPopup);
    m_versionHintOpacity->setOpacity(0.0);
    m_versionHintPopup->setGraphicsEffect(m_versionHintOpacity);
    m_versionHintPopup->hide();
}

void SaiRecordWidget::populateVersionOverride()
{
    m_versionOverrideCombo->clear();
    m_versionOverrideCombo->addItem("自动选择");

    for (const auto &version : m_saiManager->availableVersions()) {
        m_versionOverrideCombo->addItem(QString::fromStdString(version));
    }

    const int saved = m_settings->saiVersionOverride();
    const int index = std::clamp(saved + 1, 0, m_versionOverrideCombo->count() - 1);
    m_versionOverrideCombo->setCurrentIndex(index);
}

bool SaiRecordWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_versionOverrideCombo) {
        if (event->type() == QEvent::Enter) {
            showVersionHintPopup();
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
            hideVersionHintPopup();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void SaiRecordWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePanelGeometry();
    updateCanvasPreview();
}

void SaiRecordWidget::closeEvent(QCloseEvent *event)
{
    stopRecording();
    m_settings->sync();
    QWidget::closeEvent(event);
}

void SaiRecordWidget::updatePanelGeometry()
{
    if (!m_sideBar || !m_bottomBar || !m_centerPanel) {
        return;
    }

    const int w = width();
    const int h = height();

    m_sideBar->setGeometry(20, h / 2 - kSideHeight / 2, kSideWidth, kSideHeight);
    m_bottomBar->setGeometry(20, h - kBottomHeight - kBottomMargin, w - 40, kBottomHeight);

    m_centerPanel->setGeometry(0, 0, w, h);
    m_centerPanel->lower();
    m_sideBar->raise();
    m_bottomBar->raise();
    if (m_versionHintPopup && m_versionHintPopup->isVisible()) {
        m_versionHintPopup->raise();
    }
}

void SaiRecordWidget::showVersionHintPopup()
{
    if (!m_versionOverrideCombo || !m_versionHintPopup || !m_versionHintOpacity) {
        return;
    }

    if (m_versionHintAnimation) {
        m_versionHintAnimation->stop();
        m_versionHintAnimation->deleteLater();
        m_versionHintAnimation = nullptr;
    }

    m_versionHintPopup->adjustSize();
    const QSize finalSize = m_versionHintPopup->sizeHint();
    const QPoint comboTopLeft = m_versionOverrideCombo->mapTo(this, QPoint(0, 0));
    const int endX = comboTopLeft.x() + (m_versionOverrideCombo->width() - finalSize.width()) / 2;
    const int endY = comboTopLeft.y() - finalSize.height() - 10;
    const QRect endRect(endX, endY, finalSize.width(), finalSize.height());
    const QRect startRect(endRect.center().x() - finalSize.width() / 4,
                          comboTopLeft.y() - finalSize.height() / 2,
                          finalSize.width() / 2,
                          finalSize.height() / 2);

    m_versionHintPopup->setGeometry(startRect);
    m_versionHintPopup->show();
    m_versionHintPopup->raise();

    auto *geometryAnimation = new QPropertyAnimation(m_versionHintPopup, "geometry");
    geometryAnimation->setDuration(190);
    geometryAnimation->setStartValue(startRect);
    geometryAnimation->setEndValue(endRect);
    geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *opacityAnimation = new QPropertyAnimation(m_versionHintOpacity, "opacity");
    opacityAnimation->setDuration(150);
    opacityAnimation->setStartValue(0.0);
    opacityAnimation->setEndValue(1.0);
    opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_versionHintAnimation = new QParallelAnimationGroup(this);
    m_versionHintAnimation->addAnimation(geometryAnimation);
    m_versionHintAnimation->addAnimation(opacityAnimation);
    connect(m_versionHintAnimation, &QParallelAnimationGroup::finished,
            this, [this]() {
                if (m_versionHintAnimation) {
                    m_versionHintAnimation->deleteLater();
                    m_versionHintAnimation = nullptr;
                }
            });
    m_versionHintAnimation->start();
}

void SaiRecordWidget::hideVersionHintPopup()
{
    if (!m_versionHintPopup || !m_versionHintOpacity || !m_versionHintPopup->isVisible()) {
        return;
    }

    if (m_versionHintAnimation) {
        m_versionHintAnimation->stop();
        m_versionHintAnimation->deleteLater();
        m_versionHintAnimation = nullptr;
    }

    const QRect startRect = m_versionHintPopup->geometry();
    const QRect endRect(startRect.center().x() - startRect.width() / 4,
                        startRect.bottom() - startRect.height() / 2,
                        startRect.width() / 2,
                        startRect.height() / 2);

    auto *geometryAnimation = new QPropertyAnimation(m_versionHintPopup, "geometry");
    geometryAnimation->setDuration(120);
    geometryAnimation->setStartValue(startRect);
    geometryAnimation->setEndValue(endRect);
    geometryAnimation->setEasingCurve(QEasingCurve::InCubic);

    auto *opacityAnimation = new QPropertyAnimation(m_versionHintOpacity, "opacity");
    opacityAnimation->setDuration(100);
    opacityAnimation->setStartValue(m_versionHintOpacity->opacity());
    opacityAnimation->setEndValue(0.0);
    opacityAnimation->setEasingCurve(QEasingCurve::InCubic);

    m_versionHintAnimation = new QParallelAnimationGroup(this);
    m_versionHintAnimation->addAnimation(geometryAnimation);
    m_versionHintAnimation->addAnimation(opacityAnimation);
    connect(m_versionHintAnimation, &QParallelAnimationGroup::finished,
            this, [this]() {
                if (m_versionHintPopup) {
                    m_versionHintPopup->hide();
                }
                if (m_versionHintAnimation) {
                    m_versionHintAnimation->deleteLater();
                    m_versionHintAnimation = nullptr;
                }
            });
    m_versionHintAnimation->start();
}

QString SaiRecordWidget::uiText(const QString &key) const
{
    const QString lang = languageFamily(m_settings ? m_settings->language() : QString());

    if (lang == "ja") {
        if (key == "title") return QString::fromUtf8(u8"由白レコード - SAI");
        if (key == "homeShort") return QString::fromUtf8(u8"主");
        if (key == "exportShort") return QString::fromUtf8(u8"出");
        if (key == "settingsShort") return QString::fromUtf8(u8"設");
        if (key == "homeTip") return QString::fromUtf8(u8"メイン表示");
        if (key == "exportTip") return QString::fromUtf8(u8"動画をクイック書き出し");
        if (key == "settingsTip") return QString::fromUtf8(u8"設定");
        if (key == "version") return QString::fromUtf8(u8"使用中のSAIバージョン");
        if (key == "size") return QString::fromUtf8(u8"録画サイズ（幅基準）");
        if (key == "canvas") return QString::fromUtf8(u8"録画キャンバス");
        if (key == "output") return QString::fromUtf8(u8"出力フォルダー");
        if (key == "browse") return QString::fromUtf8(u8"選択");
        if (key == "start") return QString::fromUtf8(u8"録画開始");
        if (key == "stop") return QString::fromUtf8(u8"録画停止");
        if (key == "autoSelect") return QString::fromUtf8(u8"自動選択");
        if (key == "noCanvas") return QString::fromUtf8(u8"キャンバス未検出");
        if (key == "frames") return QString::fromUtf8(u8"記録済み %1 フレーム");
        if (key == "settingsTitle") return QString::fromUtf8(u8"設定");
        if (key == "language") return QString::fromUtf8(u8"言語");
        if (key == "official") return QString::fromUtf8(u8"公式サイトへ");
        if (key == "close") return QString::fromUtf8(u8"閉じる");
        if (key == "footer") return QString::fromUtf8(u8"GPL ライセンス<br>作者：由白<br>オープンソース：<a href=\"https://gitee.com/komariyui/sai-record-youbai\">https://gitee.com/komariyui/sai-record-youbai</a>");
    } else if (lang == "en") {
        if (key == "title") return "Youbai Record - SAI";
        if (key == "homeShort") return "H";
        if (key == "exportShort") return "E";
        if (key == "settingsShort") return "S";
        if (key == "homeTip") return "Home";
        if (key == "exportTip") return "Quick export video";
        if (key == "settingsTip") return "Settings";
        if (key == "version") return "SAI version";
        if (key == "size") return "Recording size (width)";
        if (key == "canvas") return "Canvas";
        if (key == "output") return "Frames path";
        if (key == "browse") return "Choose";
        if (key == "start") return "Start Recording";
        if (key == "stop") return "Stop Recording";
        if (key == "autoSelect") return "Auto Detect";
        if (key == "noCanvas") return "No canvas detected";
        if (key == "frames") return "Recorded %1 frames";
        if (key == "settingsTitle") return "Settings";
        if (key == "language") return "Language";
        if (key == "official") return "Official Website";
        if (key == "close") return "Close";
        if (key == "footer") return "GPL License<br>Author: Youbai<br>Open source: <a href=\"https://gitee.com/komariyui/sai-record-youbai\">https://gitee.com/komariyui/sai-record-youbai</a>";
    } else if (lang == "fr") {
        if (key == "title") return "Youbai Record - SAI";
        if (key == "homeShort") return "A";
        if (key == "exportShort") return "V";
        if (key == "settingsShort") return "R";
        if (key == "homeTip") return "Accueil";
        if (key == "exportTip") return "Export vidéo rapide";
        if (key == "settingsTip") return "Réglages";
        if (key == "version") return "Version de SAI";
        if (key == "size") return "Taille d'enregistrement (largeur)";
        if (key == "canvas") return "Canevas";
        if (key == "output") return "Dossier des images";
        if (key == "browse") return "Choisir";
        if (key == "start") return "Démarrer";
        if (key == "stop") return "Arrêter";
        if (key == "autoSelect") return "Détection auto";
        if (key == "noCanvas") return "Aucun canevas détecté";
        if (key == "frames") return "%1 images enregistrées";
        if (key == "settingsTitle") return "Réglages";
        if (key == "language") return "Langue";
        if (key == "official") return "Site officiel";
        if (key == "close") return "Fermer";
        if (key == "footer") return "Licence GPL<br>Auteur : Youbai<br>Open source : <a href=\"https://gitee.com/komariyui/sai-record-youbai\">https://gitee.com/komariyui/sai-record-youbai</a>";
    } else if (lang == "hi") {
        if (key == "title") return QString::fromUtf8(u8"Youbai Record - SAI");
        if (key == "homeShort") return QString::fromUtf8(u8"मुख");
        if (key == "exportShort") return QString::fromUtf8(u8"निर");
        if (key == "settingsShort") return QString::fromUtf8(u8"सेट");
        if (key == "homeTip") return QString::fromUtf8(u8"मुख्य दृश्य");
        if (key == "exportTip") return QString::fromUtf8(u8"वीडियो तुरंत निर्यात करें");
        if (key == "settingsTip") return QString::fromUtf8(u8"सेटिंग्स");
        if (key == "version") return QString::fromUtf8(u8"SAI संस्करण");
        if (key == "size") return QString::fromUtf8(u8"रिकॉर्डिंग आकार (चौड़ाई)");
        if (key == "canvas") return QString::fromUtf8(u8"कैनवास");
        if (key == "output") return QString::fromUtf8(u8"फ़्रेम फ़ोल्डर");
        if (key == "browse") return QString::fromUtf8(u8"चुनें");
        if (key == "start") return QString::fromUtf8(u8"रिकॉर्ड शुरू");
        if (key == "stop") return QString::fromUtf8(u8"रिकॉर्ड रोकें");
        if (key == "autoSelect") return QString::fromUtf8(u8"स्वतः चुनें");
        if (key == "noCanvas") return QString::fromUtf8(u8"कैनवास नहीं मिला");
        if (key == "frames") return QString::fromUtf8(u8"%1 फ़्रेम रिकॉर्ड हुए");
        if (key == "settingsTitle") return QString::fromUtf8(u8"सेटिंग्स");
        if (key == "language") return QString::fromUtf8(u8"भाषा");
        if (key == "official") return QString::fromUtf8(u8"आधिकारिक साइट");
        if (key == "close") return QString::fromUtf8(u8"बंद करें");
        if (key == "footer") return QString::fromUtf8(u8"GPL लाइसेंस<br>लेखक: Youbai<br>ओपन सोर्स: <a href=\"https://gitee.com/komariyui/sai-record-youbai\">https://gitee.com/komariyui/sai-record-youbai</a>");
    }

    if (key == "title") return QString::fromUtf8(u8"由白记录 - SAI");
    if (key == "homeShort") return QString::fromUtf8(u8"主");
    if (key == "exportShort") return QString::fromUtf8(u8"导");
    if (key == "settingsShort") return QString::fromUtf8(u8"设");
    if (key == "homeTip") return QString::fromUtf8(u8"主视图");
    if (key == "exportTip") return QString::fromUtf8(u8"快速导出视频");
    if (key == "settingsTip") return QString::fromUtf8(u8"设置");
    if (key == "version") return QString::fromUtf8(u8"当前使用SAI版本");
    if (key == "size") return QString::fromUtf8(u8"录制尺寸（宽为基准）");
    if (key == "canvas") return QString::fromUtf8(u8"录制画布");
    if (key == "output") return QString::fromUtf8(u8"输出目录");
    if (key == "browse") return QString::fromUtf8(u8"选择");
    if (key == "start") return QString::fromUtf8(u8"开始录制");
    if (key == "stop") return QString::fromUtf8(u8"停止录制");
    if (key == "autoSelect") return QString::fromUtf8(u8"自动选择");
    if (key == "noCanvas") return QString::fromUtf8(u8"未检测到画布");
    if (key == "frames") return QString::fromUtf8(u8"已记录 %1 帧");
    if (key == "settingsTitle") return QString::fromUtf8(u8"设置");
    if (key == "language") return QString::fromUtf8(u8"语言");
    if (key == "official") return QString::fromUtf8(u8"跳转官网");
    if (key == "close") return QString::fromUtf8(u8"关闭");
    if (key == "footer") return QString::fromUtf8(u8"GPL 协议<br>作者：由白<br>开源地址：<a href=\"https://gitee.com/komariyui/sai-record-youbai\">https://gitee.com/komariyui/sai-record-youbai</a>");
    return key;
}

void SaiRecordWidget::applyLanguage()
{
    setWindowTitle(uiText("title"));

    if (m_homeBtn) {
        m_homeBtn->setText(QString());
        m_homeBtn->setToolTip(uiText("homeTip"));
    }
    if (m_quickExportBtn) {
        m_quickExportBtn->setText(QString());
        m_quickExportBtn->setToolTip(uiText("exportTip"));
    }
    if (m_settingsBtn) {
        m_settingsBtn->setText(QString());
        m_settingsBtn->setToolTip(uiText("settingsTip"));
    }

    QList<QLabel *> titles;
    if (m_versionTitleLabel && m_sizeTitleLabel && m_canvasTitleLabel && m_outputTitleLabel) {
        titles << m_versionTitleLabel << m_sizeTitleLabel << m_canvasTitleLabel << m_outputTitleLabel;
    } else if (m_bottomBar) {
        for (QLabel *label : m_bottomBar->findChildren<QLabel *>()) {
            if (label != m_frameCountLabel) {
                titles << label;
            }
        }
    }
    if (titles.size() >= 4) {
        titles[0]->setText(uiText("version"));
        titles[1]->setText(uiText("size"));
        titles[2]->setText(uiText("canvas"));
        titles[3]->setText(uiText("output"));
    }

    if (m_browseBtn) {
        m_browseBtn->setText(uiText("browse"));
        m_browseBtn->setToolTip(uiText("browse"));
    }
    if (m_recordBtn) {
        m_recordBtn->setText(m_isRecording ? uiText("stop") : uiText("start"));
        m_recordBtn->setToolTip(m_isRecording ? uiText("stop") : uiText("start"));
    }
    if (m_versionOverrideCombo && m_versionOverrideCombo->count() > 0) {
        m_versionOverrideCombo->setItemText(0, uiText("autoSelect"));
    }
    if (m_canvasCombo && m_canvasCombo->count() == 1 && !m_canvasCombo->currentData().isValid()) {
        m_canvasCombo->setItemText(0, uiText("noCanvas"));
    }
    updateFrameCount();
}

void SaiRecordWidget::startSaiPolling()
{
    if (m_saiPollTimer) {
        return;
    }

    m_saiPollTimer = new QTimer(this);
    connect(m_saiPollTimer, &QTimer::timeout, this, &SaiRecordWidget::checkSaiProcess);
    m_saiPollTimer->start(1000);
    checkSaiProcess();
}

void SaiRecordWidget::stopSaiPolling()
{
    if (!m_saiPollTimer) {
        return;
    }

    m_saiPollTimer->stop();
    m_saiPollTimer->deleteLater();
    m_saiPollTimer = nullptr;
}

void SaiRecordWidget::checkSaiProcess()
{
    if (m_saiManager->isConnected()) {
        if (m_saiManager->isProcessRunning()) {
            if (m_isRecording) {
                requestPreviewFrame();
            } else {
                refreshCanvasList();
            }
        } else {
            if (m_isRecording) {
                onTargetWindowLost();
            } else {
                onSaiDisconnected();
            }
        }
        return;
    }

    if (m_isRecording) {
        onTargetWindowLost();
        return;
    }

    if (m_saiDisconnectTime.isValid() && m_saiDisconnectTime.elapsed() < 3000) {
        return;
    }
    m_saiDisconnectTime.invalidate();

    if (m_saiManager->detectAndConnect(m_settings->saiVersionOverride())) {
        onSaiConnected();
    } else {
        setStatusText("SAI: Not detected");
        m_canvasCombo->clear();
        m_canvasCombo->addItem("未检测到画布");
        static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
    }
}

void SaiRecordWidget::onSaiConnected()
{
    setStatusText("SAI: " + QString::fromStdString(m_saiManager->detectedVersion()), true);
    refreshCanvasList();
}

void SaiRecordWidget::onSaiDisconnected()
{
    m_saiManager->disconnect();
    setStatusText("SAI: Not detected");
    m_canvasCombo->clear();
    m_canvasCombo->addItem("未检测到画布");
    static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
    m_saiDisconnectTime.start();
}

void SaiRecordWidget::refreshCanvasList()
{
    if (!m_saiManager->isConnected()) {
        return;
    }

    const QVariant selected = m_canvasCombo->currentData();
    const auto canvases = m_saiManager->refreshCanvases();

    QSignalBlocker blocker(m_canvasCombo);
    m_canvasCombo->clear();

    if (canvases.empty()) {
        m_canvasCombo->addItem("未检测到画布");
        static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
        return;
    }

    int selectedIndex = -1;
    for (int i = 0; i < static_cast<int>(canvases.size()); ++i) {
        const auto &canvas = canvases[i];
        const QVariant ptr = QVariant::fromValue(static_cast<qulonglong>(canvas.selfPtr));
        m_canvasCombo->addItem(canvasLabel(canvas), ptr);
        if (selected.isValid() && ptr == selected) {
            selectedIndex = i;
        }
    }

    if (selectedIndex < 0) {
        selectedIndex = std::clamp(m_settings->saiCanvasIndex(), 0, m_canvasCombo->count() - 1);
    }
    m_canvasCombo->setCurrentIndex(selectedIndex);
    blocker.unblock();

    restoreFrameCountFromOutput();
    updateCanvasPreview();
}

void SaiRecordWidget::restoreFrameCountFromOutput()
{
    if (!m_videoWriter || !m_saiManager || !m_saiManager->isConnected() || m_isRecording) {
        return;
    }

    const QString framesPath = m_framesPathEdit->text().trimmed();
    if (framesPath.isEmpty()) {
        return;
    }

    const int idx = m_canvasCombo->currentIndex();
    if (idx < 0 || !m_canvasCombo->currentData().isValid()) {
        return;
    }

    auto canvases = m_saiManager->refreshCanvases();
    if (idx >= static_cast<int>(canvases.size())) {
        return;
    }

    m_videoWriter->setOutputDir(framesPath);
    m_videoWriter->setFramePrefix(canvasFramePrefix(canvases[idx]));
    m_videoWriter->restoreFrameCountFromOutput();
    updateFrameCount();
}

void SaiRecordWidget::updateCanvasPreview()
{
    if (!m_canvasPreview || !m_saiManager || !m_saiManager->isConnected()) {
        return;
    }
    if (m_isRecording) {
        return;
    }

    const int idx = m_canvasCombo->currentIndex();
    if (idx < 0 || !m_canvasCombo->currentData().isValid()) {
        static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
        return;
    }

    try {
        auto canvases = m_saiManager->refreshCanvases();
        if (idx >= static_cast<int>(canvases.size())) {
            static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
            return;
        }

        CanvasInfo canvas = canvases[idx];
        const int mapLevel = m_saiManager->engine()->getMapLevelForSize(
            canvas, m_imageSizeLimitSpin->value());
        const int previewLimit = std::max(m_canvasPreview->width(), m_canvasPreview->height());
        QImage preview = m_saiManager->engine()->getCanvasImage(canvas, mapLevel, previewLimit);

        if (preview.isNull()) {
            static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
            return;
        }

        static_cast<CanvasPreview *>(m_canvasPreview)->setImage(preview);
    } catch (const std::exception &e) {
        static_cast<CanvasPreview *>(m_canvasPreview)->clearImage();
        m_canvasPreview->setToolTip(QString("Preview Error: %1").arg(e.what()));
    }
}

void SaiRecordWidget::chooseFramesPath()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        "Select Frames Directory",
        m_framesPathEdit->text().isEmpty() ? QDir::homePath() : m_framesPathEdit->text());
    if (!dir.isEmpty()) {
        m_framesPathEdit->setText(dir);
        restoreFrameCountFromOutput();
    }
}

#if 0
void SaiRecordWidget::openSettingsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(uiText("settingsTitle"));
    dialog.setWindowTitle(QStringLiteral("设置"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);
    dialog.setWindowTitle(uiText("settingsTitle"));
    dialog.setStyleSheet(R"(
        QDialog {
            background: #ffffff;
        }
        QLabel {
            color: #222222;
            font-size: 13px;
        }
        QComboBox {
            min-height: 34px;
            border: 1px solid #d7dbe2;
            border-radius: 6px;
            padding-left: 10px;
            padding-right: 10px;
            color: #222222;
            background: #ffffff;
        }
        QComboBox QAbstractItemView {
            color: #222222;
            background: #ffffff;
            selection-color: #ffffff;
            selection-background-color: #333333;
        }
        QPushButton {
            min-height: 34px;
            border: none;
            border-radius: 6px;
            padding-left: 14px;
            padding-right: 14px;
            color: #ffffff;
            background: #242424;
            font-weight: 700;
        }
        QPushButton:hover { background: #111111; }
        QPushButton:pressed { background: #000000; }
    )");

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 16);
    layout->setSpacing(14);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(12);

    auto *languageCombo = new QComboBox(&dialog);
    for (const auto &option : kLanguageOptions) {
        languageCombo->addItem(QString::fromUtf8(option.label), QString::fromLatin1(option.code));
    }
    const int savedIndex = languageCombo->findData(m_settings->language());
    languageCombo->setCurrentIndex(savedIndex >= 0 ? savedIndex : 0);
    form->addRow(QStringLiteral("语言"), languageCombo);
    QLabel *languageLabel = nullptr;
    if (auto *item = form->itemAt(0, QFormLayout::LabelRole)) {
        languageLabel = qobject_cast<QLabel *>(item->widget());
    }
    if (languageLabel) {
        languageLabel->setText(uiText("language"));
        languageLabel->setStyleSheet("background: transparent; border: none; color: #222222;");
    }
    layout->addLayout(form);

    auto *officialButton = new QPushButton(QStringLiteral("跳转官网"), &dialog);
    officialButton->setCursor(Qt::PointingHandCursor);
    officialButton->setText(uiText("official"));
    layout->addWidget(officialButton);

    auto *footer = new QLabel(&dialog);
    footer->setWordWrap(true);
    footer->setOpenExternalLinks(true);
    footer->setTextFormat(Qt::RichText);
    footer->setText(QStringLiteral(
        "GPL 协议<br>"
        "作者：由白<br>"
        "开源地址：<a href=\"https://gitee.com/komariyui/sai-record-youbai\">"
        "https://gitee.com/komariyui/sai-record-youbai</a>"));
    footer->setStyleSheet(R"(
        QLabel {
            color: #555555;
            background: #f7f7f7;
            border-radius: 6px;
            padding: 10px;
            line-height: 1.35;
        }
        QLabel a { color: #1f5fbf; }
    )");
    footer->setText(uiText("footer"));
    layout->addWidget(footer);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    if (auto *closeButton = buttons->button(QDialogButtonBox::Close)) {
        closeButton->setText(uiText("close"));
    }
    layout->addWidget(buttons);

    connect(languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, &dialog, languageCombo, languageLabel, officialButton, footer, buttons](int index) {
                m_settings->setLanguage(languageCombo->itemData(index).toString());
                m_settings->sync();
                applyLanguage();
                dialog.setWindowTitle(uiText("settingsTitle"));
                if (languageLabel) {
                    languageLabel->setText(uiText("language"));
                }
                officialButton->setText(uiText("official"));
                footer->setText(uiText("footer"));
                if (auto *closeButton = buttons->button(QDialogButtonBox::Close)) {
                    closeButton->setText(uiText("close"));
                }
            });
    connect(officialButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.wuhupoo.cn")));
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.exec();
}

#endif

void SaiRecordWidget::openSettingsDialog()
{
    const double uiScale = 1.5;
    const auto s = [uiScale](int value) { return qRound(value * uiScale); };

    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8(u8"由白记录"));
    dialog.setModal(true);
    dialog.setFixedSize(s(920), s(560));
    dialog.setWindowFlag(Qt::FramelessWindowHint, true);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setStyleSheet(R"(
        QDialog { background: transparent; }
        QLabel {
            background: transparent;
            color: #3d3d3d;
            font-family: "Microsoft YaHei UI";
        }
        QToolButton#settingsLanguageButton {
            min-height: 46px;
            border: none;
            border-radius: 2px;
            padding-left: 20px;
            padding-right: 20px;
            color: #222222;
            background: #f6f6f6;
            font-size: 18px;
            text-align: left;
        }
        QToolButton#settingsLanguageButton:hover {
            background: #eeeeee;
        }
        QToolButton#settingsLanguageButton::menu-indicator {
            image: none;
            width: 0px;
            height: 0px;
            subcontrol-origin: padding;
            subcontrol-position: right center;
        }
        QMenu#settingsLanguageMenu {
            color: #222222;
            background: #ffffff;
            border: 1px solid #eeeeee;
            padding: 4px;
        }
        QMenu#settingsLanguageMenu::item {
            min-height: 28px;
            padding: 5px 18px;
            background: transparent;
        }
        QMenu#settingsLanguageMenu::item:selected {
            color: #222222;
            background: #f1f1f1;
        }
        QPushButton {
            border: none;
            color: #333333;
            background: transparent;
        }
        QPushButton:hover { background: rgba(0, 0, 0, 8); }
        QPushButton:pressed { background: rgba(0, 0, 0, 14); }
    )");

    auto *background = new QWidget(&dialog);
    background->setGeometry(dialog.rect());
    background->setObjectName("settingsBackground");
    background->setStyleSheet("QWidget#settingsBackground { background: transparent; }");

    auto *card = new RoundedPanel(&dialog, 0, QColor(255, 255, 255, 236));
    card->setGeometry(s(78), s(128), s(700), s(310));

    auto *title = new QLabel(QString::fromUtf8(u8"由白记录 ——BAI Record"), card);
    title->setGeometry(s(34), s(34), s(330), s(26));
    title->setStyleSheet(R"(
        QLabel {
            color: #414141;
            font-size: 27px;
            font-weight: 800;
        }
    )");

    auto *versionLabel = new QLabel(QStringLiteral("version 0.1.0"), card);
    versionLabel->setGeometry(s(35), s(61), s(170), s(16));
    versionLabel->setStyleSheet("color: #9a9a9a; font-size: 16px;");

//    auto *licenseLabel = new QLabel(QString::fromUtf8(u8"License GPL V3  https://gitee.com/komariyui/sai-record-youbai.git"), card);
//    licenseLabel->setGeometry(s(35), s(78), s(360), s(16));
//    licenseLabel->setStyleSheet("color: #d0d0d0; font-size: 13px;");

    auto *contactPanel = new QWidget(card);
    contactPanel->setGeometry(s(34), s(96), s(310), s(114));
    contactPanel->setStyleSheet("background: rgba(248, 248, 248, 210);");

    auto *contact1 = new QLabel(QString::fromUtf8(u8"问题反馈请添加QQ群：      175750535"), contactPanel);
    contact1->setGeometry(s(16), s(32), s(180), s(14));
    contact1->setStyleSheet("color: #777777; font-size: 15px;");

    auto *contact2 = new QLabel(QStringLiteral("邮箱反馈： tinyupsnow@foxmail.com"), contactPanel);
    contact2->setGeometry(s(16), s(52), s(205), s(14));
    contact2->setStyleSheet("color: #777777; font-size: 15px;");

    auto *logLink = new QLabel(QString::fromUtf8(u8"改进建议请进群讨论~！"), contactPanel);
    logLink->setGeometry(s(16), s(78), s(190), s(14));
    logLink->setStyleSheet("color: #d7d7d7; font-size: 15px;");

    auto *qr = new QLabel(contactPanel);
    qr->setGeometry(s(206), s(18), s(84), s(84));
    qr->setPixmap(settingsQrPixmap().scaled(qr->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    qr->setAlignment(Qt::AlignCenter);

    auto *languageLabel = new QLabel(card);
    languageLabel->setGeometry(s(34), s(231), s(160), s(18));
    languageLabel->setStyleSheet("color: #4f4f4f; font-size: 16px;");
    languageLabel->setText(uiText("language"));

    auto *languageButton = new QToolButton(card);
    languageButton->setObjectName("settingsLanguageButton");
    languageButton->setGeometry(s(34), s(251), s(208), s(31));
    languageButton->setCursor(Qt::PointingHandCursor);
    languageButton->setPopupMode(QToolButton::InstantPopup);
    languageButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    languageButton->setArrowType(Qt::NoArrow);

    auto *languageMenu = new QMenu(languageButton);
    languageMenu->setObjectName("settingsLanguageMenu");
    languageButton->setMenu(languageMenu);

    auto updateLanguageButtonText = [this, languageButton]() {
        const QString current = m_settings->language();
        for (const auto &option : kLanguageOptions) {
            if (current == QString::fromLatin1(option.code)) {
                languageButton->setText(QString::fromUtf8(option.label));
                return;
            }
        }
        languageButton->setText(QString::fromUtf8(kLanguageOptions[0].label));
    };
    updateLanguageButtonText();

    for (const auto &option : kLanguageOptions) {
        auto *action = languageMenu->addAction(QString::fromUtf8(option.label));
        action->setData(QString::fromLatin1(option.code));
    }





    auto *character = new SettingsCharacterView(&dialog);
    character->setGeometry(s(320), s(42), s(594), s(610));
    character->raise();

    auto *closeButton = new QPushButton(QStringLiteral("×"), &dialog);
    closeButton->setGeometry(s(762), s(312), s(38), s(38));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setToolTip(uiText("close"));
    closeButton->setStyleSheet(R"(
        QPushButton {
            background: rgba(255, 255, 255, 218);
            color: #333333;
            border: 1px solid rgba(0, 0, 0, 10);
            font-size: 39px;
            font-weight: 300;
        }
        QPushButton:hover { background: rgba(255, 255, 255, 245); }
        QPushButton:pressed { background: rgba(238, 238, 238, 245); }
    )");
    closeButton->raise();

    connect(languageMenu, &QMenu::triggered,
            this, [this, &dialog, languageLabel, closeButton, updateLanguageButtonText](QAction *action) {
                m_settings->setLanguage(action->data().toString());
                m_settings->sync();
                applyLanguage();
                dialog.setWindowTitle(uiText("settingsTitle"));
                languageLabel->setText(uiText("language"));
                closeButton->setToolTip(uiText("close"));
                updateLanguageButtonText();
            });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void SaiRecordWidget::quickExportVideo()
{
    if (m_exportInProgress) {
        return;
    }
    if (m_isRecording) {
        setStatusText("请先停止录制再导出视频");
        return;
    }

    const QString framesPath = m_framesPathEdit->text().trimmed();
    if (framesPath.isEmpty()) {
        setStatusText("请先选择输出目录");
        return;
    }
    if (!m_saiManager->isConnected()) {
        setStatusText("请先打开 SAI 并选择录制画布");
        return;
    }

    const int idx = m_canvasCombo->currentIndex();
    if (idx < 0 || !m_canvasCombo->currentData().isValid()) {
        setStatusText("请先选择录制画布");
        return;
    }

    auto canvases = m_saiManager->refreshCanvases();
    if (idx >= static_cast<int>(canvases.size())) {
        setStatusText("画布不可用，请刷新后重试");
        return;
    }

    const QString framePrefix = canvasFramePrefix(canvases[idx]);
    VideoExporter scanExporter;
    scanExporter.setFramesDir(framesPath);
    scanExporter.setFramePrefix(framePrefix);
    const int totalFrames = scanExporter.scanTotalFrames();
    if (totalFrames <= 0) {
        setStatusText("当前画布没有可导出的帧");
        return;
    }

    const QString outputPath = QDir(framesPath).absoluteFilePath(
        QString("%1_export.mp4").arg(safePathName(framePrefix)));
    const int fps = std::max(1, m_settings->exportFps());

    m_exportInProgress = true;
    if (m_quickExportBtn) {
        m_quickExportBtn->setEnabled(false);
    }
    setStatusText(QString("正在导出视频：%1 帧").arg(totalFrames), true);

    auto *progressDialog = new QProgressDialog("正在导出视频...", "取消", 0, totalFrames, this);
    progressDialog->setWindowTitle("快速导出视频");
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setValue(0);
    progressDialog->show();

    auto cancelRequested = std::make_shared<std::atomic_bool>(false);
    connect(progressDialog, &QProgressDialog::canceled, this, [cancelRequested]() {
        cancelRequested->store(true);
    });

    QPointer<SaiRecordWidget> self(this);
    QPointer<QProgressDialog> progress(progressDialog);
    auto *exportThread = QThread::create([self, progress, cancelRequested, framesPath, outputPath, framePrefix, fps]() {
        VideoExporter exporter;
        exporter.setFramesDir(framesPath);
        exporter.setOutputPath(outputPath);
        exporter.setFramePrefix(framePrefix);
        exporter.setContainer("mp4");
        exporter.setCodec("libx264");
        exporter.setFps(fps);
        exporter.setTimeLimit(0);

        const bool success = exporter.exportVideo([self, progress, cancelRequested](int current, int total) {
            if (self && progress) {
                QMetaObject::invokeMethod(self, [progress, current, total]() {
                    if (!progress) {
                        return;
                    }
                    progress->setMaximum(total);
                    progress->setValue(current);
                    progress->setLabelText(QString("正在导出视频... %1/%2 帧").arg(current).arg(total));
                }, Qt::QueuedConnection);
            }
            return !cancelRequested->load();
        });
        const QString error = exporter.lastError();
        if (!self) {
            return;
        }

        QMetaObject::invokeMethod(self, [self, progress, success, outputPath, error]() {
            if (!self) {
                return;
            }
            self->m_exportInProgress = false;
            if (self->m_quickExportBtn) {
                self->m_quickExportBtn->setEnabled(true);
            }
            if (progress) {
                progress->setValue(progress->maximum());
                progress->close();
                progress->deleteLater();
            }

            if (success) {
                self->setStatusText(QString("导出完成：%1").arg(outputPath), true);
                QMessageBox::information(self, "快速导出视频", QString("导出完成：\n%1").arg(outputPath));
            } else {
                const QString message = error.isEmpty()
                    ? QString("导出失败")
                    : QString("导出失败：\n%1").arg(error);
                self->setStatusText(message, false);
                QMessageBox::warning(self, "快速导出视频", message);
            }
            return;
            self->setStatusText(success
                ? QString("导出完成：%1").arg(outputPath)
                : QString("导出失败，请确认 FFmpeg 可用"), success);
        }, Qt::QueuedConnection);
    });

    connect(exportThread, &QThread::finished, exportThread, &QObject::deleteLater);
    exportThread->start();
}

void SaiRecordWidget::toggleRecording()
{
    if (m_isRecording) {
        stopRecording();
    } else {
        startRecording();
    }
}

void SaiRecordWidget::startRecording()
{
    if (m_isRecording) {
        return;
    }

    const QString framesPath = m_framesPathEdit->text().trimmed();
    if (framesPath.isEmpty()) {
        const QString message = QString::fromUtf8(u8"请先选择输出目录");
        setStatusText(message);
        if (m_framesPathEdit) {
            m_framesPathEdit->setFocus(Qt::OtherFocusReason);
            m_framesPathEdit->selectAll();
        }
        QMessageBox::warning(this, QString::fromUtf8(u8"开始录制"), message);
        return;
    }

    if (!m_saiManager->isConnected()) {
        setStatusText("请先打开 SAI 并等待检测完成");
        return;
    }

    const int idx = m_canvasCombo->currentIndex();
    if (idx < 0 || !m_canvasCombo->currentData().isValid()) {
        setStatusText("请选择录制画布");
        return;
    }

    auto canvases = m_saiManager->refreshCanvases();
    if (idx >= static_cast<int>(canvases.size())) {
        setStatusText("画布不可用，请刷新后重试");
        return;
    }
    m_currentCanvas = canvases[idx];

    m_targetHwnd = WindowHelper::findMainWindowByPid(m_saiManager->pid());
    if (!m_targetHwnd) {
        setStatusText("找不到 SAI 窗口");
        return;
    }

    QDir().mkpath(framesPath);
    m_videoWriter->setOutputDir(framesPath);
    m_videoWriter->setFramePrefix(canvasFramePrefix(m_currentCanvas));
    m_videoWriter->restoreFrameCountFromOutput();
    m_videoWriter->setRecording(true);

    m_mouseListener = new MouseStrokeListener(this);
    if (!m_mouseListener->startListening(m_targetHwnd)) {
        setStatusText("安装鼠标监听失败");
        delete m_mouseListener;
        m_mouseListener = nullptr;
        m_videoWriter->setRecording(false);
        return;
    }

    connect(m_mouseListener, &MouseStrokeListener::strokeEnded,
            this, &SaiRecordWidget::onStrokeEnded);
    connect(m_mouseListener, &MouseStrokeListener::targetWindowLost,
            this, &SaiRecordWidget::onTargetWindowLost);

    m_windowCheckTimer = new QTimer(this);
    connect(m_windowCheckTimer, &QTimer::timeout, this, [this]() {
        if (m_mouseListener && !m_mouseListener->isTargetWindowAlive()) {
            onTargetWindowLost();
        }
    });
    m_windowCheckTimer->start(500);

    m_isRecording = true;
    m_lastSaiFrame = QImage();
    ++m_captureGeneration;
    m_captureInFlight = false;
    m_previewCaptureInFlight = false;
    m_pendingCaptureRequests = 0;
    m_recordBtn->setText("停止录制");
    m_recordBtn->setToolTip("停止录制");
    m_recordBtn->setProperty("recording", true);
    m_recordBtn->style()->unpolish(m_recordBtn);
    m_recordBtn->style()->polish(m_recordBtn);
    applyLanguage();
    updateFrameCount();
    setStatusText("Recording: " + canvasLabel(m_currentCanvas), true);
}

void SaiRecordWidget::stopRecording()
{
    if (!m_isRecording && !m_mouseListener && !m_windowCheckTimer) {
        return;
    }

    if (m_mouseListener) {
        m_mouseListener->stopListening();
        delete m_mouseListener;
        m_mouseListener = nullptr;
    }
    if (m_windowCheckTimer) {
        m_windowCheckTimer->stop();
        m_windowCheckTimer->deleteLater();
        m_windowCheckTimer = nullptr;
    }

    m_videoWriter->close();
    m_videoWriter->setRecording(false);
    m_isRecording = false;
    m_targetHwnd = nullptr;
    ++m_captureGeneration;
    m_captureInFlight = false;
    m_previewCaptureInFlight = false;
    m_pendingCaptureRequests = 0;
    m_recordBtn->setText("开始录制");
    m_recordBtn->setToolTip("开始录制");
    m_recordBtn->setProperty("recording", false);
    m_recordBtn->style()->unpolish(m_recordBtn);
    m_recordBtn->style()->polish(m_recordBtn);
    applyLanguage();
    updateFrameCount();
    setStatusText(QString("Recording stopped. Total frames: %1").arg(m_videoWriter->frameCount()), true);
}

void SaiRecordWidget::requestCaptureFrame()
{
    if (!m_isRecording || !m_videoWriter->isRecording() ||
        !m_saiManager->isConnected() || !m_captureContext) {
        return;
    }
    if (m_captureInFlight) {
        ++m_pendingCaptureRequests;
        return;
    }

    SAIEngine *engine = m_saiManager->engine();
    if (!engine) {
        return;
    }

    m_captureInFlight = true;
    const int generation = m_captureGeneration;
    const CanvasInfo canvas = m_currentCanvas;
    const int maxSize = m_imageSizeLimitSpin->value();
    auto *context = static_cast<CaptureContext *>(m_captureContext);

    QMetaObject::invokeMethod(context, [this, engine, canvas, maxSize, generation]() {
        QImage frame;
        QString error;

        try {
            if (!engine->canvasExists(canvas)) {
                error = "Canvas lost, stopping recording";
            } else {
                const int mapLevel = engine->getMapLevelForSize(canvas, maxSize);
                frame = engine->getCanvasImage(canvas, mapLevel, maxSize);
                if (frame.isNull()) {
                    error = "Captured canvas image is empty";
                }
            }
        } catch (const std::exception &e) {
            error = QString("Capture error: %1").arg(e.what());
        }

        QMetaObject::invokeMethod(this, [this, generation, frame, error]() {
            handleCapturedFrame(generation, frame, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SaiRecordWidget::handleCapturedFrame(int generation, const QImage &frame, const QString &error)
{
    m_captureInFlight = false;

    if (generation != m_captureGeneration || !m_isRecording) {
        return;
    }

    if (!error.isEmpty()) {
        setStatusText(error);
        if (error.startsWith("Canvas lost")) {
            stopRecording();
        } else if (m_pendingCaptureRequests > 0 && m_isRecording) {
            --m_pendingCaptureRequests;
            requestCaptureFrame();
        }
        return;
    }

    if (!frame.isNull()) {
        m_lastSaiFrame = frame;
        static_cast<CanvasPreview *>(m_canvasPreview)->setImage(frame);
    }

    if (!m_videoWriter->writeFrame(frame)) {
        setStatusText("写入帧失败");
        if (m_pendingCaptureRequests > 0 && m_isRecording) {
            --m_pendingCaptureRequests;
            requestCaptureFrame();
        }
        return;
    }
    updateFrameCount();

    if (m_pendingCaptureRequests > 0 && m_isRecording) {
        --m_pendingCaptureRequests;
        requestCaptureFrame();
    }
}

void SaiRecordWidget::requestPreviewFrame()
{
    if (m_previewCaptureInFlight || !m_isRecording ||
        !m_saiManager->isConnected() || !m_captureContext) {
        return;
    }

    SAIEngine *engine = m_saiManager->engine();
    if (!engine) {
        return;
    }

    m_previewCaptureInFlight = true;
    const int generation = m_captureGeneration;
    const CanvasInfo canvas = m_currentCanvas;
    const int previewLimit = std::max(m_canvasPreview->width(), m_canvasPreview->height());
    auto *context = static_cast<CaptureContext *>(m_captureContext);

    QMetaObject::invokeMethod(context, [this, engine, canvas, previewLimit, generation]() {
        QImage frame;
        QString error;

        try {
            if (!engine->canvasExists(canvas)) {
                error = "Canvas lost, stopping recording";
            } else {
                const int mapLevel = engine->getMapLevelForSize(canvas, previewLimit);
                frame = engine->getCanvasImage(canvas, mapLevel, previewLimit);
                if (frame.isNull()) {
                    error = "Preview image is empty";
                }
            }
        } catch (const std::exception &e) {
            error = QString("Preview error: %1").arg(e.what());
        }

        QMetaObject::invokeMethod(this, [this, generation, frame, error]() {
            handlePreviewFrame(generation, frame, error);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void SaiRecordWidget::handlePreviewFrame(int generation, const QImage &frame, const QString &error)
{
    m_previewCaptureInFlight = false;

    if (generation != m_captureGeneration || !m_isRecording) {
        return;
    }

    if (!error.isEmpty()) {
        if (error.startsWith("Canvas lost")) {
            setStatusText(error);
            stopRecording();
        }
        return;
    }

    if (!frame.isNull()) {
        static_cast<CanvasPreview *>(m_canvasPreview)->setImage(frame);
    }
}

void SaiRecordWidget::onStrokeEnded()
{
    requestCaptureFrame();
}

void SaiRecordWidget::onTargetWindowLost()
{
    setStatusText("Target window lost, stopping recording");
    stopRecording();
}

void SaiRecordWidget::setStatusText(const QString &text, bool ok)
{
    if (m_versionStatusLabel) {
        m_versionStatusLabel->setText(text);
        m_versionStatusLabel->setProperty("ok", ok);
    }
    if (m_canvasPreview) {
        m_canvasPreview->setToolTip(text);
    }
    if (m_recordBtn) {
        m_recordBtn->setToolTip(text);
    }
    if (m_quickExportBtn) {
        m_quickExportBtn->setToolTip(text);
    }
}

void SaiRecordWidget::updateFrameCount()
{
    if (m_frameCountLabel && m_videoWriter) {
        m_frameCountLabel->setText(uiText("frames").arg(m_videoWriter->frameCount()));
        return;
        m_frameCountLabel->setText(QString("已记录 %1 帧").arg(m_videoWriter->frameCount()));
    }
}
