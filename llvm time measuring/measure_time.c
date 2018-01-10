#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#define ARRAY_SIZE 1000

typedef struct LoopExecTime {
    int64_t entryTime;
    int64_t exitTime;
} LoopExecTime;
LoopExecTime data[ARRAY_SIZE];

int64_t copyCount;

int64_t getCurrentTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t nsec = ts.tv_nsec;
    int64_t sec = ts.tv_sec;
    int64_t giga = 1000000000;
    return sec*giga + nsec;
}

void recordEntry(int64_t index) {
    if (data[index].entryTime > 0) return; // already recorded entry time
    data[index].entryTime = getCurrentTime();
}

void recordExit(int64_t index) {
    if (data[index].exitTime > 0) return; // already recorded exit time
    data[index].exitTime = getCurrentTime();
}

void setCopyCount(int64_t count) {
	copyCount = count;
    memset(data, 0, sizeof(LoopExecTime)*ARRAY_SIZE);
}

void printFinally(int64_t programID) {
    FILE *file = fopen("loop_exec_time.txt", "a"); // for binary it should be ab.
    for (int i=0; i<ARRAY_SIZE; i++) {
        if (data[i].entryTime == 0) continue; // empty item
        
        int64_t id = programID*100 + i; // hash + index
        int64_t duration = data[i].exitTime - data[i].entryTime;
        // fwrite(&id,sizeof(int64_t),1,file);
        // fwrite(&copyCount,sizeof(int64_t),1,file);
        // fwrite(&duration,sizeof(double),1,file);
        fprintf(file, "%llu, %llu, %llu\n", id, copyCount, duration);
    }
    fclose(file);
}
