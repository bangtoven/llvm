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

namespace {
  class MyUnroll : public LoopPass {
  public:
    static char ID; // Pass ID, replacement for typeid
    MyUnroll() : LoopPass(ID) {}

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

bool MyUnroll::runOnLoop(Loop *L, LPPassManager &LPM) {
    errs() << "This is Jungho's unrolling test.\n";
    
    LoopInfo *LI = &getAnalysis<LoopInfo>();
    
    if (!UnrollLoop(L, 5, 7, true, true, LI, &LPM)) {
        errs() << "Unrolling failed.\n";
        return false;
    }

  return true;
}
