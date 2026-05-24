#include "saiengine.h"
#include <QDebug>
#include <algorithm>
#include <cstring>

// ============================================================
// SAIVersionRegistry
// ============================================================
SAIVersionRegistry& SAIVersionRegistry::instance() {
    static SAIVersionRegistry reg;
    return reg;
}

void SAIVersionRegistry::registerVersion(const std::string& exeHash,
                                          const std::string& versionName,
                                          SAIFactoryFunc factory) {
    m_entries.push_back({exeHash, versionName, std::move(factory)});
}

SAIFactoryFunc SAIVersionRegistry::findFactory(const std::string& exeHash) const {
    for (const auto& entry : m_entries) {
        if (entry.hash == exeHash) {
            return entry.factory;
        }
    }
    return nullptr;
}

std::string SAIVersionRegistry::detectVersion(HANDLE hProcess) const {
    std::wstring exePath = ProcessFinder::getExePath(hProcess);
    if (exePath.empty()) return "";
    return computeFileMD5(exePath);
}

SAIEnginePtr SAIVersionRegistry::createEngine(HANDLE hProcess,
                                               const std::string& exeHash,
                                               const ProcessReader& reader,
                                               uintptr_t baseAddress) const {
    auto factory = findFactory(exeHash);
    if (factory) {
        return factory(hProcess, reader, baseAddress);
    }
    return nullptr;
}

SAIEnginePtr SAIVersionRegistry::createEngineByIndex(HANDLE hProcess,
                                                      int versionIndex,
                                                      const ProcessReader& reader,
                                                      uintptr_t baseAddress) const {
    if (versionIndex < 0 || versionIndex >= static_cast<int>(m_entries.size())) {
        return nullptr;
    }

    return m_entries[versionIndex].factory(hProcess, reader, baseAddress);
}

std::vector<std::string> SAIVersionRegistry::versionNames() const {
    std::vector<std::string> names;
    for (const auto& entry : m_entries) {
        names.push_back(entry.name);
    }
    return names;
}

// ============================================================
// SAIv1Engine
// ============================================================
SAIv1Engine::SAIv1Engine(HANDLE hProcess, const ProcessReader& reader,
                         uintptr_t baseAddress, uintptr_t sessionOffset)
    : m_hProcess(hProcess), m_reader(reader),
      m_baseAddress(baseAddress), m_sessionOffset(sessionOffset) {}

CanvasInfo SAIv1Engine::readCanvasInfo(uintptr_t canvasPtr) const {
    CanvasInfo info;
    info.selfPtr = canvasPtr;

    // 读取所有必要字段
    info.width      = m_reader.read<int32_t>(canvasPtr + Layout::canvas_width);
    info.height     = m_reader.read<int32_t>(canvasPtr + Layout::canvas_height);

    // 读取名称
    auto nameBytes = m_reader.readBytes(canvasPtr + Layout::canvas_name, 0x108);
    // 截断到第一个 null 字符
    size_t nameLen = 0;
    while (nameLen < nameBytes.size() && nameBytes[nameLen] != 0) nameLen++;
    info.name = QString::fromUtf8(reinterpret_cast<const char*>(nameBytes.data()), static_cast<int>(nameLen));

    // 读取短路径
    auto pathBytes = m_reader.readBytes(canvasPtr + Layout::canvas_short_path, 0x108);
    size_t pathLen = 0;
    while (pathLen < pathBytes.size() && pathBytes[pathLen] != 0) pathLen++;
    info.shortPath = QString::fromUtf8(reinterpret_cast<const char*>(pathBytes.data()), static_cast<int>(pathLen));

    return info;
}

std::vector<CanvasInfo> SAIv1Engine::getCanvasList() {
    std::vector<CanvasInfo> result;

    // SAIv1: 直接从 base + session_offset 读取 session 结构体
    // session->canvas_list 是一个32位指针
    uintptr_t sessionAddr = m_baseAddress + m_sessionOffset;
    uint32_t sessionPtr = m_reader.read<uint32_t>(sessionAddr);

    // sessionPtr 指向 SAISession, canvas_list 在其 offset 0x4C
    uint32_t canvasHeadPtr = m_reader.read<uint32_t>(sessionPtr + Layout::session_canvas_list);

    // 遍历链表
    uintptr_t current = canvasHeadPtr;
    while (current != 0) {
        CanvasInfo info = readCanvasInfo(current);
        result.push_back(info);

        // 读取 next 指针
        current = m_reader.read<uint32_t>(current + Layout::canvas_next);
    }

    return result;
}

