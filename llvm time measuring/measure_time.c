#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE 1000

typedef struct LoopExecTime {
    double entryTime;
    double exitTime;
} LoopExecTime;
LoopExecTime data[ARRAY_SIZE];

unsigned long copyCount;

double getCurrentTime() {
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

void setCopyCount(unsigned long count) {
	copyCount = count;
}

void printFinally(unsigned long programID) {
    FILE *file = fopen("loop_exec_time.txt", "a"); // for binary it should be ab.
    for (int i=0; i<ARRAY_SIZE; i++) {
        if (data[i].entryTime == 0) continue; // empty item
        
        unsigned long id = programID*100 + i; // hash + index
        double duration = data[i].exitTime - data[i].entryTime;
        // fwrite(&id,sizeof(unsigned long),1,file);
        // fwrite(&copyCount,sizeof(unsigned long),1,file);
        // fwrite(&duration,sizeof(double),1,file);
        fprintf(file, "%ld, %ld, %.9lf\n", id, copyCount, duration);
    }
    fclose(file);
}
