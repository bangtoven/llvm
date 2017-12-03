#define DEBUG_TYPE "my-unroll"
#include "llvm/Pass.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <climits>

using namespace llvm;
using namespace std;

static cl::opt<unsigned>
MyUnrollCount("my-count", cl::init(0), cl::Hidden, cl::desc("Unroll factor"));

static cl::opt<unsigned>
MyUnrollDepth("my-depth", cl::init(0), cl::Hidden, cl::desc("Decide the depth of unrolling loops"));

static unsigned ApproximateLoopSize(const Loop*, unsigned&, bool& , const TargetTransformInfo&);
static void writeFeatures (Loop* L, unsigned long loopID, unsigned copyCount);

namespace {
    class MyUnroll : public LoopPass {
        unsigned long moduleHash;
        unsigned long loopIndex = -1;
        
        void setHookFunctions(Module *module);
        bool hookFunctionsSetDone = false;
        Constant *hookFuncRecordStart;
        Constant *hookFuncRecordFinish;
        Constant *hookFuncFinal;
        
        bool unrolling(Loop *L, LPPassManager &LPM);
        
        void runOnEntryBlock(BasicBlock* preheader, unsigned long loop_idx);
        void runOnExitBlock(BasicBlock* exitingBlock, unsigned long loop_idx);
        
    public:
        static char ID;
        MyUnroll() : LoopPass(ID) {}
        bool runOnLoop(Loop *L, LPPassManager &LPM);
        
        virtual void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<ProfileInfo>();
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
            AU.setPreservesAll();
        }
    };
}

char MyUnroll::ID = 0;
static RegisterPass<MyUnroll> X("myunroll", "Loop Unroll Pass by Jungho Bang");

#ifdef JB_LOCAL_ENV
Pass *llvm::createMyUnrollPass() { return new MyUnroll(); }
#endif

bool MyUnroll::runOnLoop(Loop *L, LPPassManager &LPM) {
    loopIndex++; // increment loopIndex always
    
    Function *F = L->getHeader()->getParent();
    if (hookFunctionsSetDone == false) {
        setHookFunctions(F->getParent()); // set hook function references, hook at the bottom of main function
        hookFunctionsSetDone = true; // only for once
    }
    
    if (F->getName() == "printFinally") // skip our function
        return false;
    
    if (L->getLoopDepth() != MyUnrollDepth) {// not the depth we want
        errs() << "Depth: " << L->getLoopDepth() << " is not the depth provided.\n";
        return false; // don't do anything, just increment the index
    }
    
    BasicBlock* preheader = L->getLoopPreheader();
    BasicBlock* exitingBlock = L->getExitBlock();
    if (exitingBlock == NULL)
        return false; // don't do anything.
    

    // -------------- Do something!!
    writeFeatures(L, moduleHash + loopIndex, MyUnrollCount);
    
    runOnEntryBlock(preheader, loopIndex);
    runOnExitBlock(exitingBlock, loopIndex);
    
    if (MyUnrollCount > 1) // if unroll factor is 2 or more
        return unrolling(L, LPM); // then do the unrolling
    else
        return true;
}

bool MyUnroll::unrolling(Loop *L, LPPassManager &LPM) {
    static bool AllowPartial = true;
    static unsigned Threshold = 150;
    static bool UnrollRuntime = true;
    
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
    
    unsigned Count = MyUnrollCount;
    if (Count == 0)
        return false;
    
    unsigned NumInlineCandidates;
    bool notDuplicatable;
    unsigned LoopSize = ApproximateLoopSize(L, NumInlineCandidates, notDuplicatable, TTI);
    if (notDuplicatable)
        return false; // Not unrolling loop which contains non duplicatable instructions
    
    if (NumInlineCandidates != 0)
        return false; // Not unrolling loop with inlinable calls
    
    uint64_t Size = (uint64_t)LoopSize * Count;
    if (TripCount != 1 && Size > Threshold) { // Too large to fully unroll with count
        if (!AllowPartial && !(UnrollRuntime && TripCount == 0))
            return false; // will not try to unroll partially
        
        if (TripCount) {
            // Reduce unroll count to be modulo of TripCount for partial unrolling
            Count = Threshold / LoopSize;
            while (Count != 0 && TripCount%Count != 0)
                Count--;
        }
        else if (UnrollRuntime) {
            // Reduce unroll count to be a lower power-of-two value
            while (Count != 0 && Size > Threshold) {
                Count >>= 1;
                Size = LoopSize*Count;
            }
        }
        if (Count < 2)
            return false;
        
        DEBUG(dbgs() << "  partially unrolling with count: " << Count << "\n");
    }
    
    // Unroll the loop.
    if (!UnrollLoop(L, Count, TripCount, UnrollRuntime, TripMultiple, LI, &LPM))
        return false;
    
    return true;
}

