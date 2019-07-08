#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include "fat32.h"
#include <string.h>

#define FAT_ADDR_SIZE 4
#define FILE_TOTAL_NAME_LENGTH 11
#define FILE_NAME_LENGTH 8
#define FILE_EXT_LENGTH 3
#define FILE_DIR_ENTRY_SIZE 32
#define DIR_CLUSTER_SIZE 4
#define END_OF_DIR 0x00
#define FILE_ATTR_BIT_OFFSET 11

#define UNUSED_FILE 0xE5
#define END_OF_FILE 0x0FFFFFFF

#define FILE_RDONLY 0x01
#define FILE_HIDDEN 0x02
#define FILE_SYSFILE 0x04
#define FILE_VOLLAB 0x08
#define FILE_SUBDIR 0x10
#define FILE_ARCHIVE 0x20

#define DEPTH_MARK '-'

typedef uint32_t fat_addr;

typedef uint32_t cluster_num;


//typedef struct FSInfo{
//
//}FSInfo;


typedef struct fat32Dir{
    char name[FILE_TOTAL_NAME_LENGTH];
    char ext[FILE_EXT_LENGTH];
    char attr;
    uint32_t start_cluster_num;
    uint32_t size;
}fat32Dir;


//function prototype
void regBS();
void info();
void list();
fat32Dir *getDir(const uint8_t bin[FILE_DIR_ENTRY_SIZE]);
cluster_num getNextClusterNum(cluster_num fat_offset_num);
char *makeDepth(int);
char* getFileFullName(fat32Dir *);


//constant
const char PARENT_ENTRY_NAME[FILE_TOTAL_NAME_LENGTH] = {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};
const char CURRENT_ENTRY_NAME[FILE_TOTAL_NAME_LENGTH] = {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};


//global variable
fat32BS bs;
int fd;
uint32_t sectors_per_cluster, root_dir_first_cluster;
off_t fat_begin_lba, cluster_begin_lba;
uint16_t byte_per_sector, byte_per_cluster;

int main(){
    fd = open("C:\\Users\\ypeng\\CLionProjects\\fat32readonly\\a4image", O_RDONLY);
    assert(fd >= 0);

    regBS();
    info();
    list(bs.BPB_RootClus-2, 0);
    return 0;
}

void info(){
    char driveName[sizeof(bs.BS_VolLab)+10];
    sprintf(driveName, "Driver Name: %s", bs.BS_VolLab);
    assert(write(STDOUT_FILENO, driveName, sizeof(driveName)) > -1);

    uint32_t curFAT, usedFATs = 0, total_fat_bytes = bs.BPB_FATSz32*byte_per_sector;
    uint32_t fat_num = total_fat_bytes/FAT_ADDR_SIZE;
    assert(lseek(fd, fat_begin_lba*byte_per_sector, SEEK_SET) > -1);
    for(unsigned int i=0; i < fat_num; i++){
        assert(read(fd, &curFAT, FAT_ADDR_SIZE));
        if(curFAT!=0){
            usedFATs++;
        }
    }

    printf("\nFree space: %d KB", (fat_num-usedFATs)*sectors_per_cluster*byte_per_sector);

    printf("\nUsable storage: %lu KB", bs.BPB_TotSec32*byte_per_sector-(cluster_begin_lba*byte_per_sector));

    printf("\nCluster size: %d sector(s) / %d KB", bs.BPB_SecPerClus, bs.BPB_SecPerClus*byte_per_sector);
}

void list(cluster_num clusterNum, int depth){
    off_t cluster_start_byte = cluster_begin_lba*byte_per_sector+clusterNum*byte_per_cluster;
    fat32Dir *curDir;
    uint8_t file[FILE_DIR_ENTRY_SIZE];
    int dir_offset = 0;
    char *depthStr = makeDepth(depth);

    do{
        assert(lseek(fd, cluster_start_byte+dir_offset, SEEK_SET) > -1);
        dir_offset+=FILE_DIR_ENTRY_SIZE;
        assert(read(fd, file, FILE_DIR_ENTRY_SIZE) > -1 );
        curDir = getDir(file);

        //if current file entry is NOT unused or EOD
        if( (uint8_t)file[0] != UNUSED_FILE && file[0] != END_OF_DIR && (file[FILE_ATTR_BIT_OFFSET]&FILE_VOLLAB)==0){
            //if this file is directory
            if((file[FILE_ATTR_BIT_OFFSET]&FILE_SUBDIR) != 0){

                if(depth != 0)
                    printf("\n%s%s%.*s", depthStr,"Directory: ",FILE_TOTAL_NAME_LENGTH,curDir->name);
                else
                    printf("\n%s%.*s", depthStr,FILE_TOTAL_NAME_LENGTH,curDir->name);

                //if current directory does not points to parent or current directory
                if( (strcmp(curDir->name,PARENT_ENTRY_NAME)!=0) && strcmp(curDir->name, CURRENT_ENTRY_NAME) != 0){
                    cluster_num curClusterNum = curDir->start_cluster_num;
                    //traverse all chained clusters
                    while(curClusterNum != END_OF_FILE){
                        list(curClusterNum-2, depth+1);
                        curClusterNum = getNextClusterNum(curClusterNum);
                    }
                }

            }else{
                char *fullName = getFileFullName(curDir);
                if(depth == 0)
                    printf("\n%s%s", depthStr,fullName);
                else
                    printf("\n%s%s%s", depthStr,"File: ",fullName);

                free(fullName);

            }
        }
    }while(dir_offset<byte_per_cluster && file[0] != END_OF_DIR);

    free(depthStr);
}

char *makeDepth(int depth){
    char *depthStr = malloc(sizeof(char)*depth+1);
    for(int i=0; i<=depth; i ++){
        depthStr[i]=DEPTH_MARK;
        if(i == depth)
            depthStr[i] = '\0';
    }

    return depthStr;
}

