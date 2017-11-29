//
//  main.c
//  HashLikeDataStructure
//
//  Created by 방정호 on 11/27/17.
//  Copyright © 2017 Jungho Bang. All rights reserved.
//

#include <stdio.h>

struct LoopExecData {
    unsigned long loopID;
    double duration;
};

int main(int argc, const char * argv[]) {
    FILE *file=fopen("loop_exec_time.bin", "rb");
    struct LoopExecData buffer;
    while (fread(&buffer,sizeof(struct LoopExecData),1,file)) {
        printf("Loop ID:%lu\t", buffer.loopID);
        printf("Duration:%lf\n", buffer.duration);
    }
    fclose(file);
    return 0;
}
