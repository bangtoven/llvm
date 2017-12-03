depth=1
count=2
    
clang -emit-llvm -o p.bc -c measure_time.c
clang -emit-llvm -o c.bc -c c_test.c
llvm-link p.bc c.bc -S -o=b.bc
/Users/bangtoven/Hacking/llvm/build/bin/Debug/opt -loop-simplify -loop-rotate -mem2reg -myunroll -my-depth=$depth -my-count=$count b.bc -f -o b1.bc -dot-cfg
mv cfg.main.dot cfg-$depth-$count.main.dot
lli b1.bc
# ./a.out loop_exec_time.bin 
