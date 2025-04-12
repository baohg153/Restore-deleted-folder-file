#include "filehandle.h"
#include <stack>

bool isNTFSVolume(char driveLetter) {
    char fileSystemNameBuffer[MAX_PATH] = {0};
    char volumeNameBuffer[MAX_PATH] = {0};
    DWORD serialNumber = 0, maxComponentLen = 0, fileSystemFlags = 0;

    string strDriveLetter(1, driveLetter);
    std::string rootPath = strDriveLetter + ":\\\\";

    BOOL success = GetVolumeInformationA(
        rootPath.c_str(),
        volumeNameBuffer,
        sizeof(volumeNameBuffer),
        &serialNumber,
        &maxComponentLen,
        &fileSystemFlags,
        fileSystemNameBuffer,
        sizeof(fileSystemNameBuffer)
    );

    if (!success) {
        std::cerr << "GetVolumeInformationA failed with error: " << GetLastError() << std::endl;
        return false;
    }

    return (std::string(fileSystemNameBuffer) == "NTFS");
}

extern int sS, sC, sB, nF, sF, sD, sV;
bool isFAT32Volume(char driveLetter)
{
    char path[7] = "\\\\.\\"; path[4] = driveLetter; path[5] = ':'; path[6] = '\0';

    HANDLE hVolume = CreateFileA(
        path,     
        GENERIC_READ | GENERIC_WRITE , //| WRITE_DAC | WRITE_OWNER,   
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
        NULL, 
        OPEN_EXISTING, 
        FILE_FLAG_NO_BUFFERING, 
        NULL
    );

    unsigned char bootSector[sS];
    readSector(hVolume, bootSector, 0);
    string check_str = "FAT32";
    for (int i = 0; i < 5; i++)
        if (bootSector[0x52 + i] != check_str[i])
        {
            CloseHandle(hVolume);
            return 0;
        }

    CloseHandle(hVolume);
    return 1;
}

//convert from string to wstring (for Vietnamese name)
std::wstring stringToWstring(const std::string& str)
{
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size_needed - 1, 0); // -1 vì không cần ký tự null
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
}

