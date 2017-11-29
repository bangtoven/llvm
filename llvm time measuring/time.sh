clang -emit-llvm -o p.bc -c measure_time.c
clang -emit-llvm -o c.bc -c c_test.c
llvm-link p.bc c.bc -S -o=b.bc
/Users/bangtoven/Hacking/llvm/build/bin/Debug/opt -mem2reg -mytimepass b.bc -f -o b1.bc
lli b1.bc
