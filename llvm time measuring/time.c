#include <time.h>
#include <sys/time.h>
#include <stdio.h>

double getCurrentTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("%ld\t", ts.tv_nsec);
    return ts.tv_sec + ts.tv_nsec/1000000000.0;
}

int main() {
	for (int i=0; i<1000; i++) {
		double t = getCurrentTime();
		printf("%.9lf\n", t);
	}

	return 0;
}

