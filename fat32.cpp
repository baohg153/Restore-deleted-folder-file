#include "filehandle.h"

using namespace std;

int sS, sB, nF, sF, sD;

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

int main() 
{
    //Create file 
    HANDLE hVolume = CreateFileA(
        "\\\\.\\F:",     
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
        return 1;
    }
    
    //Lock volume
    lockVolume(hVolume);

    //Read boot sector
    unsigned char bootSector[BUFFER_SIZE];
    readSector(hVolume, bootSector, 0);
    sS = getIntValue(bootSector, 0xB, 2);
    sB = getIntValue(bootSector, 0xE, 2);
    nF = getIntValue(bootSector, 0x10, 1);
    sF = getIntValue(bootSector, 0x24, 4);
    int firstDataSector = sB + nF * sF;
    std::cout << sS << ", " << sB << ", " << nF << ", " << sF << ": " << firstDataSector << "\n\n";


    //Read all files in RDET
    unsigned char sector[sS];
    int recentSector = firstDataSector;
    readSector(hVolume, sector, recentSector);
    vector<ITEM> files;
    int entryIndex = 0;
    while (1)
    {
        if (entryIndex >= 16)
        {
            readSector(hVolume, sector, ++recentSector);
            entryIndex = 0;
        }

        ITEM newFile;
        int retval = readEntry(sector, entryIndex++, newFile);
        if (retval == 1)
            continue;
        if (retval == -1) 
            break;
        
        files.push_back(newFile);

        cout << get<0>(newFile) << " | " << get<1>(newFile) << " | " << get<2>(newFile) << " | "
             << get<3>(newFile) << " | " << get<4>(newFile) << " | " << "\n";
    }

    //Unlock volume and finish
    unlockVolume(hVolume);
    CloseHandle(hVolume);
    return 0;
}   