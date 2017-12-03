//===-- MyUnroll.cpp - Loop unroller pass -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass implements a simple loop unroller.  It works best when loops have
// been canonicalized by the -indvars pass, allowing it to determine the trip
// counts of loops easily.
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "my-unroll"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <climits>

using namespace llvm;

static bool MyAllowPartial = true;
static unsigned MyThreshold = 150;
static bool MyUnrollRuntime = true;
static int MyUnrollCount = 0; // not giving static count.

static cl::opt<unsigned>
MyUnrollDepth("unroll-depth", cl::init(0), cl::Hidden, cl::desc("Decide the depth of unrolling loops"));

namespace {
    class MyUnroll : public LoopPass {
    public:
        static char ID;
        MyUnroll() : LoopPass(ID) {}
        
        bool runOnLoop(Loop *L, LPPassManager &LPM);
        
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<LoopInfo>();
            AU.addPreserved<LoopInfo>();
            AU.addRequiredID(LoopSimplifyID);
            AU.addPreservedID(LoopSimplifyID);
            AU.addRequiredID(LCSSAID);
            AU.addPreservedID(LCSSAID);
            AU.addRequired<ScalarEvolution>();
            AU.addPreserved<ScalarEvolution>();
            AU.addRequired<TargetTransformInfo>();
            AU.addPreserved<DominatorTree>();
        }
    };
}

char MyUnroll::ID = 0;
static RegisterPass<MyUnroll> X("myunroll", "Loop Unroll Pass by Jungho Bang");

#ifdef JB_LOCAL_ENV
Pass *llvm::createMyUnrollPass() { return new MyUnroll(); }
#endif

/// ApproximateLoopSize - Approximate the size of the loop.
static unsigned ApproximateLoopSize(const Loop *L, unsigned &NumCalls,
                                    bool &NotDuplicatable,
                                    const TargetTransformInfo &TTI) {
    CodeMetrics Metrics;
    for (Loop::block_iterator I = L->block_begin(), E = L->block_end(); I != E; ++I)
        Metrics.analyzeBasicBlock(*I, TTI);
    NumCalls = Metrics.NumInlineCandidates;
    NotDuplicatable = Metrics.notDuplicatable;
    
    unsigned LoopSize = Metrics.NumInsts;
    
    // Don't allow an estimate of size zero.  This would allows unrolling of loops
    // with huge iteration counts, which is a compile time problem even if it's
    // not a problem for code quality.
    if (LoopSize == 0) LoopSize = 1;
    
    return LoopSize;
}

bool MyUnroll::runOnLoop(Loop *L, LPPassManager &LPM) {
    // ********* Added for loop-depth checking
    if (!(MyUnrollDepth == 0 || L->getLoopDepth() == MyUnrollDepth))
        return false; // If the user set depth as 0, we unroll every loop. Unless, we only unroll the given depth.
    
    LoopInfo *LI = &getAnalysis<LoopInfo>();
    ScalarEvolution *SE = &getAnalysis<ScalarEvolution>();
    const TargetTransformInfo &TTI = getAnalysis<TargetTransformInfo>();
    
    BasicBlock *Header = L->getHeader();
    (void)Header;
    
    // Determine the current unrolling threshold.
    unsigned TripCount = 0;
    unsigned TripMultiple = 1;
    
    BasicBlock *LatchBlock = L->getLoopLatch();
    if (LatchBlock) {
        TripCount = SE->getSmallConstantTripCount(L, LatchBlock);
        TripMultiple = SE->getSmallConstantTripMultiple(L, LatchBlock);
    }
    
    unsigned Count = 0;
    if (MyUnrollRuntime && TripCount == 0)
        Count = MyUnrollCount;
    if (Count == 0) {
        if (TripCount == 0)
            return false;
        Count = TripCount;
    }
    
    unsigned NumInlineCandidates;
    bool notDuplicatable;
    unsigned LoopSize = ApproximateLoopSize(L, NumInlineCandidates, notDuplicatable, TTI);
    if (notDuplicatable)
        return false; // Not unrolling loop which contains non duplicatable instructions
    
    if (NumInlineCandidates != 0)
        return false; // Not unrolling loop with inlinable calls
    
    uint64_t Size = (uint64_t)LoopSize * Count;
    if (TripCount != 1 && Size > MyThreshold) { // Too large to fully unroll with count
        if (!MyAllowPartial && !(MyUnrollRuntime && TripCount == 0))
            return false; // will not try to unroll partially
        
        if (TripCount) {
            // Reduce unroll count to be modulo of TripCount for partial unrolling
            Count = MyThreshold / LoopSize;
            while (Count != 0 && TripCount%Count != 0)
                Count--;
        }
        else if (MyUnrollRuntime) {
            // Reduce unroll count to be a lower power-of-two value
            while (Count != 0 && Size > MyThreshold) {
                Count >>= 1;
                Size = LoopSize*Count;
            }
        }
        if (Count < 2)
            return false;
        
        DEBUG(dbgs() << "  partially unrolling with count: " << Count << "\n");
    }
    
    // Unroll the loop.
    if (!UnrollLoop(L, Count, TripCount, MyUnrollRuntime, TripMultiple, LI, &LPM))
        return false;
    
    return true;
}