bool SAIv1Engine::canvasExists(const CanvasInfo& canvas) {
    auto canvases = getCanvasList();
    return std::any_of(canvases.begin(), canvases.end(),
                       [&](const CanvasInfo& c) { return c.selfPtr == canvas.selfPtr; });
}

QImage SAIv1Engine::getCanvasImage(const CanvasInfo& canvas, int /*mapLevel*/, int maxSize) {
    // 重新读取画布结构体以获取最新数据
    CanvasInfo updated = readCanvasInfo(canvas.selfPtr);

    int32_t lowerPadX = m_reader.read<int32_t>(canvas.selfPtr + Layout::canvas_lower_pad_x);
    int32_t lowerPadY = m_reader.read<int32_t>(canvas.selfPtr + Layout::canvas_lower_pad_y);
    int32_t paddedW = m_reader.read<int32_t>(canvas.selfPtr + Layout::canvas_padded_w);
    int32_t paddedH = m_reader.read<int32_t>(canvas.selfPtr + Layout::canvas_padded_h);

    // 读取 pixel_heap 指针
    uint32_t pixelHeapPtr = m_reader.read<uint32_t>(canvas.selfPtr + Layout::canvas_pixelheap);
    // 读取 pixel_heap->data 指针
    uint32_t dataPtr = m_reader.read<uint32_t>(pixelHeapPtr + Layout::pixelheap_data);

    // 读取整个 padded 区域的像素数据 (BGRA 格式)
    size_t dataSize = static_cast<size_t>(paddedW) * paddedH * 4;
    auto rawData = m_reader.readBytes(dataPtr, dataSize);

    // 裁剪有效区域
    int w = updated.width;
    int h = updated.height;

    QImage image(w, h, QImage::Format_RGB32);

    // 逐行 memcpy (SAI 像素数据为 BGRA，Python 版也丢弃 Alpha 通道只取 [:3])
    for (int y = 0; y < h; y++) {
        int srcY = y + lowerPadY;
        size_t srcIdx = (static_cast<size_t>(srcY) * paddedW + lowerPadX) * 4;
        size_t rowBytes = static_cast<size_t>(w) * 4;
        if (srcIdx + rowBytes <= rawData.size()) {
            memcpy(image.scanLine(y), rawData.data() + srcIdx, rowBytes);
        }
    }

    // 缩放
    if (maxSize > 0 && (w > maxSize || h > maxSize)) {
        image = image.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return image;
}

int SAIv1Engine::getMapLevelForSize(const CanvasInfo& /*canvas*/, int /*maxSize*/) {
    return 0; // SAIv1 只有一个级别
}

// ============================================================
// SAIv2Engine
// ============================================================
SAIv2Engine::SAIv2Engine(HANDLE hProcess, const ProcessReader& reader,
                         uintptr_t baseAddress, uintptr_t canvasListOffset)
    : m_hProcess(hProcess), m_reader(reader),
      m_baseAddress(baseAddress), m_canvasListOffset(canvasListOffset) {}

CanvasInfo SAIv2Engine::readCanvasInfo(uintptr_t canvasPtr) const {
    CanvasInfo info;
    info.selfPtr = canvasPtr;

    info.id     = m_reader.read<int32_t>(canvasPtr + Layout::canvas_id);
    info.width  = m_reader.read<int32_t>(canvasPtr + Layout::canvas_width);
    info.height = m_reader.read<int32_t>(canvasPtr + Layout::canvas_height);

    // 读取名称 (UTF-16LE)
    auto nameBytes = m_reader.readBytes(canvasPtr + Layout::canvas_name, 0x200);
    // 找到 null 终止位置
    size_t nameLen = 0;
    for (size_t i = 0; i + 1 < nameBytes.size(); i += 2) {
        if (nameBytes[i] == 0 && nameBytes[i + 1] == 0) break;
        nameLen += 2;
    }
    if (nameLen > 0) {
        info.name = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(nameBytes.data()),
            static_cast<int>(nameLen / 2));
    }

    // 读取短路径 (UTF-16LE)
    auto pathBytes = m_reader.readBytes(canvasPtr + Layout::canvas_short_path, 0x200);
    size_t pathLen = 0;
    for (size_t i = 0; i + 1 < pathBytes.size(); i += 2) {
        if (pathBytes[i] == 0 && pathBytes[i + 1] == 0) break;
        pathLen += 2;
    }
    if (pathLen > 0) {
        info.shortPath = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(pathBytes.data()),
            static_cast<int>(pathLen / 2));
    }

    return info;
}

