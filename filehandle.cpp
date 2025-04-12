#include "filehandle.h"

int sS = 512, sC, sB, nF, sF, sD, sV;

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
    if (!ReadFile(hVolume, buffer, sS, &bytesRead, NULL) || bytesRead == 0) {
        DWORD error = GetLastError();
        std::cerr << "Reading sector error: " << error << std::endl;
        CloseHandle(hVolume);
        return 0;
    }

    return 1;
}


int getIntValue(unsigned char buffer[], int start, int size)
{
    int result = 0, coef = 1;
    for (int i = start; i <= start + size - 1; i++)
    {
        result += buffer[i] * coef;
        coef *= 256;
    }        
    return result;
}

//FOR FAT32---------------------------------------------------------------
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
    if (!WriteFile(hVolume, buffer, sS, &bytesWritten, NULL) || bytesWritten != sS) 
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

void getCertainFolder(HANDLE hVolume, int recentSector, vector<ITEM> &folders, vector<ITEM> &deletedFiles)
{
    folders.clear(); deletedFiles.clear();
    unsigned char sector[sS];
    readSector(hVolume, sector, recentSector);
    int entryIndex = 1;
    while (1)
    {
        if (entryIndex >= 16)
        {
            readSector(hVolume, sector, ++recentSector);
            entryIndex = 0;
        }

        ITEM newItem;
        int retval = readEntry(hVolume, sector, recentSector, entryIndex++, newItem);
        if (retval == 1)
            continue;
        if (retval == -1) 
            break;
        get<0>(newItem) = {recentSector, entryIndex - 1};
        
        if (get<2>(newItem) && !get<5>(newItem)) folders.push_back(newItem);
        else if (!get<2>(newItem) && get<5>(newItem)) deletedFiles.push_back(newItem);
    }
}


