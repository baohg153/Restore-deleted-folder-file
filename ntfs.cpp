#include "filehandleNTFS.h"
#include <cmath>

int sS, sC, sV;
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
        "\\\\.\\E:",     
        GENERIC_READ | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER,        
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, 
        OPEN_EXISTING, 
        0, 
        NULL
    );


    //HANDLE hVolume = CreateFile(L"\\\\.\\E:", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);


    if (hVolume == INVALID_HANDLE_VALUE) 
    {
        std::cerr << "Lỗi khi mở volume: " << GetLastError() << std::endl;
        return 1;
    }
    
    //Lock volume
    lockVolume(hVolume);

    //Read boot sector
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
    
    unsigned char MFTEntryBuffer[MFTRecordSize];
    vector<ITEM> item;
    int tracker = MFTstart - MFTMirrorStart;
    int step = 0;
    int count = 0; //Count to stop
    // while(tracker){
    //     readSectorNTFS(hVolume, MFTEntryBuffer, MFTstart + step);
    //     if(!isMFTEntry(MFTEntryBuffer)){
    //         count++;
    //     }
    //     else
    //         count = 0;
    //     if(count > 5)
    //         break;
    //     ITEM result = parseMFTEntry(MFTEntryBuffer, MFTstart + step);
    //     if(get<5>(result) == 0 & get<0>(result) != "Unknown" & get<4>(result)){
    //         item.push_back(result);
    //     }
        
    //     step += 2;
    //     tracker -= 2;
    // }
    

    // for(int i = 0; i < item.size(); i++){
    //     cout << "File name: " << get<0>(item[i]) << endl;
    //     cout << "isFolder: " << (get<1>(item[i]) ? "Y" : "N") << endl;
    //     cout << "Size: " << get<2>(item[i]) << " bytes" << endl;
    //     cout << "Start Cluster: " << get<3>(item[i]) << endl;
    //     cout << "isDeleted: " << (get<4>(item[i]) ? "Y" : "N") << endl;
    //     cout << "isHidden: " << (get<5>(item[i]) ? "Y" : "N") << endl;
    //     cout << "MFT Start: " << get<6>(item[i]) << endl;
    //     cout << endl;
    // }


    //Phục hồi file TRILE.txt từ MFT entry
    DWORD bytesReturned;
    if (!recoverFileFromMFTA(hVolume, 682760, "C:\\RRV2N46.png")) {
        cerr << "File recovery failed." << endl;
        DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesReturned, NULL);
        CloseHandle(hVolume);
        return 1;
    }

    //Unlock volume and finish
    unlockVolume(hVolume);
    CloseHandle(hVolume);
    return 0;
}   