std::vector<CanvasInfo> SAIv2Engine::getCanvasList() {
    std::vector<CanvasInfo> result;

    // SAIv2: base + offset 处存储的是指向 SAICanvas 链表头的指针
    // 与 Python 版 RP pointer 语义一致：读取一次指针即为链表头地址
    uint64_t headPtr = m_reader.read<uint64_t>(m_baseAddress + m_canvasListOffset);
    if (headPtr == 0) return result;

    uintptr_t current = static_cast<uintptr_t>(headPtr);

    // 跳过哨兵节点 (index 0), 从第一个实际画布开始
    if (current != 0) {
        current = static_cast<uintptr_t>(m_reader.read<uint64_t>(current + Layout::canvas_next));
    }

    while (current != 0) {
        CanvasInfo info = readCanvasInfo(current);
        result.push_back(info);
        current = static_cast<uintptr_t>(m_reader.read<uint64_t>(current + Layout::canvas_next));
    }

    return result;
}

bool SAIv2Engine::canvasExists(const CanvasInfo& canvas) {
    auto canvases = getCanvasList();
    return std::any_of(canvases.begin(), canvases.end(),
                       [&](const CanvasInfo& c) { return c.id == canvas.id; });
}

QImage SAIv2Engine::readTile(const ProcessReader& reader, uintptr_t treePtr,
                              int tileX, int tileY) const {
    // tree 是 row[col[data]] 的三级指针结构
    // treePtr 指向 row 指针数组
    uint64_t rowPtr = reader.read<uint64_t>(treePtr + static_cast<uintptr_t>(tileY) * 8);
    if (rowPtr == 0) {
        // 空 tile，返回纯黑
        QImage tile(Layout::tileSize, Layout::tileSize, QImage::Format_RGB32);
        tile.fill(Qt::black);
        return tile;
    }

    uint64_t colPtr = reader.read<uint64_t>(rowPtr + static_cast<uintptr_t>(tileX) * 8);
    if (colPtr == 0) {
        QImage tile(Layout::tileSize, Layout::tileSize, QImage::Format_RGB32);
        tile.fill(Qt::black);
        return tile;
    }

    // colPtr 指向 tile 数据的地址
    // SAI 像素数据为 BGRA 格式。Python 版取 [:3] 丢弃 Alpha。
    // 使用 Format_RGB32 使 Qt 忽略 Alpha 字节，避免未绘制区域(Alpha=0)显示为透明。
    size_t tileBytes = Layout::tileSize * Layout::tileSize * 4;
    auto rawData = reader.readBytes(colPtr, tileBytes);

    QImage tile(Layout::tileSize, Layout::tileSize, QImage::Format_RGB32);
    size_t copyBytes = std::min(rawData.size(), tileBytes);
    if (copyBytes > 0) {
        memcpy(tile.bits(), rawData.data(), copyBytes);
    }
    return tile;
}