void MyUnroll::setHookFunctions(Module *m) {
    LLVMContext &Ctx = m->getContext();
    FunctionType *funcType = FunctionType::get(Type::getVoidTy(Ctx), Type::getInt64Ty(Ctx), false);
    
    hookFuncRecordStart = m->getOrInsertFunction("recordEntry", funcType);
    hookFuncRecordFinish = m->getOrInsertFunction("recordExit", funcType);
    hookFuncFinal = m->getOrInsertFunction("printFinally", funcType);
    
    hash<string> hashFunc;
    moduleHash = hashFunc(m->getModuleIdentifier());
    llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64, moduleHash))};
    Instruction *newInst = CallInst::Create(hookFuncFinal, arg, "");
    
    Function *main = m->getFunction("main");
    Function::iterator bit = main->end();
    BasicBlock::iterator iit = (--bit)->end();
    newInst->insertBefore(--iit);
}

void MyUnroll::runOnEntryBlock(BasicBlock* preheader, unsigned long loop_idx) {
    LLVMContext &Ctx = preheader->getContext();
    llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64,loop_idx ))};
    Instruction *newInst = CallInst::Create(hookFuncRecordStart, arg, "");
    BasicBlock::iterator pit = preheader->end();
    newInst->insertBefore(--pit);
}

void MyUnroll::runOnExitBlock(BasicBlock* exitingBlock, unsigned long loop_idx) {
    LLVMContext &Ctx = exitingBlock->getContext();
    llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64,loop_idx ))};
    Instruction *newInst = CallInst::Create(hookFuncRecordFinish, arg, "");
    newInst->insertBefore(exitingBlock->begin());
}

/*
 Function writes all features of loops to a file, this file will be used for
 Neural network analysis later.
 */
void writeFeatures (Loop *L, unsigned long loopID, unsigned copyCount){
    //setup parameters to get features
    //if loop nests other loop, it adds the features in the nested loops to itself as well
    unsigned num_instructions = 0; //get number of instructions, substitutes for num_statements
    unsigned num_arithmetic_ops = 0; //number of arithmetic ops in loop
    unsigned num_array_accesses = 0; //number of array accesses in loop
    unsigned num_conditions = 0; // number of conditional instructions
    for (Loop::block_iterator ii = L->block_begin(), ie = L->block_end(); ii != ie; ++ii) {
        num_instructions += (*ii)->size();
        for (BasicBlock::iterator i = (*ii)->begin(), e = (*ii)->end(); i != e; i++){
            unsigned opcode = i->getOpcode();
            if (opcode == Instruction::Add || opcode == Instruction::Sub || opcode == Instruction::Mul
                || opcode == Instruction::UDiv || opcode == Instruction::SDiv || opcode == Instruction::URem
                || opcode == Instruction::Shl || opcode == Instruction::LShr || opcode == Instruction::AShr
                || opcode == Instruction::And || opcode == Instruction::Or || opcode == Instruction::Xor
                || opcode == Instruction::ICmp || opcode == Instruction::SRem || opcode == Instruction::FAdd
                || opcode == Instruction::FSub || opcode == Instruction::FMul|| opcode == Instruction::FDiv
                || opcode == Instruction::FRem || opcode == Instruction::FCmp){
                num_arithmetic_ops += 1;
            }
            
            BranchInst* bi = dyn_cast<llvm::BranchInst>(i);
            if (bi && bi->isConditional()) {
                num_conditions += 1;
            }
            
            if (opcode == Instruction::Load){
                std::string str;
                llvm::raw_string_ostream rso(str);
                (i)->print(rso);
                std::size_t found = str.find("arrayidx");
                if (found!=std::string::npos){
                    (i)->dump();
                    num_array_accesses+=1;
                }
            }
        }
    }
    //output features to a features file
    string file_name = "loop_features.txt";
    ofstream fout(file_name, std::ofstream::app | std::ofstream::binary);
    fout.write((char*) &loopID, sizeof(unsigned long));
    fout.write((char*) &num_instructions, sizeof(unsigned));
    fout.write((char*) &num_arithmetic_ops, sizeof(unsigned));
    fout.write((char*) &num_array_accesses, sizeof(unsigned));
    fout.write((char*) &num_conditions, sizeof(unsigned));
    fout.write((char*) &copyCount, sizeof(unsigned));
//    fout << "Loop_ID: " << loopID;
//    fout << "\t Instruction count: " << num_instructions;
//    fout << "\t Arithmetic ops count: " << num_arithmetic_ops;
//    fout << "\t Array access count: " << num_array_accesses;
//    fout << "\t Conditional Instruction count: " << num_conditions;
////    fout << ", Loop Iteration count: " << trip_count;
//    fout << "\t Unroll factor: " << copyCount;
//    fout << endl;
    fout.close();
}

/// ApproximateLoopSize - Approximate the size of the loop.
static unsigned ApproximateLoopSize(const Loop *L, unsigned &NumCalls, bool &NotDuplicatable, const TargetTransformInfo &TTI) {
    CodeMetrics Metrics;
    for (Loop::block_iterator I = L->block_begin(), E = L->block_end(); I != E; ++I)
        Metrics.analyzeBasicBlock(*I, TTI);
    NumCalls = Metrics.NumInlineCandidates;
    NotDuplicatable = Metrics.notDuplicatable;
    
    unsigned LoopSize = Metrics.NumInsts;
    if (LoopSize == 0) LoopSize = 1;
    return LoopSize;
}
