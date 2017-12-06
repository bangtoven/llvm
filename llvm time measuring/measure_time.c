#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ARRAY_SIZE 1000

typedef struct LoopExecTime {
    unsigned long entryTime;
    unsigned long exitTime;
} LoopExecTime;
LoopExecTime data[ARRAY_SIZE];

unsigned long copyCount;

unsigned long getCurrentTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*1000000000.0 + ts.tv_nsec;
}

void recordEntry(unsigned long index) {
    if (data[index].entryTime > 0) return; // already recorded entry time
    data[index].entryTime = getCurrentTime();
}

void recordExit(unsigned long index) {
    if (data[index].exitTime > 0) return; // already recorded exit time
    data[index].exitTime = getCurrentTime();
}

void setCopyCount(unsigned long count) {
	copyCount = count;
    memset(data, 0, sizeof(LoopExecTime)*ARRAY_SIZE);
}

void printFinally(unsigned long programID) {
    FILE *file = fopen("loop_exec_time.txt", "a"); // for binary it should be ab.
    for (int i=0; i<ARRAY_SIZE; i++) {
        if (data[i].entryTime == 0) continue; // empty item
        
        unsigned long id = programID*100 + i; // hash + index
        unsigned long duration = data[i].exitTime - data[i].entryTime;
        // fwrite(&id,sizeof(unsigned long),1,file);
        // fwrite(&copyCount,sizeof(unsigned long),1,file);
        // fwrite(&duration,sizeof(double),1,file);
        fprintf(file, "%ld, %ld, %ld\n", id, copyCount, duration);
    }
    fclose(file);
}
