#pragma once

#include <string>
#include <stdexcept>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace tachyon {

class MMapReader {
public:
    const char* data = nullptr;
    size_t size = 0;

    MMapReader(const std::string& filepath) {
#ifdef _WIN32
        HANDLE hFile = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) throw std::runtime_error("MMap failed to open file.");
        
        LARGE_INTEGER li;
        GetFileSizeEx(hFile, &li);
        size = li.QuadPart;
        
        HANDLE hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (hMap == NULL) throw std::runtime_error("MMap failed to create mapping.");
        
        data = static_cast<const char*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
        
        CloseHandle(hMap);
        CloseHandle(hFile);
#else
        int fd = open(filepath.c_str(), O_RDONLY);
        if (fd == -1) throw std::runtime_error("MMap failed to open file.");
        
        struct stat sb;
        fstat(fd, &sb);
        size = sb.st_size;
        
        data = static_cast<const char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        close(fd);
#endif
        if (!data) throw std::runtime_error("MMap returned null pointer.");
    }

    ~MMapReader() {
        if (data) {
#ifdef _WIN32
            UnmapViewOfFile(data);
#else
            munmap(const_cast<char*>(data), size);
#endif
        }
    }
};

} // namespace tachyon