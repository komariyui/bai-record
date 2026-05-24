#ifndef SAIENGINE_H
#define SAIENGINE_H

#include "remoteptr.h"
#include <QObject>
#include <QImage>
#include <QString>
#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <cstdint>

// ============================================================
// CanvasInfo - 画布信息
// ============================================================
struct CanvasInfo {
    uintptr_t selfPtr;          // 画布结构体在目标进程中的地址
    int32_t  id = 0;            // 画布ID (SAIv2)
    int32_t  width = 0;
    int32_t  height = 0;
    QString   name;
    QString   shortPath;
};

// ============================================================
// SAIEngine - SAI 进程引擎基类
// ============================================================
class SAIEngine {
public:
    virtual ~SAIEngine() = default;

    // 获取画布列表
    virtual std::vector<CanvasInfo> getCanvasList() = 0;

    // 检查画布是否还存在
    virtual bool canvasExists(const CanvasInfo& canvas) = 0;

    // 读取画布图像 (BGRA格式, map_level 指定 mipmap 级别)
    virtual QImage getCanvasImage(const CanvasInfo& canvas, int mapLevel = 0, int maxSize = 0) = 0;

    // 获取适合指定尺寸的 mipmap 级别
    virtual int getMapLevelForSize(const CanvasInfo& canvas, int maxSize) = 0;

    virtual std::string versionName() const = 0;
    virtual int mapCount() const = 0;
};

using SAIEnginePtr = std::unique_ptr<SAIEngine>;

// ============================================================
// SAI 版本注册与工厂
// ============================================================
using SAIFactoryFunc = std::function<SAIEnginePtr(HANDLE hProcess, const ProcessReader& reader, uintptr_t baseAddress)>;

class SAIVersionRegistry {
public:
    static SAIVersionRegistry& instance();

    void registerVersion(const std::string& exeHash,
                         const std::string& versionName,
                         SAIFactoryFunc factory);

    SAIFactoryFunc findFactory(const std::string& exeHash) const;

    std::string detectVersion(HANDLE hProcess) const;
    SAIEnginePtr createEngine(HANDLE hProcess, const std::string& exeHash,
                              const ProcessReader& reader, uintptr_t baseAddress) const;
    SAIEnginePtr createEngineByIndex(HANDLE hProcess, int versionIndex,
                                     const ProcessReader& reader, uintptr_t baseAddress) const;

    // 列出所有已注册的版本名
    std::vector<std::string> versionNames() const;

private:
    struct Entry {
        std::string hash;
        std::string name;
        SAIFactoryFunc factory;
    };
    std::vector<Entry> m_entries;
};

// ============================================================
// SAIv1 引擎 (32位, sai.exe)
// ============================================================
class SAIv1Engine : public SAIEngine {
public:
    SAIv1Engine(HANDLE hProcess, const ProcessReader& reader,
                uintptr_t baseAddress, uintptr_t sessionOffset);

    std::vector<CanvasInfo> getCanvasList() override;
    bool canvasExists(const CanvasInfo& canvas) override;
    QImage getCanvasImage(const CanvasInfo& canvas, int mapLevel = 0, int maxSize = 0) override;
    int getMapLevelForSize(const CanvasInfo& canvas, int maxSize) override;
    int mapCount() const override { return 1; }
    std::string versionName() const override { return m_versionName; }
    void setVersionName(const std::string& name) { m_versionName = name; }

protected:
    std::string m_versionName;
    HANDLE m_hProcess;
    const ProcessReader& m_reader;
    uintptr_t m_baseAddress;
    uintptr_t m_sessionOffset;

