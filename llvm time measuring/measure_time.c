#include <time.h>
#include <stdio.h>

#define MAX_SIZE 100

struct LoopExecTime {
    unsigned long loopID;
    clock_t entryTime;
    clock_t exitTime;
};
struct LoopExecTime data[MAX_SIZE];
int count = 0;

int indexOfLoop(unsigned long loop_id) {
    for (int i=0; i<count; i++) {
        if (data[i].loopID == loop_id) return i;
    }
    return -1; // couldn't find it.
}

// double getCurrentTime() {
//     struct timeval tv;
//     gettimeofday(&tv, NULL);
//     return tv.tv_sec + tv.tv_usec/1000000.0;
// }

void recordEntry(unsigned long loop_id) {
    int index = indexOfLoop(loop_id);
    if (index != -1) // already accessed
        return;
    
    struct LoopExecTime newLoop;
    newLoop.loopID = loop_id;
    newLoop.entryTime = clock();
    newLoop.exitTime = 0;
    
    data[count] = newLoop;
    count++;
}

void recordExit(unsigned long loop_id) {
    int index = indexOfLoop(loop_id); // this shouldn't be -1
    if (data[index].exitTime > 0) // already recorded exit time
        return;
    
    data[index].exitTime = clock();
}

struct LoopExecData {
    unsigned long loopID;
    double duration;
};

void printFinally() {
    FILE *file=fopen("loop_exec_time.bin", "ab");
    for (int i=0; i<count; i++) {
        struct LoopExecData led;
        led.loopID = data[i].loopID;
        led.duration = ((double)data[i].exitTime - data[i].entryTime)/CLOCKS_PER_SEC;
        fwrite(&led,sizeof(struct LoopExecData),1,file);
    }
    fclose(file);
}