void doFAT(char volumeChar) 
{
    char path[7] = "\\\\.\\"; path[4] = volumeChar; path[5] = ':'; path[6] = '\0';
    //Create file 
    HANDLE hVolume = CreateFileA(
        path,     
        GENERIC_READ | GENERIC_WRITE , //| WRITE_DAC | WRITE_OWNER,   
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
        NULL, 
        OPEN_EXISTING, 
        FILE_FLAG_NO_BUFFERING, 
        NULL
    );

    if (hVolume == INVALID_HANDLE_VALUE) 
    {
        std::cerr << "Lỗi khi mở volume: " << GetLastError() << std::endl;
        return;
    }

    //Read boot sector
    unsigned char bootSector[sS];
    readSector(hVolume, bootSector, 0);
    sS = getIntValue(bootSector, 0xB, 2);
    sC = getIntValue(bootSector, 0xD, 1);
    sB = getIntValue(bootSector, 0xE, 2);
    nF = getIntValue(bootSector, 0x10, 1);
    sF = getIntValue(bootSector, 0x24, 4);
    int firstDataSector = sB + nF * sF;
    std::cout << sS << ", " << sB << ", " << nF << ", " << sF << ": " << firstDataSector << "\n\n";

    //Print folders and deleted files in certain folder
    vector<ITEM> folders, deletedFiles;
    int recentSector = firstDataSector;
    stack<int> prevSectors;


    string currentPath(1, volumeChar); currentPath = currentPath + ":/"; 
    while (1)
    {
        system("cls");
        std::cout << "\033[1;34mPATH: \033[34m" << currentPath << "\033[0m\n-------------------------------------------------------\n";
        getCertainFolder(hVolume, recentSector, folders, deletedFiles);

        //print folders and deleted files
        cout << "\033[1;33mFOLDERS:\033[0m \n0>..\n";
        for (int i = 1; i< folders.size(); i++)
            wcout << i << "> " << stringToWstring(get<1>(folders[i])) << "\n";
        cout << "\n\033[1;33mDELETED FILES:\033[0m\n";
        for (int i = 0; i < deletedFiles.size(); i++)
            wcout << i << "> " << stringToWstring(get<1>(deletedFiles[i])) << "\n";
        cout << "\n \033[0;32m Commands: \"cd [num]\" or \"restore [num]\" or \"quit\" or \"reset\" \n> \033[0m";

        //Type in command
        string cmd; int num;
        cin >> cmd;
        if (cmd == "quit") break;
        else if (cmd == "reset")
        {
            cout << "Resetting...\n";
            CloseHandle(hVolume);
            Sleep(500);
            hVolume = CreateFileA(
                path,     
                GENERIC_READ | GENERIC_WRITE , //| WRITE_DAC | WRITE_OWNER,   
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                NULL, 
                OPEN_EXISTING, 
                FILE_FLAG_NO_BUFFERING, 
                NULL
            );

            continue;
        }

        //Process command
        if (cmd == "cd")
        {
            cin >> num;
            if (!(num == 0 || (num >= 0 && num < folders.size()))) continue;
            if (num == 0)
            {
                if (prevSectors.empty()) continue;

                recentSector = prevSectors.top();
                prevSectors.pop();
                
                //change currentPath
                int pos = currentPath.size() - 2;
                while (currentPath[pos--] != '/');
                currentPath.erase(pos + 2);

                continue;
            }

            prevSectors.push(recentSector);
            recentSector = firstDataSector + (get<4>(folders[num]) - 2)*4;
            currentPath = currentPath + get<1>(folders[num]) + "/";
        }
        else if (cmd == "restore")
        {
            cin >> num;
            if (!(num >= 0 && num <= deletedFiles.size() && !deletedFiles.empty())) continue;

            int retval = restoreItem(hVolume, deletedFiles[num]);\
            if (retval == -2) cout << "Could not restore file because the first cluster has been overwritten!\n";
            else if (retval == -1) cout << "Could not restore all data of the file!\n";
            else if (retval == 0) cout << "Restored successfully!\n";

            CloseHandle(hVolume);
            Sleep(500);
            hVolume = CreateFileA(
                path,     
                GENERIC_READ | GENERIC_WRITE , //| WRITE_DAC | WRITE_OWNER,   
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                NULL, 
                OPEN_EXISTING, 
                FILE_FLAG_NO_BUFFERING, 
                NULL
            );
            system("pause");
        }   
    }

    CloseHandle(hVolume);
    return;
}