    // SAIv1 结构体偏移
    struct Layout {
        // SAIPixelHeap
        static constexpr uint32_t pixelheap_stride_x = 0x4;
        static constexpr uint32_t pixelheap_stride_y = 0x8;
        static constexpr uint32_t pixelheap_data = 0xC;
        // SAICanvas
        static constexpr uint32_t canvas_next     = 0x004;
        static constexpr uint32_t canvas_pixelheap = 0x030;
        static constexpr uint32_t canvas_lower_pad_x = 0x114;
        static constexpr uint32_t canvas_lower_pad_y = 0x118;
        static constexpr uint32_t canvas_width     = 0x124;
        static constexpr uint32_t canvas_height    = 0x128;
        static constexpr uint32_t canvas_padded_w  = 0x12C;
        static constexpr uint32_t canvas_padded_h  = 0x130;
        static constexpr uint32_t canvas_short_path = 0x4D0;
        static constexpr uint32_t canvas_name       = 0x5D8;
        // SAISession
        static constexpr uint32_t session_canvas_list = 0x4C;
    };

    // 读取画布结构体
    CanvasInfo readCanvasInfo(uintptr_t canvasPtr) const;
};

// ============================================================
// SAIv2 引擎 (64位, sai2.exe)
// ============================================================
class SAIv2Engine : public SAIEngine {
public:
    SAIv2Engine(HANDLE hProcess, const ProcessReader& reader,
                uintptr_t baseAddress, uintptr_t canvasListOffset);

    std::vector<CanvasInfo> getCanvasList() override;
    bool canvasExists(const CanvasInfo& canvas) override;
    QImage getCanvasImage(const CanvasInfo& canvas, int mapLevel = 0, int maxSize = 0) override;
    int getMapLevelForSize(const CanvasInfo& canvas, int maxSize) override;
    int mapCount() const override { return 11; }
    std::string versionName() const override { return m_versionName; }
    void setVersionName(const std::string& name) { m_versionName = name; }

private:
    HANDLE m_hProcess;
    const ProcessReader& m_reader;
    uintptr_t m_baseAddress;
    uintptr_t m_canvasListOffset;
    std::string m_versionName;

    // SAIv2 结构体偏移
    struct Layout {
        // SAICanvasTileMap
        static constexpr uint32_t tilemap_tree    = 0x08;
        static constexpr uint32_t tilemap_width   = 0x10;
        static constexpr uint32_t tilemap_height  = 0x14;
        static constexpr uint32_t tilemap_count_x = 0x18;
        static constexpr uint32_t tilemap_count_y = 0x1C;
        // SAICanvas
        static constexpr uint32_t canvas_next       = 0x000;
        static constexpr uint32_t canvas_id         = 0x020;
        static constexpr uint32_t canvas_tile_maps  = 0x028;
        static constexpr uint32_t canvas_width      = 0x250;
        static constexpr uint32_t canvas_height     = 0x254;
        static constexpr uint32_t canvas_name       = 0x2AC;
        static constexpr uint32_t canvas_short_path = 0x4AC;
        static constexpr int tileSize = 256;
    };

    CanvasInfo readCanvasInfo(uintptr_t canvasPtr) const;

    // 读取单个 tile 的像素数据
    QImage readTile(const ProcessReader& reader, uintptr_t treePtr,
                    int tileX, int tileY) const;
};

// ============================================================
// SAI 管理类 - 高层接口
// ============================================================
class SAIManager : public QObject {
    Q_OBJECT
public:
    explicit SAIManager(QObject* parent = nullptr);

    // 检测并连接到 SAI 进程
    bool detectAndConnect(int versionOverrideIndex = -1);

    // 断开
    void disconnect();

    bool isConnected() const { return m_engine != nullptr; }

    // 获取引擎
    SAIEngine* engine() { return m_engine.get(); }

    // 获取可用版本列表
    std::vector<std::string> availableVersions() const;

    // 获取检测到的版本名
    std::string detectedVersion() const { return m_detectedVersion; }

    DWORD pid() const { return m_pid; }

    bool isProcessRunning() const;

    // 刷新画布列表
    std::vector<CanvasInfo> refreshCanvases();

private:
    HANDLE m_hProcess = nullptr;
    DWORD m_pid = 0;
    std::unique_ptr<ProcessReader> m_reader;
    SAIEnginePtr m_engine;
    std::string m_detectedVersion;
    uintptr_t m_baseAddress = 0;
};

#endif // SAIENGINE_H