QImage SAIv2Engine::getCanvasImage(const CanvasInfo& canvas, int mapLevel, int maxSize) {
    // 重新读取最新画布数据
    CanvasInfo updated = readCanvasInfo(canvas.selfPtr);

    // 读取 tile_maps 指针 (指向 SAICanvasTileMap* 数组)
    uintptr_t tileMapsArrayPtr = canvas.selfPtr + Layout::canvas_tile_maps;

    // 读取 tile_maps[mapLevel] 指针
    uint64_t tileMapPtrPtr = m_reader.read<uint64_t>(tileMapsArrayPtr +
                                                      static_cast<uintptr_t>(mapLevel) * 8);
    if (tileMapPtrPtr == 0) {
        return QImage();
    }

    // 解引用 -> SAICanvasTileMap
    uint64_t tileMapPtr = m_reader.read<uint64_t>(tileMapPtrPtr);
    if (tileMapPtr == 0) {
        return QImage();
    }
    uintptr_t tmAddr = static_cast<uintptr_t>(tileMapPtr);

    // 读取 tileMap 字段
    int32_t tmWidth  = m_reader.read<int32_t>(tmAddr + Layout::tilemap_width);
    int32_t tmHeight = m_reader.read<int32_t>(tmAddr + Layout::tilemap_height);
    int32_t countX   = m_reader.read<int32_t>(tmAddr + Layout::tilemap_count_x);
    int32_t countY   = m_reader.read<int32_t>(tmAddr + Layout::tilemap_count_y);

    // 读取 tree 指针
    uint64_t treePtr = m_reader.read<uint64_t>(tmAddr + Layout::tilemap_tree);
    if (treePtr == 0) {
        return QImage();
    }

    // 创建完整画布图像
    int fullW = countX * Layout::tileSize;
    int fullH = countY * Layout::tileSize;
    QImage fullImage(fullW, fullH, QImage::Format_RGB32);

    // 逐 tile 读取 (使用 scanLine memcpy，性能远优于逐像素 setPixelColor)
    for (int y = 0; y < countY; y++) {
        for (int x = 0; x < countX; x++) {
            QImage tile = readTile(m_reader, treePtr, x, y);
            int destBaseX = x * Layout::tileSize;
            int destBaseY = y * Layout::tileSize;
            int tileW = Layout::tileSize;
            int tileH = Layout::tileSize;
            // 复制 tile 的每一行到完整图像的对应位置
            for (int ty = 0; ty < tileH; ty++) {
                int destY = destBaseY + ty;
                if (destY >= fullH) break;
                int copyPixels = qMin(tileW, fullW - destBaseX);
                if (copyPixels > 0) {
                    memcpy(fullImage.scanLine(destY) + destBaseX * 4,
                           tile.constScanLine(ty),
                           static_cast<size_t>(copyPixels) * 4);
                }
            }
        }
    }

    // 裁剪到实际画布尺寸
    QImage cropped = fullImage.copy(0, 0, tmWidth, tmHeight);

    // 缩放
    if (maxSize > 0 && (tmWidth > maxSize || tmHeight > maxSize)) {
        cropped = cropped.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return cropped;
}

int SAIv2Engine::getMapLevelForSize(const CanvasInfo& canvas, int maxSize) {
    CanvasInfo updated = readCanvasInfo(canvas.selfPtr);

    uintptr_t tileMapsArrayPtr = canvas.selfPtr + Layout::canvas_tile_maps;
    int bestLevel = 0;

    for (int i = 0; i < mapCount(); i++) {
        uint64_t tileMapPtrPtr = m_reader.read<uint64_t>(tileMapsArrayPtr +
                                                          static_cast<uintptr_t>(i) * 8);
        if (tileMapPtrPtr == 0) break;

        uint64_t tileMapPtr = m_reader.read<uint64_t>(tileMapPtrPtr);
        if (tileMapPtr == 0) break;

        int32_t w = m_reader.read<int32_t>(static_cast<uintptr_t>(tileMapPtr) + Layout::tilemap_width);
        int32_t h = m_reader.read<int32_t>(static_cast<uintptr_t>(tileMapPtr) + Layout::tilemap_height);

        if (w < maxSize && h < maxSize) break;
        bestLevel = i;
    }

    return bestLevel;
}

// ============================================================
// SAI 版本工厂函数
// ============================================================
// SAI 版本宏，自动注册
namespace {
    struct AutoRegister {
        AutoRegister(const std::string& hash, const std::string& name, SAIFactoryFunc f) {
            SAIVersionRegistry::instance().registerVersion(hash, name, std::move(f));
        }
    };
}

// SAIv1 1.2.5
static AutoRegister _reg_v1_1_2_5("f4e7e00aa4c222d6253aa1f0a5f302c2",
    "SAI Ver.1.2.5 (32bit)",
    [](HANDLE h, const ProcessReader& r, uintptr_t base) -> SAIEnginePtr {
        auto engine = std::make_unique<SAIv1Engine>(h, r, base, 0x491a8c);
        engine->setVersionName("SAI Ver.1.2.5 (32bit)");
        return engine;
    });

// SAIv1 1.2.6 Beta 3
static AutoRegister _reg_v1_1_2_6_b3("b693f2c4516c09d008e30827208be1e6",
    "SAI Ver.1.2.6-Beta.3 (32bit)",
    [](HANDLE h, const ProcessReader& r, uintptr_t base) -> SAIEnginePtr {
        auto engine = std::make_unique<SAIv1Engine>(h, r, base, 0x494bcc);
        engine->setVersionName("SAI Ver.1.2.6-Beta.3 (32bit)");
        return engine;
    });

// SAIv2 helper
static SAIEnginePtr createSAIv2(HANDLE h, const ProcessReader& r, uintptr_t base,
                                 uintptr_t offset, const std::string& name) {
    auto engine = std::make_unique<SAIv2Engine>(h, r, base, offset);
    engine->setVersionName(name);
    return engine;
}

// SAIv2 2022.02.07
static AutoRegister _reg_v2_2022_02_07("648a1e5df42851f33feb6f3db31bb85e",
    "SAI Ver.2 (64bit) Preview.2022.02.07",
    [](HANDLE h, const ProcessReader& r, uintptr_t base) -> SAIEnginePtr {
        return createSAIv2(h, r, base, 0x319BE0,
                           "SAI Ver.2 (64bit) Preview.2022.02.07");
    });

// SAIv2 2023.07.11
static AutoRegister _reg_v2_2023_07_11("f8df4067657d811e6e48c37c5b0f8fc5",
    "SAI Ver.2 (64bit) Preview.2023.07.11",
    [](HANDLE h, const ProcessReader& r, uintptr_t base) -> SAIEnginePtr {
        return createSAIv2(h, r, base, 0x31af20,
                           "SAI Ver.2 (64bit) Preview.2023.07.11");
    });

// SAIv2 2024.11.23
static AutoRegister _reg_v2_2024_11_23("bd60d6750ef57668f9bc44eb98d992c4",
    "SAI Ver.2 (64bit) Preview.2024.11.23",
    [](HANDLE h, const ProcessReader& r, uintptr_t base) -> SAIEnginePtr {
        return createSAIv2(h, r, base, 0x322620,
                           "SAI Ver.2 (64bit) Preview.2024.11.23");
    });

// ============================================================
// SAIManager
// ============================================================
SAIManager::SAIManager(QObject* parent)
    : QObject(parent) {}

bool SAIManager::detectAndConnect(int versionOverrideIndex) {
    disconnect();

    // 查找 SAI 进程
    m_pid = ProcessFinder::findPidByName(L"sai2.exe");
    if (m_pid == 0) {
        m_pid = ProcessFinder::findPidByName(L"sai.exe");
    }
    if (m_pid == 0) {
        return false;
    }

    try {
        m_hProcess = ProcessFinder::openProcess(m_pid);
    } catch (...) {
        m_pid = 0;
        return false;
    }

    m_reader = std::make_unique<ProcessReader>(m_hProcess);
    m_baseAddress = ProcessFinder::getBaseAddress(m_hProcess, m_pid);

    // 检测版本
    const std::string detectedHash = SAIVersionRegistry::instance().detectVersion(m_hProcess);
    m_detectedVersion = detectedHash;
    m_engine = SAIVersionRegistry::instance().createEngine(m_hProcess, detectedHash,
                                                            *m_reader, m_baseAddress);

    // 如果版本未匹配，尝试使用覆盖
    if (!m_engine && versionOverrideIndex >= 0) {
        m_engine = SAIVersionRegistry::instance().createEngineByIndex(
            m_hProcess, versionOverrideIndex, *m_reader, m_baseAddress);
    }

    if (m_engine) {
        m_detectedVersion = m_engine->versionName();
    }

    return m_engine != nullptr;
}

void SAIManager::disconnect() {
    m_engine.reset();
    m_reader.reset();
    if (m_hProcess) {
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
    }
    m_pid = 0;
    m_baseAddress = 0;
    m_detectedVersion.clear();
}

bool SAIManager::isProcessRunning() const {
    // 使用已持有的 m_hProcess 句柄（PROCESS_QUERY_INFORMATION 权限即可），
    // 而非重新 OpenProcess(SYNCHRONIZE)，避免因完整性级别不匹配导致权限被拒
    if (m_pid == 0 || !m_hProcess) return false;
    DWORD exitCode;
    if (GetExitCodeProcess(m_hProcess, &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    // 如果 GetExitCodeProcess 也失败了，回退尝试用 SYNCHRONIZE 打开
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, m_pid);
    if (h) {
        BOOL ok = GetExitCodeProcess(h, &exitCode);
        CloseHandle(h);
        return ok && exitCode == STILL_ACTIVE;
    }
    return false;
}

std::vector<std::string> SAIManager::availableVersions() const {
    return SAIVersionRegistry::instance().versionNames();
}

std::vector<CanvasInfo> SAIManager::refreshCanvases() {
    if (!m_engine) return {};
    try {
        return m_engine->getCanvasList();
    } catch (...) {
        return {};
    }
}
