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

extern int sS, sC, sB, nF, sF, sD;
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
            cout << i << "> " << get<1>(folders[i]) << "\n";
        cout << "\n\033[1;33mDELETED FILES:\033[0m\n";
        for (int i = 0; i < deletedFiles.size(); i++)
            cout << i << "> " << get<1>(deletedFiles[i]) << "\n";
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
                "\\\\.\\F:",     
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
            if (!num >= 0 && num <= deletedFiles.size() && !deletedFiles.empty()) continue;

            int retval = restoreItem(hVolume, deletedFiles[num]);
            if (retval == -1) cout << "Could not restore all data of the file!\n";
            else if (retval == 0) cout << "Restored successfully!\n";

            CloseHandle(hVolume);
            Sleep(500);
            hVolume = CreateFileA(
                "\\\\.\\F:",     
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

int main()
{
    char volumeChar;
    cout << "Type in volume's name (C/D/E/...): "; cin >> volumeChar;

    bool isNTFS = isNTFSVolume(volumeChar);
    bool isFAT32 = isFAT32Volume(volumeChar);

    if (isFAT32) doFAT(volumeChar);
    return 0;
}