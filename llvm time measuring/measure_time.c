#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE 1000

unsigned long programID;

typedef struct LoopExecTime {
    clock_t entryTime;
    clock_t exitTime;
} LoopExecTime;
LoopExecTime data[ARRAY_SIZE];

void prepareMeasuring(unsigned long hash) {
    programID = hash;
}

void recordEntry(unsigned long index) {
    if (data[index].entryTime > 0) return; // already recorded entry time
    data[index].entryTime = clock();
}

void recordExit(unsigned long index) {
    if (data[index].exitTime > 0) return; // already recorded exit time
    data[index].exitTime = clock();
}

void printFinally() {
    FILE *file=fopen("loop_exec_time.bin", "ab");
    for (int i=0; i<ARRAY_SIZE; i++) {
        if (data[i].entryTime == 0) break; // empty item
        
        double id = programID + i; // hash + index
        fwrite(&id,sizeof(double),1,file);
        
        double duration = ((double)data[i].exitTime - data[i].entryTime)/CLOCKS_PER_SEC;
        fwrite(&duration,sizeof(double),1,file);
    }
    fclose(file);
}
