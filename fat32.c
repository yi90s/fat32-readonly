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
#include <stdbool.h>

#define FAT_ADDR_SIZE 4
#define FILE_TOTAL_NAME_LENGTH 11
#define FILE_NAME_LENGTH 8
#define FILE_EXT_LENGTH 3
#define ENTRY_SIZE 32
#define DIR_CLUSTER_SIZE 4
#define END_OF_DIR 0x00
#define FILE_ATTR_BIT_OFFSET 11
#define KB_IN_BYTES 1021

#define UNUSED_FILE 0xE5
#define END_OF_FILE 0x0FFFFFFF

#define FILE_RDONLY 0x01
#define FILE_HIDDEN 0x02
#define FILE_SYSFILE 0x04
#define FILE_VOLLAB 0x08
#define FILE_SUBDIR 0x10
#define FILE_ARCHIVE 0x20

#define DEPTH_MARK '-'
#define MAX_PATH_DEPTH 20
#define MAX_PATH_LENGTH 200

#define INFO_OUT_PATH "info.txt"
#define LIST_OUT_PATH "list.txt"
#define FAT32_IMAGE_PATH "a4image"
#define OUT_PATH ""


typedef uint32_t fat_addr;
typedef uint32_t cluster_num;


typedef struct FSInfo{
    uint32_t FSI_LeadSig;
    char FSI_Reserved1[480];
    uint32_t FSI_StrucSig;
    uint32_t FSI_Free_Count;
    uint32_t FSI_Nxt_Free;
    char FSI_Reserved2[12];
    uint32_t FSI_TrailSig;
}FSInfo;


typedef struct fat32Dir{
    char name[FILE_TOTAL_NAME_LENGTH];
    char ext[FILE_EXT_LENGTH];
    uint8_t attr;
    uint32_t start_cluster_num;
    uint32_t size;
}fat32Dir;


//function prototype
void regBS();
void regFSInfo();
void info();
void list(cluster_num, int);
fat32Dir *getEntry(char **, int, cluster_num);
fat32Dir *getDir(const uint8_t bin[ENTRY_SIZE]);
cluster_num getNextClusterNum(cluster_num fat_offset_num);
char *makeDepth(int);
char* getFileFullName(fat32Dir *);
char** parsePath(char *path);
void getFile(fat32Dir *);
bool validate();


