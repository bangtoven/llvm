#include <sys/time.h>
#include <stdio.h>

#define MAX_SIZE 100
int count = 0;
struct LoopExecTime {
    unsigned long loopID;
    double entryTime;
    double exitTime;
};
struct LoopExecTime data[MAX_SIZE];

int indexOfLoop(unsigned long loop_id) {
    for (int i=0; i<count; i++) {
        if (data[i].loopID == loop_id) return i;
    }
    return -1; // couldn't find it.
}

double getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec/1000000.0;
}

void recordEntry(unsigned long loop_id) {
    int index = indexOfLoop(loop_id);
    if (index != -1) // already accessed
        return;
    
    struct LoopExecTime newLoop;
    newLoop.loopID = loop_id;
    newLoop.entryTime = getCurrentTime();
    newLoop.exitTime = 0;
    
    data[count] = newLoop;
    count++;
}

void recordExit(unsigned long loop_id) {
    int index = indexOfLoop(loop_id); // this shouldn't be -1
    if (data[index].exitTime > 0) // already recorded exit time
        return;
    
    data[index].exitTime = getCurrentTime();
}

void printFinally() {
    FILE *lTFile=fopen("loop_time_file.txt", "a");
    struct timeval tv;
    gettimeofday(&tv, NULL);
    for (int i=0; i<count; i++) {
        fprintf(lTFile, "Loop ID:%lu\t", data[i].loopID);
//        fprintf(lTFile, "%s:%lf\t", "Entry", data[i].entryTime);
//        fprintf(lTFile, "%s:%lf\n", "Exit", data[i].exitTime);
        fprintf(lTFile, "Duration:%lf\n", data[i].exitTime - data[i].entryTime);
    }
    fclose(lTFile);
}
