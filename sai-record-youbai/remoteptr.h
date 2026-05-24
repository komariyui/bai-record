#ifndef REMOTEPTR_H
#define REMOTEPTR_H

// 必须在 windows.h 之前定义，防止 winsock.h 与 Qt connect 冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <string>

// ============================================================
// ProcessReader - Windows 进程内存读取工具
// 封装 ReadProcessMemory，提供类型安全的远程内存访问
// ============================================================
class ProcessReader {
public:
    explicit ProcessReader(HANDLE hProcess) : m_hProcess(hProcess) {}
    ~ProcessReader() = default;

    ProcessReader(const ProcessReader&) = delete;
    ProcessReader& operator=(const ProcessReader&) = delete;

    // 读取指定类型的值
    template<typename T>
    T read(uintptr_t address) const {
        T result{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(address),
                               &result, sizeof(T), &bytesRead) || bytesRead != sizeof(T)) {
            throw std::runtime_error("ReadProcessMemory failed at 0x" +
                                     std::to_string(address));
        }
        return result;
    }

    // 读取字节数组
    std::vector<uint8_t> readBytes(uintptr_t address, size_t size) const {
        std::vector<uint8_t> buffer(size);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(m_hProcess, reinterpret_cast<LPCVOID>(address),
                               buffer.data(), size, &bytesRead)) {
            throw std::runtime_error("ReadProcessMemory (bytes) failed at 0x" +
                                     std::to_string(address));
        }
        buffer.resize(bytesRead);
        return buffer;
    }

    // 读取指针（32位进程）
    uint32_t readPtr32(uintptr_t address) const {
        return read<uint32_t>(address);
    }

    // 读取指针（64位进程）
    uint64_t readPtr64(uintptr_t address) const {
        return read<uint64_t>(address);
    }

    // 判断是否32位进程
    bool isProcess32Bit() const {
        BOOL wow64 = FALSE;
        if (IsWow64Process(m_hProcess, &wow64) && wow64) {
            return true;
        }
        // 检查当前进程是否是64位
#ifdef _WIN64
        return false;
#else
        return true;
#endif
    }

    // 读指针（自动判断32/64位）
    uint64_t readPtr(uintptr_t address) const {
        if (isProcess32Bit()) {
            return read<uint32_t>(address);
        }
        return read<uint64_t>(address);
    }

    // 读指针并解引用到指定类型（自动32/64位）
    template<typename T>
    T derefPtr(uintptr_t address) const {
        uint64_t ptr = readPtr(address);
        if (ptr == 0) {
            return T{};
        }
        return read<T>(static_cast<uintptr_t>(ptr));
    }

    HANDLE handle() const { return m_hProcess; }

private:
    HANDLE m_hProcess;
};

// ============================================================
// ProcessFinder - 进程查找工具
// ============================================================
class ProcessFinder {
public:
    // 按进程名查找 PID
    static DWORD findPidByName(const std::wstring& processName) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

        PROCESSENTRY32W pe32{};
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        DWORD pid = 0;
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (_wcsicmp(pe32.szExeFile, processName.c_str()) == 0) {
                    pid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
        return pid;
    }

    // 按 PID 获取进程名
    static std::wstring getProcessName(DWORD pid) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) return L"";

        PROCESSENTRY32W pe32{};
        pe32.dwSize = sizeof(PROCESSENTRY32W);

        std::wstring name;
        if (Process32FirstW(hSnapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == pid) {
                    name = pe32.szExeFile;
                    break;
                }
            } while (Process32NextW(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
        return name;
    }

    // 打开进程句柄
    static HANDLE openProcess(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!h) {
            throw std::runtime_error("Cannot open process PID=" +
                                     std::to_string(pid));
        }
        return h;
    }

    // 获取进程基址
    static uintptr_t getBaseAddress(HANDLE hProcess, DWORD pid) {
        HMODULE hMods[1];
        DWORD cbNeeded;
        if (EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded,
                                 LIST_MODULES_ALL)) {
            return reinterpret_cast<uintptr_t>(hMods[0]);
        }
        return 0;
    }

    // 获取进程EXE路径
    static std::wstring getExePath(HANDLE hProcess) {
        WCHAR path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
            return std::wstring(path, size);
        }
        return L"";
    }
};

// ============================================================
// MD5 简易实现 (仅用于文件哈希匹配)
// ============================================================
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")

inline std::string computeFileMD5(const std::wstring& filePath) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL,
                              CRYPT_VERIFYCONTEXT)) {
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        BYTE buffer[8192];
        DWORD bytesRead;
        while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
            CryptHashData(hHash, buffer, bytesRead, 0);
        }
        CloseHandle(hFile);

        BYTE md5[16];
        DWORD md5Len = sizeof(md5);
        if (CryptGetHashParam(hHash, HP_HASHVAL, md5, &md5Len, 0)) {
            char hex[33];
            for (int i = 0; i < 16; i++) {
                sprintf_s(hex + i * 2, 3, "%02x", md5[i]);
            }
            result = std::string(hex, 32);
        }
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

#endif // REMOTEPTR_H
