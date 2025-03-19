#include <iostream>
#include <windows.h>
#include <winioctl.h>
#include <vector>
#include <tuple>

using namespace std;

typedef tuple<string, bool, int, int, bool> ITEM; //{name, isFolder, size, startCluster, isDeleted}  
#define BUFFER_SIZE 512

bool lockVolume(HANDLE hVolume)
{
    DWORD bytesReturned;
    return DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
}

bool unlockVolume(HANDLE hVolume)
{
    DWORD bytesReturned;
    return DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
}

bool readSector(HANDLE hVolume, unsigned char buffer[], int sector)
{
    LARGE_INTEGER li;
    li.QuadPart = 0x200 * sector; 
    if (!SetFilePointerEx(hVolume, li, NULL, FILE_BEGIN)) {
        DWORD error = GetLastError();
        std::cerr << "Seeking error: " << error << std::endl;
        CloseHandle(hVolume);
        return 1;
    }

    DWORD bytesRead;
    if (!ReadFile(hVolume, buffer, BUFFER_SIZE, &bytesRead, NULL) || bytesRead == 0) {
        DWORD error = GetLastError();
        std::cerr << "Reading sector error: " << error << std::endl;
        CloseHandle(hVolume);
        return 0;
    }

    return 1;
}

bool writeSector(HANDLE hVolume, unsigned char buffer[], int sector)
{
    LARGE_INTEGER li;
    li.QuadPart = 0x200 * sector; 
    if (!SetFilePointerEx(hVolume, li, NULL, FILE_BEGIN)) {
        DWORD error = GetLastError();
        std::cerr << "Seeking error: " << error << std::endl;
        CloseHandle(hVolume);
        return 1;
    }

    DWORD bytesWritten;
    if (!WriteFile(hVolume, buffer, BUFFER_SIZE, &bytesWritten, NULL) || bytesWritten != BUFFER_SIZE) 
    {
        DWORD error = GetLastError();
        std::cerr << "Writing sector error: " << error << std::endl;
        CloseHandle(hVolume);
        return false;
    }

    return 1;
}

// void readEntry(unsigned char buffer[], int line, std::string &name, int &size, int &start_cluster)
// {

// }

int readEntry(unsigned char buffer[], int line, ITEM &item)
{
    int offset = line * 32; // Mỗi entry trong FAT32 có kích thước 32 byte
    std::string fullName;
    int entryIndex = line;

    if (buffer[offset + 0xB] == 0x0F)
        return 1;
    if (buffer[offset] == 0 and buffer[offset + 0x10] == 0)
        return -1;

    // Đọc entry phụ (Long File Name - LFN)
    while (entryIndex > 0)
    {
        int lfnOffset = (entryIndex - 1) * 32;
        if (buffer[lfnOffset + 11] != 0x0F) // Nếu không phải entry phụ thì dừng
            break;

        // Đọc 13 ký tự Unicode từ entry phụ
        for (int i : {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30})
        {
            if (buffer[lfnOffset + i] == 0xFF || buffer[lfnOffset + i] == 0x00) // Kết thúc chuỗi
                break;
            fullName += (char)buffer[lfnOffset + i];
        }
        entryIndex--;
    }

    // Đọc entry chính
    std::string shortName(reinterpret_cast<char*>(buffer + offset), 11);
    shortName = shortName.substr(0, shortName.find_last_not_of(' ') + 1);

    if (fullName.empty()) // Nếu không có LFN, dùng short name
        fullName = shortName;

    // Đọc cluster bắt đầu (2 byte ở offset 20 + 2 byte ở offset 26)
    int start_cluster = (buffer[offset + 26] | (buffer[offset + 27] << 8)) |
                        ((buffer[offset + 20] | (buffer[offset + 21] << 8)) << 16);

    // Đọc kích thước file (4 byte cuối)
    int size = buffer[offset + 28] | (buffer[offset + 29] << 8) |
               (buffer[offset + 30] << 16) | (buffer[offset + 31] << 24);
    
    // Folder or File?
    bool isFolder = (buffer[offset + 0xB] & (1 << 4));
    
    // Is Deleted?
    bool isDeleted = (buffer[offset] == 0xE5);

    item = {fullName, isFolder, size, start_cluster, isDeleted};
    return 0;
}