uint32_t getNextClusterNum(cluster_num fat_offset_num){
    cluster_num cluster_num;
    off_t fat_offset_byte = fat_begin_lba*byte_per_sector+fat_offset_num*FAT_ADDR_SIZE;
    lseek(fd, fat_offset_byte, SEEK_SET);
    read(fd, &cluster_num, sizeof(fat_addr));

    //mask the top 4 bit
    cluster_num = cluster_num & 0xFFFFFFF;

    return cluster_num;
}

char *getFileFullName(fat32Dir *file){
    char *fullName = malloc(FILE_TOTAL_NAME_LENGTH+2);
    char ext[FILE_EXT_LENGTH];
    memcpy(ext, file->name+FILE_NAME_LENGTH, sizeof(ext));
    memcpy(fullName, file->name, sizeof(file->name));

    for(int i=sizeof(fullName)-1-sizeof(ext); i>=0; i--){
        if(fullName[i]!=' '){
            fullName[i+1]='.';
            fullName[i+2]=ext[0];
            fullName[i+3]=ext[1];
            fullName[i+4]=ext[2];
            fullName[i+5]='\0';
            break;
        }
    }

    return fullName;
}

//void remove_spaces(char* s) {
//    const char* d = s;
//    do {
//        while (*d == ' ') {
//            ++d;
//        }
//    } while (*s++ = *d++);
//}

fat32Dir *getDir(const uint8_t bin[FILE_DIR_ENTRY_SIZE]){
    fat32Dir* fat32Dir = malloc(sizeof(fat32Dir));
    assert(fat32Dir != NULL);
    //set name with first 11 bytes in bin
    memcpy(fat32Dir->name, bin, sizeof(fat32Dir->name));
    //set attr with 1 byte follow name bytes
    fat32Dir->attr = bin[sizeof(fat32Dir->name)];
    //set cluster number
//    char clusterNum[DIR_CLUSTER_SIZE];
    memcpy(&(fat32Dir->start_cluster_num)+2, bin+20, DIR_CLUSTER_SIZE/2);
    memcpy(&(fat32Dir->start_cluster_num), bin+26, DIR_CLUSTER_SIZE/2);
//    fat32Dir->start_cluster_num = *((uint32_t *)&clusterNum);
    //set cluster size
    memcpy(&(fat32Dir->size), bin+28, sizeof(fat32Dir->size));
    return fat32Dir;
}

void regBS(){
    read(fd, bs.BS_jmpBoot, sizeof(bs.BS_jmpBoot));
    read(fd, bs.BS_OEMName, sizeof(bs.BS_OEMName));
    read(fd, &bs.BPB_BytesPerSec, sizeof(bs.BPB_BytesPerSec));
    read(fd, &bs.BPB_SecPerClus, sizeof(bs.BPB_SecPerClus));
    read(fd, &bs.BPB_RsvdSecCnt, sizeof(bs.BPB_RsvdSecCnt));
    read(fd, &bs.BPB_NumFATs, sizeof(bs.BPB_NumFATs));
    read(fd, &bs.BPB_RootEntCnt, sizeof(bs.BPB_RootEntCnt));
    read(fd, &bs.BPB_TotSec16, sizeof(bs.BPB_TotSec16));
    read(fd, &bs.BPB_Media, sizeof(bs.BPB_Media));
    read(fd, &bs.BPB_FATSz16, sizeof(bs.BPB_FATSz16));
    read(fd, &bs.BPB_SecPerTrk, sizeof(bs.BPB_SecPerTrk));
    read(fd, &bs.BPB_NumHeads, sizeof(bs.BPB_NumHeads));
    read(fd, &bs.BPB_HiddSec, sizeof(bs.BPB_HiddSec));
    read(fd, &bs.BPB_TotSec32, sizeof(bs.BPB_TotSec32));
    read(fd, &bs.BPB_FATSz32, sizeof(bs.BPB_FATSz32));
    read(fd, &bs.BPB_ExtFlags, sizeof(bs.BPB_ExtFlags));
    read(fd, &bs.BPB_FSVerHigh, sizeof(bs.BPB_FSVerHigh));
    read(fd, &bs.BPB_FSVerLow, sizeof(bs.BPB_FSVerLow));
    read(fd, &bs.BPB_RootClus, sizeof(bs.BPB_RootClus));
    read(fd, &bs.BPB_FSInfo, sizeof(bs.BPB_FSInfo));
    read(fd, &bs.BPB_BkBootSec, sizeof(bs.BPB_BkBootSec));
    read(fd, bs.BPB_reserved, sizeof(bs.BPB_reserved));
    read(fd, &bs.BS_DrvNum, sizeof(bs.BS_DrvNum));
    read(fd, &bs.BS_Reserved1, sizeof(bs.BS_Reserved1));
    read(fd, &bs.BS_BootSig, sizeof(bs.BS_BootSig));
    read(fd, &bs.BS_VolID, sizeof(bs.BS_VolID));
    read(fd, bs.BS_VolLab, sizeof(bs.BS_VolLab));
    read(fd, bs.BS_FilSysType, sizeof(bs.BS_FilSysType));

    root_dir_first_cluster = bs.BPB_RootClus;
    sectors_per_cluster = bs.BPB_SecPerClus;
    cluster_begin_lba = bs.BPB_RsvdSecCnt+(bs.BPB_FATSz32*bs.BPB_NumFATs);
    fat_begin_lba = bs.BPB_RsvdSecCnt;
    byte_per_sector = bs.BPB_BytesPerSec;
    byte_per_cluster = bs.BPB_SecPerClus*byte_per_sector;
}

