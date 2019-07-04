#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "fat32.h"

//function prototype
void regBS();
void setBSField(void*, int);

typedef enum BS_offset{
    BS_jmpBoot = 0,
	BS_OEMName = 3,
	BPB_BytesPerSec = 11,
	BPB_SecPerClus = 13,
	BPB_RsvdSecCnt = 14,
	BPB_NumFATs = 16,
	BPB_RootEntCnt = 17,
	BPB_TotSec16 = 19,
	BPB_Media = 21,
	BPB_FATSz16 = 22,
	BPB_SecPerTrk = 24,
	BPB_NumHeads = 26,
	BPB_HiddSec = 28,
	BPB_TotSec32 = 32,
	BPB_FATSz32 = 36,
	BPB_ExtFlags = 40,
	BPB_FSVerLow = 42,
	BPB_FSVerHigh = 42,
	BPB_RootClus = 44,
	BPB_FSInfo = 48,
	BPB_BkBootSec = 50,
	BPB_reserved = 52,
	BS_DrvNum = 64,
	BS_Reserved1 = 65,
	BS_BootSig = 66,
	BS_VolID = 67,
	BS_VolLab = 71
	// BS_FilSysType = 82，
	// char BS_CodeReserved[420];
	// uint8_t BS_SigA = sizeof(uint8_t),
	// uint8_t BS_SigB = sizeof(uint8_t),

}bs_offset;

//global variable
fat32BS bs;
int fd;

int main(){
    fd = open("a4image", 0);
    regBS();
    printf("%s", bs.BS_OEMName);
    return 0;
}

void regBS(){
    setBSField(bs.BS_jmpBoot, sizeof(bs.BS_jmpBoot));
    setBSField(bs.BS_OEMName, sizeof(bs.BS_OEMName));
}

void setBSField(void *BS_field, int offset){
    read(fd, BS_field, sizeof(*BS_field));
}

