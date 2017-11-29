#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE 1000

unsigned long programID;

typedef struct LoopExecTime {
    double entryTime;
    double exitTime;
} LoopExecTime;
LoopExecTime data[ARRAY_SIZE];

double getCurrentTime() {
//    struct timeval tv;
//    gettimeofday(&tv, NULL);
//    return tv.tv_sec + tv.tv_usec/1000000.0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec/1000000000.0;
}

void recordEntry(unsigned long index) {
    if (data[index].entryTime > 0) return; // already recorded entry time
    data[index].entryTime = getCurrentTime();
}

void recordExit(unsigned long index) {
    if (data[index].exitTime > 0) return; // already recorded exit time
    data[index].exitTime = getCurrentTime();
}

void printFinally(unsigned long programID) {
    FILE *file=fopen("loop_exec_time.bin", "ab");
    for (int i=0; i<ARRAY_SIZE; i++) {
        if (data[i].entryTime == 0) continue; // empty item
        
        unsigned long id = programID + i; // hash + index
        fwrite(&id,sizeof(unsigned long),1,file);
        
        double duration = data[i].exitTime - data[i].entryTime;
        fwrite(&duration,sizeof(double),1,file);
    }
    fclose(file);
}
