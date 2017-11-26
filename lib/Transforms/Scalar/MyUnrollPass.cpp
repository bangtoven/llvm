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

static bool MyUnrollRuntime = false;

namespace {
  class MyUnroll : public LoopPass {
  public:
    static char ID; // Pass ID, replacement for typeid
    MyUnroll() : LoopPass(ID) {}
      
      unsigned CurrentCount = 0;
      unsigned CurrentThreshold = 150;
      bool     CurrentAllowPartial = true;

    bool runOnLoop(Loop *L, LPPassManager &LPM);

    /// This transformation requires natural loop information & requires that
    /// loop preheaders be inserted into the CFG...
    ///
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
      // FIXME: Loop unroll requires LCSSA. And LCSSA requires dom info.
      // If loop unroll does not preserve dom info then LCSSA pass on next
      // loop will receive invalid dom info.
      // For now, recreate dom info, if loop is unrolled.
      AU.addPreserved<DominatorTree>();
    }
  };
}

char MyUnroll::ID = 0;
static RegisterPass<MyUnroll> X("myunroll", "Loop Unroll Pass by Jungho Bang");

Pass *llvm::createMyUnrollPass() {
  return new MyUnroll();
}

/// ApproximateLoopSize - Approximate the size of the loop.
static unsigned ApproximateLoopSize(const Loop *L, unsigned &NumCalls,
                                    bool &NotDuplicatable,
                                    const TargetTransformInfo &TTI) {
    CodeMetrics Metrics;
    for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
         I != E; ++I)
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
    errs() << "This is Jungho's unrolling test.\n";
    
    LoopInfo *LI = &getAnalysis<LoopInfo>();
    ScalarEvolution *SE = &getAnalysis<ScalarEvolution>();
    const TargetTransformInfo &TTI = getAnalysis<TargetTransformInfo>();
    
    BasicBlock *Header = L->getHeader();
    DEBUG(dbgs() << "Loop Unroll: F[" << Header->getParent()->getName()
          << "] Loop %" << Header->getName() << "\n");
    (void)Header;
    
    // Determine the current unrolling threshold.  While this is normally set
    // from UnrollThreshold, it is overridden to a smaller value if the current
    // function is marked as optimize-for-size, and the unroll threshold was
    // not user specified.
    unsigned Threshold = CurrentThreshold;
    
    // Find trip count and trip multiple if count is not available
    unsigned TripCount = 0;
    unsigned TripMultiple = 1;
    // Find "latch trip count". UnrollLoop assumes that control cannot exit
    // via the loop latch on any iteration prior to TripCount. The loop may exit
    // early via an earlier branch.
    BasicBlock *LatchBlock = L->getLoopLatch();
    if (LatchBlock) {
        TripCount = SE->getSmallConstantTripCount(L, LatchBlock);
        TripMultiple = SE->getSmallConstantTripMultiple(L, LatchBlock);
    }
    // Use a default unroll-count if the user doesn't specify a value
    // and the trip count is a run-time value.  The default is different
    // for run-time or compile-time trip count loops.
    unsigned Count = CurrentCount;
//    if (UnrollRuntime && CurrentCount == 0 && TripCount == 0)
//        Count = UnrollRuntimeCount;
    
    if (Count == 0) {
        // Conservative heuristic: if we know the trip count, see if we can
        // completely unroll (subject to the threshold, checked below); otherwise
        // try to find greatest modulo of the trip count which is still under
        // threshold value.
        if (TripCount == 0)
            return false;
        Count = TripCount;
    }
    
    // Enforce the threshold.
    if (Threshold != UINT_MAX) {
        unsigned NumInlineCandidates;
        bool notDuplicatable;
        unsigned LoopSize = ApproximateLoopSize(L, NumInlineCandidates,
                                                notDuplicatable, TTI);
        DEBUG(dbgs() << "  Loop Size = " << LoopSize << "\n");
        if (notDuplicatable) {
            DEBUG(dbgs() << "  Not unrolling loop which contains non duplicatable"
                  << " instructions.\n");
            return false;
        }
        if (NumInlineCandidates != 0) {
            DEBUG(dbgs() << "  Not unrolling loop with inlinable calls.\n");
            return false;
        }
        uint64_t Size = (uint64_t)LoopSize*Count;
        if (TripCount != 1 && Size > Threshold) {
            DEBUG(dbgs() << "  Too large to fully unroll with count: " << Count
                  << " because size: " << Size << ">" << Threshold << "\n");
            if (!CurrentAllowPartial && !(MyUnrollRuntime && TripCount == 0)) {
                DEBUG(dbgs() << "  will not try to unroll partially because "
                      << "-unroll-allow-partial not given\n");
                return false;
            }
            if (TripCount) {
                // Reduce unroll count to be modulo of TripCount for partial unrolling
                Count = Threshold / LoopSize;
                while (Count != 0 && TripCount%Count != 0)
                    Count--;
            }
            else if (MyUnrollRuntime) {
                // Reduce unroll count to be a lower power-of-two value
                while (Count != 0 && Size > Threshold) {
                    Count >>= 1;
                    Size = LoopSize*Count;
                }
            }
            if (Count < 2) {
                DEBUG(dbgs() << "  could not unroll partially\n");
                return false;
            }
            DEBUG(dbgs() << "  partially unrolling with count: " << Count << "\n");
        }
    }
    
    // Unroll the loop.
    if (!UnrollLoop(L, Count, TripCount, MyUnrollRuntime, TripMultiple, LI, &LPM))
        return false;
    
    return true;
}

