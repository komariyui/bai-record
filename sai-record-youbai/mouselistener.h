#ifndef MOUSELISTENER_H
#define MOUSELISTENER_H

#include <QObject>
#include <QRect>
#include <QThread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// ============================================================
// MouseStrokeListener - 全局鼠标钩子，检测笔画（点击释放）
// 当鼠标在目标窗口内按下并释放时，发出 strokeEnded 信号
// ============================================================
class MouseStrokeListener : public QObject {
    Q_OBJECT
public:
    explicit MouseStrokeListener(QObject* parent = nullptr);
    ~MouseStrokeListener() override;

    // 开始监听 (设置目标窗口句柄)
    bool startListening(HWND targetHwnd, const QRect& region = QRect());

    // 停止监听
    void stopListening();

    bool isListening() const { return m_listening; }

    // 设置目标窗口
    void setTargetWindow(HWND hwnd) { m_targetHwnd = hwnd; }
    void setCaptureRegion(const QRect& region) { m_captureRegion = region; }

    // 检查窗口是否还存在
    bool isTargetWindowAlive() const;

    QString targetWindowTitle() const;

signals:
    // 笔画结束 (鼠标在目标窗口内按下后释放)
    void strokeEnded();

    // 目标窗口丢失
    void targetWindowLost();

    // 错误
    void errorOccurred(const QString& message);

private:
    static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

    HWND m_targetHwnd = nullptr;
    DWORD m_targetPid = 0;
    QRect m_captureRegion;  // 空矩形表示整个窗口
    HHOOK m_hook = nullptr;
    HHOOK m_keyboardHook = nullptr;
    bool m_mouseDownInTarget = false;
    bool m_ctrlDown = false;
    bool m_listening = false;

    // 静态实例指针（用于在回调中访问）
    static MouseStrokeListener* s_instance;
};

// ============================================================
// WindowHelper - 窗口查找辅助
// ============================================================
class WindowHelper {
public:
    // 通过 PID 获取主窗口句柄
    static HWND findMainWindowByPid(DWORD pid);

    // 通过 PID 获取所有可见窗口
    static std::vector<HWND> findWindowsByPid(DWORD pid);

    // 获取窗口标题
    static QString getWindowTitle(HWND hwnd);

    // 获取窗口矩形
    static QRect getWindowRect(HWND hwnd);

    // 获取当前鼠标位置所在的窗口
    static HWND getWindowAtPoint(int x, int y);

    // 获取窗口的根父窗口
    static HWND getRootWindow(HWND hwnd);

    // 判断窗口是否存活
    static bool isWindowAlive(HWND hwnd);

    // 判断点是否在矩形内
    static bool pointInRect(const QRect& rect, int x, int y) {
        return x >= rect.left() && x <= rect.right() &&
               y >= rect.top() && y <= rect.bottom();
    }
};

#endif // MOUSELISTENER_H