extern int BUFFER_SIZE, SECTOR_SIZE, MFT_RECORD_SIZE, CLUSTER_SIZE;
void doNTFS(char volumeChar){
    char path[7] = "\\\\.\\"; path[4] = volumeChar; path[5] = ':'; path[6] = '\0';
    //Create file 
    HANDLE hVolume = CreateFileA(
        path,     
        GENERIC_READ | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER,        
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, 
        OPEN_EXISTING, 
        0, 
        NULL
    );

    if (hVolume == INVALID_HANDLE_VALUE) 
    {
        std::cerr << "Lỗi khi mở volume: " << GetLastError() << std::endl;
        return;
    }

    // lockVolume(hVolume);
    unsigned char VolumeSector[BUFFER_SIZE];
    readSectorNTFS(hVolume, VolumeSector, 0);
    sS = getIntValue(VolumeSector, 0xB, 2); // Kích thước của một sector
    sC = getIntValue(VolumeSector, 0xD, 1) ;// Kich thuoc cua mot cluster (tinh bang sector)
    sV = getIntValue(VolumeSector, 0x28, 8) ;// So sector trong o dia
    int MFTstart = getIntValue(VolumeSector, 0x30, 8) * sC; // Cluster bat dau cua MFT
    int MFTMirrorStart = getIntValue(VolumeSector, 0x38, 8) * sC; // Cluster bat dau cua MFT Du phong

    // Giá trị hex 0xF6
    unsigned char hexValue = VolumeSector[0x40];
    // Chuyển đổi sang số có dấu (sử dụng ép kiểu)
    signed char signedValue = static_cast<signed char>(hexValue);
    // Tính giá trị tuyệt đối của số có dấu
    int absoluteValue = abs(signedValue);
    int MFTRecordSize = pow(2, absoluteValue);     // Tính kích thước của bản ghi MFT

    //BUFFER_SIZE = sS;
    cout << sS << " " << sC << " " << sV << " " << MFTstart << " " << MFTRecordSize << " " << MFTMirrorStart << endl;
    unsigned char MFTEntryBuffer[CLUSTER_SIZE];
    vector<ITEM_NTFS> item;

    reset:
    unsigned long long tracker = MFTstart - MFTMirrorStart;
    unsigned long long step = 0;
    unsigned long long count = 0; //Count to stop
    while(tracker > 0){
        readSectorNTFS(hVolume, MFTEntryBuffer, MFTstart + step);
        if(!isMFTEntry(MFTEntryBuffer)){
            cout << '|';
            count++;
        }
        else
            count = 0;
        if(count > 10)
            break;
        // cout << 1;
        
        cout << tracker << " | " << MFTEntryBuffer << ", " << MFTstart << ", " << step << "\n";
        ITEM_NTFS result = parseMFTEntry(MFTEntryBuffer, MFTstart + step);
        // cout << 4 << " ";
        if(get<5>(result) == 0 & get<0>(result) != "Unknown" & get<4>(result)){
            item.push_back(result);
        }
        step += 2;
        tracker -= 2;
        cout << tracker << endl;
    }

    string currentPath(1, volumeChar); currentPath = currentPath + ":/"; 
    while (1)
    {
        system("cls");
        std::cout << "\033[1;34mPATH: \033[34m" << currentPath << "\033[0m\n-------------------------------------------------------\n";
        //print deleted files
        cout << "\n\033[1;33mDELETED FILES:\033[0m\n";
        for (unsigned long long i = 0; i < item.size(); i++)
            cout << i << "> " << get<0>(item[i]) << " - Size: " << get<2>(item[i]) << "\n";
        cout << "\n \033[0;32m Commands: \"restore [num]\" or \"quit\" or \"reset\" \n> \033[0m";

        //Type in command
        string cmd; int num;
        cin >> cmd;
        if (cmd == "quit") break;
        else if (cmd == "reset")
        {
            cout << "Resetting...\n";
            CloseHandle(hVolume);
            Sleep(500);
            hVolume = CreateFileA(
                path,     
                GENERIC_READ | GENERIC_WRITE , //| WRITE_DAC | WRITE_OWNER,   
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                NULL, 
                OPEN_EXISTING, 
                FILE_FLAG_NO_BUFFERING, 
                NULL
            );
            item.clear();
            goto reset;
        }

        //Process command
        if (cmd == "restore")
        {
            cin >> num;
            // cout << "Where to save (C/D/E/...) - EXCEPT recovery drive: ";
            // char drive;
            // cin >> drive; 
            string saveIn = "";
            saveIn = string(1, volumeChar) + ":\\" + get<0>(item[num]);

            size_t pos = saveIn.find('\0');
            while (pos != std::string::npos) {
                saveIn.erase(pos, 1); // Xoá từ vị trí '\0' trở đi
                pos = saveIn.find('\0');
            }
            
            replace(saveIn.begin(), saveIn.end(), ' ', '_');

            int retval = recoverFileFromMFTA(hVolume, get<6>(item[num]), saveIn);
            DWORD bytesReturned;
            if (retval) 
                cout << "";
            else
            {
                //DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
                cout << "Could not restore all data of the file!\n";
            }

            CloseHandle(hVolume);
            Sleep(500);
            hVolume = CreateFileA(
                path,     
                GENERIC_READ | GENERIC_WRITE , //| WRITE_DAC | WRITE_OWNER,   
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
                NULL, 
                OPEN_EXISTING, 
                FILE_FLAG_NO_BUFFERING, 
                NULL
            );
            system("pause");
            item.clear();
            goto reset;
        }   
    }

    // unlockVolume(hVolume);
    CloseHandle(hVolume);
}


int main()
{
    char volumeChar;
    cout << "Type in volume's name (C/D/E/...): "; cin >> volumeChar;

    bool isNTFS = isNTFSVolume(volumeChar);
    bool isFAT32 = isFAT32Volume(volumeChar);

    if (isFAT32) doFAT(volumeChar);
    if (isNTFS) doNTFS(volumeChar);

    return 0;
}