//constant
const char PARENT_ENTRY_NAME[FILE_TOTAL_NAME_LENGTH] = {'.','.',' ',' ',' ',' ',' ',' ',' ',' ',' '};
const char CURRENT_ENTRY_NAME[FILE_TOTAL_NAME_LENGTH] = {'.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
const char DO_GET[] = "get";
const char DO_LIST[] = "list";
const char DO_INFO[] = "info";


//global variable
fat32BS bs;
FSInfo fsInfo;
int fd;
uint32_t sectors_per_cluster;
off_t fat_begin_lba, cluster_begin_lba;
uint16_t byte_per_sector, byte_per_cluster;
FILE* infoFile, *listFile;


int main(int argv, char *args[]){

    //wrong number of parameters
    if(argv != 3 && argv != 4){
        fprintf(stderr,"Wrong number of arguments (should be 3 or 4)\n");
        exit(EXIT_FAILURE);
    }

    //open the fat32 image file
    fd = open(args[1], O_RDONLY);
    assert(fd >= 0);

    //register the Boot sector
    regBS();
    assert(validate());
    regFSInfo();

    char *method = NULL;
    if(argv == 3){
        method = args[2];
        if(strcmp(method, DO_INFO)==0){

            infoFile = fopen(INFO_OUT_PATH,"w");
            assert(infoFile != NULL);
            info();
            fclose(infoFile);
        }else if(strcmp(method, DO_LIST) == 0){

            listFile = fopen(LIST_OUT_PATH,"w");
            assert(listFile != NULL);
            list(bs.BPB_RootClus, 0);
            fclose(listFile);
        }else{
            fprintf(stderr, "%s is unknown program, please select from \'info list get\' \n", method);
            exit(EXIT_FAILURE);
        }
    }else if(argv == 4){
        method = args[2];
        if(strcmp(method, DO_GET) == 0){
            char **pathSplit = parsePath(args[3]);
            fat32Dir *destFileEntry = getEntry(pathSplit, 0, bs.BPB_RootClus);
            if(destFileEntry == NULL){
                printf("\nThe File does not exist");
            }else{
                getFile(destFileEntry);
            }
    
        }else{
            fprintf(stderr, "%s is unknown program, please select from \'info list get\' \n", method);
            exit(EXIT_FAILURE);
        }
    }


    printf("\n");
    close(fd);
    

    return 0;
}


bool validate(){
    
}


void info(){
    fprintf(infoFile,"Driver name: %.*s", sizeof(bs.BS_VolLab) ,bs.BS_VolLab);

    uint32_t curFAT, usedFATs = 0, total_fat_bytes = bs.BPB_FATSz32*byte_per_sector;
    uint32_t fat_num = total_fat_bytes/FAT_ADDR_SIZE;


    fprintf(infoFile,"\nFree space: %d KB", (fsInfo.FSI_Free_Count*sectors_per_cluster*byte_per_sector)/KB_IN_BYTES);

    unsigned long usableBytes = (bs.BPB_TotSec32*byte_per_sector-bs.BPB_RsvdSecCnt*byte_per_sector-bs.BPB_NumFATs*4)/KB_IN_BYTES;
    fprintf(infoFile,"\nUsable storage: %lu KB", usableBytes);

    fprintf(infoFile,"\nCluster size: %d sector(s) / %d Bytes", bs.BPB_SecPerClus, bs.BPB_SecPerClus*byte_per_sector);
    fclose(infoFile);
}


//TODO: implement traversal in chained clusters for root directory
void list(cluster_num clusterNum, int depth){
    off_t cluster_start_byte = cluster_begin_lba*byte_per_sector+(clusterNum-2)*byte_per_cluster;
    fat32Dir *curDir;
    uint8_t file[ENTRY_SIZE];
    int dir_offset = 0;
    char *depthStr = makeDepth(depth);

    do{
        assert(lseek(fd, cluster_start_byte+dir_offset, SEEK_SET) > -1);
        dir_offset+=ENTRY_SIZE;
        assert(read(fd, file, ENTRY_SIZE) > -1 );
        curDir = getDir(file);

        //if current file entry is NOT unused or EOD
        if( (uint8_t)file[0] != UNUSED_FILE && file[0] != END_OF_DIR && (file[FILE_ATTR_BIT_OFFSET]&FILE_VOLLAB)==0){
            //if this file is directory
            if((file[FILE_ATTR_BIT_OFFSET]&FILE_SUBDIR) != 0){

                fprintf(listFile,"\n%s%s%.*s", depthStr,"Directory: ",FILE_TOTAL_NAME_LENGTH,curDir->name);

                //if current directory does not points to parent or current directory
                if( (memcmp(curDir->name,PARENT_ENTRY_NAME, FILE_NAME_LENGTH)!=0) && memcmp(curDir->name, CURRENT_ENTRY_NAME,FILE_NAME_LENGTH) != 0){
                    cluster_num curClusterNum = curDir->start_cluster_num;
                    //traverse all chained clusters
                    while(curClusterNum != END_OF_FILE){
                        list(curClusterNum, depth+1);
                        curClusterNum = getNextClusterNum(curClusterNum);
                    }
                }

            }else{
                char *fullName = getFileFullName(curDir);
                fprintf(listFile,"\n%s%s%s", depthStr,"File: ",fullName);

                free(fullName);

            }
        }
    }while(dir_offset<byte_per_cluster);

    free(curDir);
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

void getFile(fat32Dir *entry){

    //create file
    char *fileName = getFileFullName(entry);
    char fullOutPath[MAX_PATH_LENGTH];
    sprintf(fullOutPath, "%s%s", OUT_PATH, fileName);
    FILE *outFile = fopen(fullOutPath, "w");


    cluster_num curClusterNum = entry->start_cluster_num;
    //get data
    uint32_t byte_read = 0;

    while(curClusterNum != END_OF_FILE){
        off_t offset = cluster_begin_lba*byte_per_sector+(curClusterNum-2)*byte_per_cluster;
        int off_data = 0;

        while(byte_read < entry->size && off_data < byte_per_cluster){

            lseek(fd, offset+off_data, SEEK_SET);
            char dataChunk;
            read(fd, &dataChunk, 1);
            fwrite(&dataChunk, 1, 1,outFile);
            byte_read++;
            off_data++;
        }

        curClusterNum = getNextClusterNum(curClusterNum);
    }


    fclose(outFile);
}

fat32Dir *getDir(const uint8_t bin[ENTRY_SIZE]){
    fat32Dir* fat32Dir = malloc(sizeof(struct fat32Dir));
    assert(fat32Dir != NULL);
    //set name with first 11 bytes in bin
    memcpy(fat32Dir->name, bin, sizeof(fat32Dir->name));
    //set attr with 1 byte follow name bytes
    fat32Dir->attr = bin[sizeof(fat32Dir->name)];
    //set cluster number
    char clusterNum[DIR_CLUSTER_SIZE];
    memcpy(clusterNum+2, bin+20, DIR_CLUSTER_SIZE/2);
    memcpy(clusterNum, bin+26, DIR_CLUSTER_SIZE/2);
    memcpy(&(fat32Dir->start_cluster_num), clusterNum, 4);
//    fat32Dir->start_cluster_num = *((uint32_t *)&clusterNum);
    //set cluster size
    memcpy(&(fat32Dir->size), bin+28, sizeof(fat32Dir->size));
    return fat32Dir;
}

//return an array of pointer that points to a depth of path
char** parsePath(char *path){
    const char delimiters[2] = "/";
    char *token = strtok(path, delimiters);
    char **paths = malloc(MAX_PATH_DEPTH*4);
    int depth = 0;

    while(token!=NULL){
        paths[depth]=token;
        token = strtok(NULL,delimiters);
        depth++;
    }


    //set terminator to the path
    paths[depth+1] = NULL;

    return paths;
}

// dotdot directory's starting cluster number start at 0
fat32Dir *getEntry(char **paths, int d, cluster_num clusterNum){
    assert(paths != NULL);

    off_t cluster_start_byte = cluster_begin_lba*byte_per_sector+(clusterNum-2)*byte_per_cluster;
    fat32Dir *curDir;
    uint8_t file[ENTRY_SIZE];
    int dir_offset = 0;
    const char *curPathSeg = paths[d];
    bool failed = false;
    cluster_num nextClusterNum= getNextClusterNum(clusterNum);

    do{
        assert(lseek(fd, cluster_start_byte+dir_offset, SEEK_SET) > -1);
        dir_offset+=ENTRY_SIZE;
        assert(read(fd, file, ENTRY_SIZE) > -1 );
        curDir = getDir(file);

        //if current file entry is NOT unused or EOD
        if( (uint8_t)file[0] != UNUSED_FILE && file[0] != END_OF_DIR && (file[FILE_ATTR_BIT_OFFSET]&FILE_VOLLAB)==0){


            //if current depth is the end of path
            if(paths[d+1] == NULL){
                if(memcmp(getFileFullName(curDir), curPathSeg, strlen(curPathSeg)) == 0 ){
                    return curDir;
                }
            }else{

                //if current entry is directory
                if((file[FILE_ATTR_BIT_OFFSET]&FILE_SUBDIR) != 0){
                    //if this directory's name is same as current path segment
                    if(memcmp(curDir->name, curPathSeg, strlen(curPathSeg)) == 0 ){
                        fat32Dir *cn = getEntry(paths, d+1, curDir->start_cluster_num);
                        if(cn == NULL){
                            failed = true;
                        }else{
                            return cn;
                        }

                    }
                }

            }

        }

        //if reaching the end of cluster and still not find it
        if(dir_offset==byte_per_cluster && !failed){

            if(nextClusterNum != END_OF_FILE){
                return getEntry(paths, d, nextClusterNum);
            }
        }

    }while(dir_offset<byte_per_cluster && !failed);

    return NULL;
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

    sectors_per_cluster = bs.BPB_SecPerClus;
    cluster_begin_lba = bs.BPB_RsvdSecCnt+(bs.BPB_FATSz32*bs.BPB_NumFATs);
    fat_begin_lba = bs.BPB_RsvdSecCnt;
    byte_per_sector = bs.BPB_BytesPerSec;
    byte_per_cluster = bs.BPB_SecPerClus*byte_per_sector;
}


void regFSInfo(){
    off_t FSInfo_offset = bs.BPB_FSInfo*byte_per_sector;
    lseek(fd, FSInfo_offset, SEEK_SET);
    read(fd, &fsInfo.FSI_LeadSig, sizeof(fsInfo.FSI_LeadSig));
    assert(fsInfo.FSI_LeadSig == 0x41615252);
    read(fd, fsInfo.FSI_Reserved1, sizeof(fsInfo.FSI_Reserved1));
    read(fd, &fsInfo.FSI_StrucSig, sizeof(fsInfo.FSI_StrucSig));
    read(fd, &fsInfo.FSI_Free_Count, sizeof(fsInfo.FSI_Free_Count));
    read(fd, &fsInfo.FSI_Nxt_Free, sizeof(fsInfo.FSI_Nxt_Free));
    read(fd, fsInfo.FSI_Reserved2, sizeof(fsInfo.FSI_Reserved2));
    read(fd, &fsInfo.FSI_TrailSig, sizeof(fsInfo.FSI_TrailSig));
}
