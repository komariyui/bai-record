#include "mouselistener.h"
#include <QDebug>
#include <QWindow>

MouseStrokeListener* MouseStrokeListener::s_instance = nullptr;

// ============================================================
// MouseStrokeListener
// ============================================================
MouseStrokeListener::MouseStrokeListener(QObject* parent)
    : QObject(parent) {}

MouseStrokeListener::~MouseStrokeListener() {
    stopListening();
}

LRESULT CALLBACK MouseStrokeListener::mouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_instance) {
        auto* msll = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        switch (wParam) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            // 检查点击是否在目标窗口内
            if (s_instance->m_targetHwnd) {
                HWND hwndAtPoint = WindowHelper::getWindowAtPoint(msll->pt.x, msll->pt.y);
                HWND rootWnd = WindowHelper::getRootWindow(hwndAtPoint);
                HWND targetRoot = WindowHelper::getRootWindow(s_instance->m_targetHwnd);
                DWORD pointPid = 0;
                GetWindowThreadProcessId(hwndAtPoint, &pointPid);

                if ((rootWnd == targetRoot && targetRoot != nullptr) ||
                    (pointPid != 0 && pointPid == s_instance->m_targetPid)) {
                    // 如果在区域内（如果有设置区域限制）
                    if (s_instance->m_captureRegion.isNull()) {
                        s_instance->m_mouseDownInTarget = true;
                    } else {
                        s_instance->m_mouseDownInTarget =
                            WindowHelper::pointInRect(s_instance->m_captureRegion,
                                                       msll->pt.x, msll->pt.y);
                    }
                }
            }
            break;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            if (s_instance->m_mouseDownInTarget) {
                s_instance->m_mouseDownInTarget = false;
                auto *listener = s_instance;
                QMetaObject::invokeMethod(listener, [listener]() {
                    emit listener->strokeEnded();
                }, Qt::QueuedConnection);
            }
            break;
        }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseStrokeListener::keyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && s_instance) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const DWORD vk = kb->vkCode;
        const bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) {
            s_instance->m_ctrlDown = isKeyDown ? true : (isKeyUp ? false : s_instance->m_ctrlDown);
        }

        if (isKeyUp && vk == 'Z' && s_instance->m_ctrlDown) {
            HWND foreground = GetForegroundWindow();
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(foreground, &foregroundPid);

            if (foregroundPid != 0 && foregroundPid == s_instance->m_targetPid) {
                auto *listener = s_instance;
                QMetaObject::invokeMethod(listener, [listener]() {
                    emit listener->strokeEnded();
                }, Qt::QueuedConnection);
            }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool MouseStrokeListener::startListening(HWND targetHwnd, const QRect& region) {
    stopListening();

    m_targetHwnd = targetHwnd;
    m_targetPid = 0;
    GetWindowThreadProcessId(m_targetHwnd, &m_targetPid);
    m_captureRegion = region;
    m_mouseDownInTarget = false;
    m_ctrlDown = false;

    s_instance = this;

    // 设置低级鼠标钩子
    m_hook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
    if (!m_hook) {
        s_instance = nullptr;
        emit errorOccurred(QString("Failed to install mouse hook: %1").arg(GetLastError()));
        return false;
    }

    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!m_keyboardHook) {
        stopListening();
        emit errorOccurred(QString("Failed to install keyboard hook: %1").arg(GetLastError()));
        return false;
    }

    m_listening = true;

    // 检查目标窗口是否存活
    if (!isTargetWindowAlive()) {
        stopListening();
        emit targetWindowLost();
        return false;
    }

    return true;
}

void MouseStrokeListener::stopListening() {
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
    if (m_keyboardHook) {
        UnhookWindowsHookEx(m_keyboardHook);
        m_keyboardHook = nullptr;
    }
    m_listening = false;
    m_targetPid = 0;
    m_ctrlDown = false;
    s_instance = nullptr;
}

bool MouseStrokeListener::isTargetWindowAlive() const {
    return WindowHelper::isWindowAlive(m_targetHwnd);
}

QString MouseStrokeListener::targetWindowTitle() const {
    return WindowHelper::getWindowTitle(m_targetHwnd);
}

// ============================================================
// WindowHelper
// ============================================================

// 回调数据结构
struct EnumWindowsData {
    DWORD targetPid;
    std::vector<HWND> windows;
};

static BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumWindowsData*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->targetPid && IsWindowVisible(hwnd)) {
        data->windows.push_back(hwnd);
    }
    return TRUE;
}

std::vector<HWND> WindowHelper::findWindowsByPid(DWORD pid) {
    EnumWindowsData data{};
    data.targetPid = pid;
    EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&data));
    return data.windows;
}

HWND WindowHelper::findMainWindowByPid(DWORD pid) {
    auto windows = findWindowsByPid(pid);
    if (windows.empty()) return nullptr;

    // 找出最顶层的主窗口（无父窗口或有最小化等属性的）
    for (auto hwnd : windows) {
        if (GetWindow(hwnd, GW_OWNER) == nullptr) {
            LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            if (style & WS_CAPTION) {
                return hwnd;
            }
        }
    }

    return windows.front();
}

QString WindowHelper::getWindowTitle(HWND hwnd) {
    if (!hwnd) return {};
    WCHAR title[256];
    GetWindowTextW(hwnd, title, 256);
    return QString::fromWCharArray(title);
}

QRect WindowHelper::getWindowRect(HWND hwnd) {
    RECT rect{};
    if (GetWindowRect(hwnd, &rect)) {
        return QRect(rect.left, rect.top,
                     rect.right - rect.left,
                     rect.bottom - rect.top);
    }
    return {};
}

HWND WindowHelper::getWindowAtPoint(int x, int y) {
    POINT pt{x, y};
    return WindowFromPoint(pt);
}

HWND WindowHelper::getRootWindow(HWND hwnd) {
    if (!hwnd) return nullptr;
    return GetAncestor(hwnd, GA_ROOT);
}

bool WindowHelper::isWindowAlive(HWND hwnd) {
    return hwnd != nullptr && IsWindow(hwnd);
}
