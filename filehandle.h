#include <iostream>
#include <windows.h>
#include <winioctl.h>
#include <vector>
#include <tuple>
#include <cstring> // memcpy
#include <cstdint>
#include <fstream>
#include <algorithm>

using namespace std;

bool lockVolume(HANDLE hVolume);
bool unlockVolume(HANDLE hVolume);
bool readSector(HANDLE hVolume, unsigned char buffer[], int sector);
bool writeSector(HANDLE hVolume, unsigned char buffer[], int sector);
int getIntValue(unsigned char buffer[], int start, int size);

//FOR FAT32
typedef tuple<pair<int, int>, string, bool, int, int, bool> ITEM; //{{sector, index}, name, isFolder, size, startCluster, isDeleted}  

int readEntry(HANDLE hVolume, unsigned char sector[], int recentSector, int line, ITEM &item);
int restoreItem(HANDLE hVolume, ITEM item);
void getCertainFolder(HANDLE hVolume, int recentSector, vector<ITEM> &folders, vector<ITEM> &deletedFiles);

//FOR NTFS
typedef tuple<string, bool, int, int, bool, bool, int> ITEM_NTFS; 

#pragma pack(push, 1)
struct ResidentAttrHeader {
    uint32_t type;           // Loại thuộc tính
    uint32_t length;         // Độ dài toàn bộ thuộc tính (header + value)
    uint8_t  nonResident;    // 0 nếu resident, 1 nếu non-resident
    uint8_t  nameLength;     // Độ dài tên của attribute (nếu có), tính theo ký tự
    uint16_t nameOffset;     // Offset đến tên của attribute (nếu có)
    uint16_t flags;          // Flags của attribute
    uint16_t attributeNumber;// Số thứ tự của attribute
    // Resident Attribute Header cụ thể:
    uint32_t valueLength;    // Độ dài phần value (nội dung)
    uint16_t valueOffset;    // Offset từ đầu attribute đến phần value
    uint8_t  indexedFlag;    // Chỉ số (nếu có)
    uint8_t  padding;        // Padding
};

struct NonResidentAttrHeader {
    uint32_t type;
    uint16_t length;
    uint8_t  nonResident;
    uint8_t  nameLength;
    uint16_t nameOffset;
    uint16_t flags;
    uint16_t attributeNumber;
    uint64_t startVCN;
    uint64_t endVCN;
    uint16_t dataRunOffset;
    uint16_t compressionUnit;
    uint32_t padding;
    uint64_t allocatedSize;
    uint64_t realSize;
    uint64_t initializedSize;
};
#pragma pack(pop)

bool isMFTEntry(const unsigned char* buffer);
bool readSectorNTFS(HANDLE hVolume, unsigned char buffer[], uint64_t sector);
ITEM_NTFS parseMFTEntry(const unsigned char* buffer, int MFTstart);
vector<pair<int,int>> parseDataRuns(const unsigned char* dataRun, int maxLen);
bool recoverFileFromMFTA(HANDLE hVol, int mftEntrySector, const string& outputFile);
