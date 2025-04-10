#include "filehandle.h"

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
        return 0;
    }

    DWORD bytesRead;
    if (!ReadFile(hVolume, buffer, BUFFER_SIZE_FAT, &bytesRead, NULL) || bytesRead == 0) {
        DWORD error = GetLastError();
        std::cerr << "Reading sector error: " << error << std::endl;
        CloseHandle(hVolume);
        return 0;
    }

    return 1;
}

bool writeSector(HANDLE hVolume, unsigned char buffer[], int sector)
{
    lockVolume(hVolume);

    LARGE_INTEGER li;
    li.QuadPart = 0x200 * sector; 
    if (!SetFilePointerEx(hVolume, li, NULL, FILE_BEGIN)) {
        DWORD error = GetLastError();
        std::cerr << "Seeking error: " << error << std::endl;
        CloseHandle(hVolume);

        unlockVolume(hVolume);
        return 0;
    }

    DWORD bytesWritten;
    if (!WriteFile(hVolume, buffer, BUFFER_SIZE_FAT, &bytesWritten, NULL) || bytesWritten != BUFFER_SIZE_FAT) 
    {
        DWORD error = GetLastError();
        std::cerr << "Writing sector error: " << error << std::endl;
        CloseHandle(hVolume);

        unlockVolume(hVolume);
        return 0;
    }

    unlockVolume(hVolume);

    return 1;
}

int readEntry(HANDLE hVolume, unsigned char sector[], int recentSector, int line, ITEM &item)
{
    int offset = line * 32; // Mỗi entry trong FAT32 có kích thước 32 byte
    std::string fullName;
    int entryIndex = line;

    unsigned char buffer[BUFFER_SIZE_FAT];
    for (int i = 0; i < BUFFER_SIZE_FAT; i++)
        buffer[i] = sector[i];

    if (buffer[offset + 0xB] == 0x0F)
        return 1;
    if (buffer[offset] == 0 and buffer[offset + 0x10] == 0)
        return -1;

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

    //Đọc entry phụ (Long File Name - LFN)
    while (entryIndex >= 0)
    {
        if (entryIndex <= 0 && recentSector > 0)
        {
            recentSector--;  // Di chuyển về sector trước
            readSector(hVolume, buffer, recentSector); // Đọc lại sector trước
            entryIndex = 16; // FAT32 có tối đa 16 entry trên 1 sector, bắt đầu từ cuối
        }

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
    {
        //fullName = shortName; --> X
        // Lấy phần tên (8 ký tự đầu)
        std::string name(reinterpret_cast<char*>(buffer + offset), 8);
        name = name.substr(0, name.find_last_not_of(' ') + 1);

        // Lấy phần mở rộng (3 ký tự tiếp theo)
        std::string ext(reinterpret_cast<char*>(buffer + offset + 8), 3);
        ext = ext.substr(0, ext.find_last_not_of(' ') + 1);

        // Nối thành "name.ext" nếu có phần mở rộng
        fullName = ext.empty() ? name : (name + "." + ext);
    }

    

    item = {{0, 0}, fullName, isFolder, size, start_cluster, isDeleted};
    return 0;
}
