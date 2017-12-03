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

static bool MyAllowPartial = true;
static unsigned MyThreshold = 150;
static bool MyUnrollRuntime = true;

static cl::opt<unsigned>
UnrollDepth("unroll-depth", cl::init(0), cl::Hidden, cl::desc("Decide the depth of unrolling loops"));

static cl::opt<unsigned>
MyUnrollCount("unroll-count", cl::init(0), cl::Hidden,
            cl::desc("Use this unroll count for all loops, for testing purposes"));

namespace {
    static unsigned long loop_count = 0;
    
    class TimeMeasurePass : public FunctionPass {
        Constant *hookFuncRecordStart;
        Constant *hookFuncRecordFinish;
        Constant *hookFuncFinal;
        
        void setHookFunctions(Function &F);
        
        void handleLoop(ScalarEvolution &SE, Loop *L, Function &F);
        void runOnEntryBlock(BasicBlock* Preheader, unsigned long loop_idx, Function &F);
        void runOnExitBlock(BasicBlock* exitingBlock, unsigned long loop_idx, Function &F);
        
        void writeFeatures (unsigned long loop_id_hash, Loop *L, unsigned trip_count);
        unsigned long hashString(string str);
        
    public:
        static char ID;
        TimeMeasurePass() : FunctionPass(ID) {}
        bool runOnFunction (Function &F);
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

char TimeMeasurePass::ID = 0;
static RegisterPass<TimeMeasurePass> X("mytimepass", "my Pass", false, false);

#ifdef JB_LOCAL_ENV
Pass *llvm::createTimeMeasurePass() { return new TimeMeasurePass(); }
#endif

bool TimeMeasurePass::runOnFunction (Function &F) {
    setHookFunctions(F);
    
    if (!F.isDeclaration()) {
        LoopInfo& LI= getAnalysis<LoopInfo>();
        ScalarEvolution &SE = getAnalysis<ScalarEvolution> ();
        for (LoopInfo::iterator LIT = LI.begin(), LEND = LI.end(); LIT != LEND; ++LIT) {
            handleLoop(SE, *LIT, F);
        }
        
        if (F.getName() == "main"){
            LLVMContext &Ctx = F.getContext();
            unsigned long mod_hash = hashString(F.getParent()->getModuleIdentifier());
            llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64, mod_hash))};
            Instruction *newInst = CallInst::Create(hookFuncFinal, arg, "");
            Function::iterator bit = F.end();
            BasicBlock::iterator iit = (--bit)->end();
            newInst->insertBefore(--iit);
        }
    }
    return false;
}

void TimeMeasurePass::setHookFunctions(Function &F) {
    LLVMContext &Ctx = F.getParent()->getContext();
    Type *retType = Type::getVoidTy(Ctx);
    ArrayRef<Type*> Params = Type::getInt64Ty(Ctx);
    FunctionType *recordFuncType = FunctionType::get(retType, Params, false);
    FunctionType *finalFuncType = FunctionType::get(retType, Params, false);
    hookFuncRecordStart = F.getParent()->getOrInsertFunction("recordEntry", recordFuncType);
    hookFuncRecordFinish = F.getParent()->getOrInsertFunction("recordExit", recordFuncType);
    hookFuncFinal = F.getParent()->getOrInsertFunction("printFinally", finalFuncType);
}

void TimeMeasurePass::handleLoop(ScalarEvolution &SE, Loop *L, Function &F){
    if (F.getName() == "printFinally")
        return;
    
    unsigned long mod_hash = hashString(F.getParent()->getModuleIdentifier());
    
    //get preheader and loop exit to insert time instruction
    BasicBlock* Preheader = L->getLoopPreheader();
    BasicBlock* exitingBlock = L->getExitBlock();
    if (exitingBlock == NULL)
        return;
    
    unsigned long loop_index = loop_count;
    ++loop_count;
    
    unsigned long loop_id_hash = mod_hash + loop_index;
    unsigned trip_count = SE.getSmallConstantTripCount(L, L->getExitBlock());
    writeFeatures(loop_id_hash, L, trip_count);
    
    runOnEntryBlock(Preheader, loop_index, F);
    for (LoopInfo::iterator LIT = L->getSubLoops().begin(), LEND = L->getSubLoops().end(); LIT != LEND; ++LIT) {
        handleLoop(SE, *LIT, F);
    }
    runOnExitBlock(exitingBlock,loop_index, F);
}

void TimeMeasurePass::runOnEntryBlock(BasicBlock* Preheader, unsigned long loop_idx, Function &F) {
    LLVMContext &Ctx = F.getContext();
    llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64,loop_idx ))};
    Instruction *newInst = CallInst::Create(hookFuncRecordStart, arg, "");
    BasicBlock::iterator pit = Preheader->end();
    --pit;
    newInst->insertBefore(pit);
}

void TimeMeasurePass::runOnExitBlock(BasicBlock* exitingBlock, unsigned long loop_idx, Function &F) {
    LLVMContext &Ctx = F.getContext();
    llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64,loop_idx ))};
    Instruction *newInst = CallInst::Create(hookFuncRecordFinish, arg, "");
    newInst->insertBefore(exitingBlock->begin());
}

/*
 Function writes all features of loops to a file, this file will be used for
 Neural network analysis later.
 */
void TimeMeasurePass::writeFeatures (unsigned long loop_id_hash, Loop *L, unsigned trip_count){
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
    ofstream fout;
    fout.open(file_name, std::ofstream::app);
    fout << "Loop_ID: " << loop_id_hash;
    fout << ", Instruction count: " << num_instructions;
    fout << ", Arithmetic ops count: " << num_arithmetic_ops;
    fout << ", Array access count: " << num_array_accesses;
    fout << ", Conditional Instruction count: " << num_conditions;
    fout << ", Loop Iteration count: " << trip_count;
    fout << endl;
    fout.close();
}

//hashes c_string to int, makes it easy to pass loop_id to external function
unsigned long TimeMeasurePass::hashString(string str) {
    char *c_str = new char[str.length() + 1];
    strcpy(c_str, str.c_str());
    
    unsigned long hash = 5381;
    int c = *c_str;
    while (c != '\0'){
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
        c = *(++c_str);
    }
    
    delete [] c_str;
    return hash;
}

