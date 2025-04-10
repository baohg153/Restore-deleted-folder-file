#include "filehandle.h"
#include <stdlib.h>
#include <stack>

using namespace std;

int sS, sC, sB, nF, sF, sD;
extern int BUFFER_SIZE_FAT;

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

int restoreItem(HANDLE hVolume, ITEM item)
{
    int sectorIndex = get<0>(item).first, offset = get<0>(item).second * 32;
    int cluster = get<4>(item), size = get<3>(item);

    //Read sector
    unsigned char sector[sS];
    readSector(hVolume, sector, sectorIndex);

    //Change byte E5
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

    //Restore cluster
    unsigned char FAT[sS];
    int fatSector = sB + cluster / 128;
    readSector(hVolume, FAT, fatSector);
    int nCluster = (size - 1)/(sC * sS) + 1, retval = 0, restoredCluster = 0;
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

    // resetVolume(hVolume, "\\\\.\\F:");

    return retval;
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
        
        if (get<2>(newItem)) folders.push_back(newItem);
        else if (get<5>(newItem)) deletedFiles.push_back(newItem);
    }
}

int main() 
{
    //Create file 
    HANDLE hVolume = CreateFileA(
        "\\\\.\\F:",     
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
        return 1;
    }
    
    //Lock volume
    // lockVolume(hVolume);
    //Check if FAT32
    bool isFAT = 1;
    unsigned char bootSector[BUFFER_SIZE_FAT];
    readSector(hVolume, bootSector, 0);
    string check_str = "FAT32";
    for (int i = 0; i < 5; i++)
        if (bootSector[0x52 + i] != check_str[i]){ isFAT = 0; break;}

    //Read boot sector
    // unsigned char bootSector[BUFFER_SIZE_FAT];
    // readSector(hVolume, bootSector, 0);
    sS = getIntValue(bootSector, 0xB, 2);
    sC = getIntValue(bootSector, 0xD, 1);
    sB = getIntValue(bootSector, 0xE, 2);
    nF = getIntValue(bootSector, 0x10, 1);
    sF = getIntValue(bootSector, 0x24, 4);
    int firstDataSector = sB + nF * sF;
    std::cout << sS << ", " << sB << ", " << nF << ", " << sF << ": " << firstDataSector << "\n\n";
    BUFFER_SIZE_FAT = sS;

    //Print folders and deleted files in certain folder
    vector<ITEM> folders, deletedFiles;
    int recentSector = firstDataSector;
    stack<int> prevSectors;
    while (1)
    {
        system("cls");
        getCertainFolder(hVolume, recentSector, folders, deletedFiles);

        //print folders and deleted files
        cout << "FOLDERS: \n0>..\n";
        for (int i = 1; i < folders.size(); i++)
            cout << i << "> " << get<1>(folders[i]) << "\n";
        cout << "\nDELETED FILES: \n";
        for (int i = 0; i < deletedFiles.size(); i++)
            cout << i << "> " << get<1>(deletedFiles[i]) << "\n";
        cout << "\n Commands: \"cd [num]\" or \"restore [num]\" or \"quit\" or \"reset\"\n> ";

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
        cin >> num;

        //Process command
        if (cmd == "cd" && (num == 0 || (num >= 0 && num < folders.size())))
        {
            if (num == 0)
            {
                if (prevSectors.empty()) continue;

                recentSector = prevSectors.top();
                prevSectors.pop();
                continue;
            }

            prevSectors.push(recentSector);
            recentSector = firstDataSector + (get<4>(folders[num]) - 2)*4;
        }
        else if (cmd == "restore" && num >= 0 && num <= deletedFiles.size() && !deletedFiles.empty())
        {
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
    return 0;
}   