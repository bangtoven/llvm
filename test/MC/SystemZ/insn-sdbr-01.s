# RUN: llvm-mc -triple s390x-linux-gnu -show-encoding %s | FileCheck %s

#CHECK: sdbr	%f0, %f0                # encoding: [0xb3,0x1b,0x00,0x00]
#CHECK: sdbr	%f0, %f15               # encoding: [0xb3,0x1b,0x00,0x0f]
#CHECK: sdbr	%f7, %f8                # encoding: [0xb3,0x1b,0x00,0x78]
#CHECK: sdbr	%f15, %f0               # encoding: [0xb3,0x1b,0x00,0xf0]

	sdbr	%f0, %f0
	sdbr	%f0, %f15
	sdbr	%f7, %f8
	sdbr	%f15, %f0