int readEntry(HANDLE hVolume, unsigned char sector[], int recentSector, int line, ITEM &item)
{
    int offset = line * 32; // Mỗi entry trong FAT32 có kích thước 32 byte
    std::string fullName;
    int entryIndex = line;

    unsigned char buffer[sS];
    for (int i = 0; i < sS; i++)
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
    bool isFolder = buffer[offset + 0xB] & (1 << 4);
    
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

int restoreItem(HANDLE hVolume, ITEM item)
{
    int sectorIndex = get<0>(item).first, offset = get<0>(item).second * 32;
    int cluster = get<4>(item), size = get<3>(item);

    //Restore cluster
    unsigned char FAT[sS];
    int fatSector = sB + cluster / 128;
    readSector(hVolume, FAT, fatSector);
    int nCluster = (size - 1)/(sC * sS) + 1, retval = 0, restoredCluster = 0;

    //check if the first cluster is written
    if (getIntValue(FAT, 4*(cluster % 128), 4) != 0) retval = -2;
    else
    {
        for (int i = 0; i < nCluster; i++)
        {
            int curCluster = cluster + i;
            int offset = 4 * (curCluster % 128);
            if (getIntValue(FAT, offset, 4) == 0)
            {
                restoredCluster++;
                int fatValue = (i == nCluster - 1) ? 0x0FFFFFFF : curCluster + 1;
                FAT[offset] = fatValue & 0xFF;        
                FAT[offset + 1] = (fatValue >> 8) & 0xFF;
                FAT[offset + 2] = (fatValue>> 16) & 0xFF;
                FAT[offset + 3] = (fatValue >> 24) & 0xFF; 

                //What if clusters are in two different sectors?
                if ((curCluster + 1) % 128 == 0)
                {
                    writeSector(hVolume, FAT, fatSector);
                    writeSector(hVolume, FAT, fatSector + sF);
                    readSector(hVolume, FAT, ++fatSector);
                }
            }
            else
            {
                retval = -1;
                break;
            }
        }

        writeSector(hVolume, FAT, fatSector);
        writeSector(hVolume, FAT, fatSector + sF);
    }

    //Read sector
    unsigned char sector[sS];
    readSector(hVolume, sector, sectorIndex);

    //Change byte E5
    if (retval == -2)
    {
        sector[offset + 0xB] = (1 << 4);
        writeSector(hVolume, sector, sectorIndex);
        return retval;
    }
    
    sector[offset] = 'A';
    int loffset = offset - 32, count = 0;
    while (loffset >= 0 && sector[loffset + 0xB] == 0x0F)
    {
       sector[loffset] = ++count;
       loffset -= 32;
    }
    
    int exCount = -1; unsigned char exSector[sS];
    if (loffset < 0)
    {
        exCount = 0;
        readSector(hVolume, exSector, sectorIndex - 1);
        loffset = sS - 32;
        while (exSector[loffset + 0xB] == 0x0F)
        {
            exSector[loffset] = ++count;
            ++exCount;
            loffset -= 32;
        }
    }

    if (exCount > 0)
    {   
        char ch;
        if (offset > 0) ch = sector[offset - 31];
        else ch = exSector[sS - 31];

        sector[offset] = ch - 0x20 * (ch >= 91 && ch <= 116);
        exSector[loffset + 32] += 0x40;
    }
    else if (count)
    {
        if (exCount == 0) loffset = -32; 
        char ch = sector[offset - 31];
        sector[offset] = ch - 0x20 * (ch >= 91 && ch <= 116);
        sector[loffset + 32] += 0x40;
    }
    writeSector(hVolume, sector, sectorIndex);
    if (exCount > 0) writeSector(hVolume, exSector, sectorIndex - 1);

    return retval;
}

//NTFS--------------------------------------------------------------
int BUFFER_SIZE = 4096;
int SECTOR_SIZE = 512;             // Kích thước 1 sector (thường 512 bytes)
int MFT_RECORD_SIZE = 1024;          // 1 MFT entry = 1024 bytes (2 sectors)
int CLUSTER_SIZE = 4096;             // Cluster size = 4096 bytes

bool isMFTEntry(const unsigned char* buffer) {
    // MFT entry phải bắt đầu với "FILE" (hex: 0x46 49 4C 45)
    const uint32_t MFT_SIGNATURE = 0x454C4946; // 'FILE' trong little-endian
    uint32_t signature = *(uint32_t*)buffer;  // Đọc 4 byte đầu

    return signature == MFT_SIGNATURE;
}

bool readSectorNTFS(HANDLE hVolume, unsigned char buffer[], uint64_t sector) {
    if (SECTOR_SIZE > BUFFER_SIZE) {
        cerr << "Error: BUFFER_SIZE quá nhỏ!" << endl;
        return false;
    }

    LARGE_INTEGER li;
    li.QuadPart = (int64_t)sector * SECTOR_SIZE;  // Chỉnh lại nhân với SECTOR_SIZE

    // Kiểm tra lỗi seek
    if (!SetFilePointerEx(hVolume, li, NULL, FILE_BEGIN)) {
        DWORD error = GetLastError();
        cerr << "Seeking error: " << error << " (Sector: " << sector << ")" << endl;
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(hVolume, buffer, SECTOR_SIZE, &bytesRead, NULL) || bytesRead != SECTOR_SIZE) {
        DWORD error = GetLastError();
        cerr << "Reading sector error: " << error << " (Sector: " << sector << ", Read: " << bytesRead << " bytes)" << endl;
        return false;
    }

    return true;
}

ITEM_NTFS parseMFTEntry(const unsigned char* buffer, int MFTstart) {
    // Lấy offset danh sách thuộc tính từ header (2 byte tại offset 0x14)
    uint16_t attrOffset = *(uint16_t*)(buffer + 0x14);
    // Lấy kích thước của entry (4 byte tại offset 0x1C)
    uint32_t entrySize = *(uint32_t*)(buffer + 0x1C);
    // Kiểm tra flag "in use": tại offset 0x16, nếu bit 0 (0x01) không bật => entry đã bị xoá.
    bool isDeleted = !(buffer[0x16] & 0x01);

    // Khởi tạo các giá trị mặc định
    string fileName = "Unknown";
    int fileSize = -1;
    int startCluster = -1;  // Sẽ lấy từ $DATA attribute nếu non-resident
    bool isFolder = false;
    bool isHidden = false;
    int tracker = 1; // Theo doi lay size trong FILE_NAME hay trong DATA 

    int offset = attrOffset;
    while (offset < (int)entrySize) {
        // Đọc loại thuộc tính (4 byte)
        uint32_t attrType = *(uint32_t*)(buffer + offset);
        if (attrType == 0xFFFFFFFF)
            break;  // Kết thúc danh sách thuộc tính

        // Lấy header của thuộc tính resident (có kích thước cố định)
        ResidentAttrHeader rHeader;
        //memcpy(&rHeader, buffer + offset, sizeof(ResidentAttrHeader));
        rHeader.type         = *(uint32_t*)(buffer + offset);
        rHeader.length       = *(uint32_t*)(buffer + offset + 0x04);
        rHeader.nameLength   = *(uint8_t*)(buffer + offset + 0x09);
        rHeader.valueLength  = *(uint32_t*)(buffer + offset + 0x10);
        rHeader.valueOffset  = *(uint16_t*)(buffer + offset + 0x14);

        if (rHeader.length == 0)
            break; // Ngăn vòng lặp vô hạn

        if (attrType == 0x10) { // $STANDARD_INFORMATION
            const unsigned char* valuePtr = buffer + offset + rHeader.valueOffset;
            uint32_t stdAttributes = *(uint32_t*)(valuePtr + 0x20); //<----- Error
            isHidden = (stdAttributes & 0x02) != 0;
        }
        else if (attrType == 0x30) { // $FILE_NAME
            const unsigned char* valuePtr = buffer + offset + rHeader.valueOffset;
            // Ở $FILE_NAME, file size (logical size) nằm tại offset 0x30 (8 bytes)
            uint64_t fnFileSize = *(uint64_t*)(valuePtr + 0x30);
            // Nếu giá trị này hợp lý (ví dụ, dưới 100MB), dùng nó làm fileSize.
            if (fnFileSize > 0 && fnFileSize < 100000000) {
                fileSize = (int)fnFileSize;
            }
            // Lấy tên file: offset 0x40 chứa độ dài tên (1 byte), tên bắt đầu tại offset 0x42 (UTF-16, mỗi ký tự 2 byte)
            uint8_t nameLen = *(uint8_t*)(valuePtr + 0x40);
            fileName = string((char*)(valuePtr + 0x42), nameLen * 2);
            // Đọc flag của $FILE_NAME (ở offset 0x38) để xác định nếu là thư mục.
            uint32_t fnFlags = *(uint32_t*)(valuePtr + 0x38);
            isFolder = (fnFlags & 0x10000000) != 0;
        }
        else if (attrType == 0x80) { // $DATA attribute
            uint8_t nrFlag = *(uint8_t*)(buffer + offset + 8);
            if (nrFlag) {
                // Non-resident $DATA: sử dụng NonResidentAttrHeader
                NonResidentAttrHeader nrHeader;
                //memcpy(&nrHeader, buffer + offset, sizeof(NonResidentAttrHeader));
                // Ở đây, nếu file non-resident, bạn có thể dùng initializedSize (nếu file sparse)
                nrHeader.startVCN = *(uint64_t*)(buffer + offset + 0x10);
                nrHeader.endVCN = *(uint64_t*)(buffer + offset + 0x18);
                nrHeader.dataRunOffset = *(uint16_t*)(buffer + offset + 0x20);
                nrHeader.allocatedSize = *(uint64_t*)(buffer + offset + 0x28);
                nrHeader.realSize = *(uint64_t*)(buffer + offset + 0x30);
                nrHeader.initializedSize = *(uint64_t*)(buffer + offset + 0x38);
                fileSize = (int)nrHeader.initializedSize;
                
                // Xử lý Data Run để lấy startCluster:
                uint16_t dataRunOffset = *(uint16_t*)(buffer + offset + 0x20);
                const unsigned char* dataRun = buffer + offset + dataRunOffset;
                uint8_t headerByte = *dataRun;
                uint8_t lengthSize = headerByte & 0x0F;
                uint8_t offsetSize = (headerByte >> 4) & 0x0F;
                if (lengthSize > 0 && offsetSize > 0) {
                    uint64_t clusterCount = 0;
                    memcpy(&clusterCount, dataRun + 1, lengthSize);
                    int64_t startClusterValue = 0;
                    memcpy(&startClusterValue, dataRun + 1 + lengthSize, offsetSize);
                    if (offsetSize > 0 && (startClusterValue & (1LL << ((offsetSize * 8) - 1)))) {
                        startClusterValue |= (-1LL << (offsetSize * 8));
                    }
                    startCluster = (int)startClusterValue;
                }
            } else {
                // Resident $DATA: sử dụng ResidentAttrHeader để lấy fileSize
                ResidentAttrHeader* rDataHeader = (ResidentAttrHeader*)(buffer + offset);
                fileSize = rDataHeader->valueLength;
            }
        }

        offset += rHeader.length;
    }

    return make_tuple(fileName, isFolder, fileSize, startCluster, isDeleted, isHidden, MFTstart);
}  

vector<pair<int,int>> parseDataRuns(const unsigned char* dataRun, int maxLen) {
    vector<pair<int,int>> runs;
    int pos = 0;
    int prevCluster = 0;

    while (pos < maxLen) {
        uint8_t header = dataRun[pos];

        if (header == 0)
            break; // Kết thúc data runs

        uint8_t lengthSize = header & 0x0F;      // Số byte của cluster count
        uint8_t offsetSize = (header >> 4) & 0x0F; // Số byte của relative offset
        pos++; // Bỏ qua byte header


        // Kiểm tra giá trị hợp lệ (bỏ điều kiện lengthSize > 4 để xem giá trị)
        if (pos + lengthSize + offsetSize > maxLen) {
            cerr << "Data run extends beyond available length! (pos=" << pos << ")" << endl;
            return {};
        }

        // Đọc cluster count (zero-extend)
        int clusterCount = 0;
        memcpy(&clusterCount, dataRun + pos, lengthSize);
        pos += lengthSize;

        // Đọc relative offset (sign-extend)
        int32_t relOffset = 0;
        if (offsetSize > 0) {
            //memcpy(&relOffset, dataRun + pos, offsetSize);
            relOffset = 0;
            for (int i = 0; i < offsetSize; i++) {
                relOffset |= (int32_t)dataRun[pos + i] << (i * 8);
            }
            // Xử lý sign extension nếu offsetSize < 4
            if (offsetSize < 4 && (relOffset & (1 << (offsetSize * 8 - 1)))) {
                relOffset |= ~((1 << (offsetSize * 8)) - 1);
            }
            // Sign extension
            int shift = 32 - (offsetSize * 8);
            relOffset = (relOffset << shift) >> shift;
            pos += offsetSize;
        }

        int absoluteCluster = prevCluster + relOffset;

        // Debug giá trị cluster
        // cout << "Run start cluster: " << absoluteCluster 
        //      << ", Run length: " << clusterCount << endl;

        if (clusterCount <= 0 || absoluteCluster < 0) {
            cerr << "Invalid data run detected!" << endl;
            return {};
        }
        // cout << "Header: " << (int)header 
        // << ", lengthSize: " << (int)lengthSize 
        // << ", offsetSize: " << (int)offsetSize << endl;
        // cout << "Parsed Run - Start Cluster: " << absoluteCluster 
        // << ", Length: " << clusterCount << endl;
        runs.emplace_back(absoluteCluster, clusterCount);
        prevCluster = absoluteCluster;
    }

    return runs;
}

bool recoverFileFromMFTA(HANDLE hVol, int mftEntrySector,const string& outputFile) {
    unsigned char mftBuffer[MFT_RECORD_SIZE] = {0};
    readSectorNTFS(hVol, mftBuffer, mftEntrySector);
    cout << outputFile << endl;
    uint16_t attrOffset = *(uint16_t*)(mftBuffer + 0x14);
    uint32_t entrySize = *(uint32_t*)(mftBuffer + 0x1C);
    bool foundData = false;
    bool nonResident = false;
    int fileSize = 0;
    int offset = attrOffset;
    
    while (offset < (int)entrySize) {
        uint32_t attrType = *(uint32_t*)(mftBuffer + offset);
        if (attrType == 0xFFFFFFFF) break;
        uint16_t attrLen = *(uint16_t*)(mftBuffer + offset + 4);
        if (attrLen == 0) break;
        
        if (attrType == 0x80) { // $DATA attribute
            uint8_t nrFlag = *(uint8_t*)(mftBuffer + offset + 8);
            if (nrFlag == 0) {
                ResidentAttrHeader* rDataHeader = (ResidentAttrHeader*)(mftBuffer + offset);
                int residentDataLength = rDataHeader->valueLength;
                const unsigned char* residentData = mftBuffer + offset + rDataHeader->valueOffset;
                fileSize = residentDataLength;
                nonResident = false;
                foundData = true;
                
                ofstream outFile(outputFile, ios::binary);
                if (!outFile) {
                    cerr << "Error creating output file: " << outputFile << endl;
                    return false;
                }
                outFile.write(reinterpret_cast<const char*>(residentData), residentDataLength);
                outFile.close();
                cout << "Recovered resident file successfully: " << outputFile << " (" << residentDataLength << " bytes)" << endl;
                return true;
            } else {
                NonResidentAttrHeader nrHeader;
                nrHeader.startVCN = *(uint64_t*)(mftBuffer + offset + 0x10);
                nrHeader.endVCN = *(uint64_t*)(mftBuffer + offset + 0x18);
                nrHeader.dataRunOffset = *(uint16_t*)(mftBuffer + offset + 0x20);
                nrHeader.allocatedSize = *(uint64_t*)(mftBuffer + offset + 0x28);
                nrHeader.realSize = *(uint64_t*)(mftBuffer + offset + 0x30);
                nrHeader.initializedSize = *(uint64_t*)(mftBuffer + offset + 0x38);
                fileSize = (int)nrHeader.initializedSize;
                nonResident = true;
                foundData = true;
                
                // cout << "NonResidentAttrHeader Debug:" << endl;
                // cout << "Start VCN: " << nrHeader.startVCN << endl;
                // cout << "End VCN: " << nrHeader.endVCN << endl;
                // cout << "Data Run Offset: " << (int)nrHeader.dataRunOffset << endl;
                // cout << "Allocated Size: " << nrHeader.allocatedSize << endl;
                cout << "Real Size: " << nrHeader.realSize << endl;
                // cout << "Initialized Size: " << nrHeader.initializedSize << endl;
                
                const unsigned char* dataRun = mftBuffer + offset + nrHeader.dataRunOffset;
                vector<pair<int, int>> runs = parseDataRuns(dataRun, attrLen - nrHeader.dataRunOffset);
                
                if (runs.empty()) {
                    cerr << "Error parsing data run." << endl;
                    return false;
                }
                cout << "File size detected: " << fileSize << " bytes" << endl;
                ofstream outFile(outputFile, ios::binary);
                if (!outFile) {
                    cerr << "Error creating output file!" << endl;
                    return false;
                }
                
                int bytesRemaining = fileSize; 
                int bytesWritten = 0;
                int sectorsPerCluster = 8; // Cần xác định giá trị chính xác
               
                for (auto& run : runs) {
                    int runStartCluster = run.first;
                    int runClusterCount = run.second;
                    uint64_t runBytes = (uint64_t)runClusterCount * CLUSTER_SIZE;
                    //uint64_t absOffset = (uint64_t)runStartCluster * CLUSTER_SIZE;
                    uint64_t absOffset = (uint64_t)runStartCluster * sectorsPerCluster * SECTOR_SIZE;
                    for (int i = 0; i < runBytes / SECTOR_SIZE && bytesRemaining > 0; i++) {
                        int sectorNum = (int)(absOffset / SECTOR_SIZE + i);
                        unsigned char sectorBuffer[SECTOR_SIZE] = {0};
                        //cout << "Debug: hVol = " << hVol << endl;
                        if (!readSectorNTFS(hVol, sectorBuffer, sectorNum)) {
                            cerr << "Error reading sector " << sectorNum << " from volume." << endl;
                            outFile.close();
                            return false;}
                        // }else {
                        //     cout << "Sector " << sectorNum << " read successfully, first bytes: ";
                        //     for (int j = 0; j < 16; j++) printf("%02X ", sectorBuffer[j]);
                        //     cout << endl;
                        // }
                    
                        int bytesToWrite = min(SECTOR_SIZE, bytesRemaining);
                        if (bytesToWrite <= 0) {
                            cerr << "Error: Attempting to write invalid byte count: " << bytesToWrite << endl;
                            break;
                        }
                        //cout << "Bytes written this cycle: " << bytesToWrite
                        //<< ", Remaining: " << bytesRemaining << endl;
                        if (bytesRemaining <= 0) {
                            cerr << "Error: No more bytes left to write!" << endl;
                            break;
                        }                        
                        outFile.write(reinterpret_cast<char*>(sectorBuffer), bytesToWrite);
                        bytesWritten += bytesToWrite;
                        if (bytesRemaining < bytesToWrite) {
                            bytesToWrite = bytesRemaining; // Chỉ ghi phần còn lại
                        }
                        else
                            bytesRemaining -= bytesToWrite;
                    }
                }
                
                outFile.close();
                cout << "Bytes written: " << bytesWritten << " / " << fileSize << endl;
                cout << "Recovered resident file successfully: " << outputFile << " (" << fileSize << " bytes)" << endl;
                return true;
            }
        }
        offset += attrLen;
    }
    
    cerr << "No valid $DATA attribute found." << endl;
    return false;
}

