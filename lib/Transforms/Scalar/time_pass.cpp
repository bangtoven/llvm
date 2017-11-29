#include "llvm/Pass.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include <sstream>
#include <string>
#include <map>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <fstream>

using namespace llvm;
using namespace std;

namespace {
    static unsigned long loop_count = 0;
    
    struct TimeMeasurePass : public FunctionPass {
        static char ID;
        Constant *hookFuncRecordStart;
        Constant *hookFuncRecordFinish;
        Constant *hookFuncFinal;
        TimeMeasurePass() : FunctionPass(ID) {}
        /*below variables are cutoff values for loops to consider
         each position in variable name means
         1 - main or other function (m/o)
         2 - L - Level
         3 - level_value (1,2,...)
         4-7 - Trip count cut off
         */
        unsigned mL2TCCO = 31;
        unsigned oL1TCCO = 101;
        unsigned oL2TCCO = 11;
        /*
         Function writes all features of loops to a file, this file will be used for
         Neural network analysis later.
         */
        void writeFeatures (unsigned long loop_id_hash, Loop *L, unsigned trip_count){
            //setup parameters to get features
            //if loop nests other loop, it adds the features in the nested loops to itself as well
            unsigned num_instructions = 0; //get number of instructions, substitutes for num_statements
            unsigned num_arithmetic_ops = 0; //number of arithmetic ops in loop
            unsigned num_array_accesses = 0; //number of array accesses in loop
            unsigned num_conditions = 0; // number of conditional instructions
            for (Loop::block_iterator ii = L->block_begin(), ie = L->block_end(); ii != ie; ++ii) {
                num_instructions += (*ii)->size();
                for (BasicBlock::iterator i = (*ii)->begin(), e = (*ii)->end(); i != e; i++){
                    if (i->getOpcode() == Instruction::Add || i->getOpcode() == Instruction::Sub || i->getOpcode() == Instruction::Mul
                        || i->getOpcode() == Instruction::UDiv || i->getOpcode() == Instruction::SDiv || i->getOpcode() == Instruction::URem
                        || i->getOpcode() == Instruction::Shl || i->getOpcode() == Instruction::LShr || i->getOpcode() == Instruction::AShr
                        || i->getOpcode() == Instruction::And || i->getOpcode() == Instruction::Or || i->getOpcode() == Instruction::Xor
                        || i->getOpcode() == Instruction::ICmp || i->getOpcode() == Instruction::SRem || i->getOpcode() == Instruction::FAdd
                        || i->getOpcode() == Instruction::FSub || i->getOpcode() == Instruction::FMul|| i->getOpcode() == Instruction::FDiv
                        || i->getOpcode() == Instruction::FRem || i->getOpcode() == Instruction::FCmp){
                        num_arithmetic_ops += 1;
                    }
                    BranchInst* bi = dyn_cast<llvm::BranchInst>(i);
                    if (bi){
                        if (bi->isConditional())
                        {
                            num_conditions+=1;
                        }
                    }
                    if (i->getOpcode() == Instruction::Load){
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
            const char* f_name = file_name.c_str();
            ofstream fout;
            fout.open(f_name, std::ofstream::app);
            fout << "Loop_ID: " << loop_id_hash << ", Instruction count: " << num_instructions<< ", Arithmetic ops count: "
            << num_arithmetic_ops << ", Array access count: " << num_array_accesses << ", Conditional Instruction count: "
            << num_conditions  << ", Loop Iteration count: " << trip_count << "\n";
            fout.close();
        }
        
        void handleLoop(ScalarEvolution &SE, Loop *L, Function &F){
            if (F.getName() == "printFinally")
                return;
            
            unsigned trip_count = SE.getSmallConstantTripCount(L, L->getExitBlock());
            
            //denotes outermost loop and increases with nesting
            unsigned outer_depth = 1;
            /*convert loop_id which is a string to unsigned long. In LLVM, it is easier
             to pass long type to an external function compared to string get loop_id*/
            //string f_name = string(F.getName());
            string m_name = ToString(F.getParent()->getModuleIdentifier());
            //string loop_id = m_name+f_name;
            //+ToString(loopcounter);
            char *loop_id_cstr = new char[m_name.length() + 1];
            strcpy(loop_id_cstr, m_name.c_str());
            
            // compute hash for string
            unsigned long mod_hash = hash(loop_id_cstr);
            
            //free space
            delete [] loop_id_cstr;
            
            /*
             The outer_loop and trip_count conditions below are a trade-off between getting enough loops
             and preventing infinite loops when recording times for nested loops.
             The loop nesting conditions (first and second) are arbitrary, but they should be enough for what
             we are trying to do. The trip_count values(100, 30, 10) are also arbitrary but seem to work for
             our need.
             */
            
            //get preheader and loop exit to insert time instruction
            BasicBlock* Preheader = L->getLoopPreheader();
            BasicBlock* exitingBlock = L->getExitBlock();
            if (exitingBlock == NULL)
                return;
            
            unsigned long loop_index = loop_count;
            ++loop_count;
            unsigned long loop_id_hash = mod_hash + loop_index;
            
            writeFeatures(loop_id_hash, L, trip_count);
            runOnEntryBlock(Preheader, loop_index, F);
            for (LoopInfo::iterator LIT = L->getSubLoops().begin(), LEND = L->getSubLoops().end(); LIT != LEND; ++LIT) {
                handleLoop(SE, *LIT, F);
            }
            runOnExitBlock(exitingBlock,loop_index, F);
        }
        virtual bool runOnFunction (Function &F) {
            //int loopcounter = 0;
            LLVMContext &Ctx = F.getParent()->getContext();
            //setup parameters to function that measures loop time
            //only parameter it takes is loop_id which is an integer hash of a string gotten
            //from module_id+function_name+loop_counter
            Type *retType = Type::getVoidTy(Ctx);
            ArrayRef<Type*> Params = Type::getInt64Ty(Ctx);
            FunctionType *recordFuncType = FunctionType::get(retType, Params, false);
            FunctionType *finalFuncType = FunctionType::get(retType, Params, false);
            hookFuncRecordStart = F.getParent()->getOrInsertFunction("recordEntry", recordFuncType);
            hookFuncRecordFinish = F.getParent()->getOrInsertFunction("recordExit", recordFuncType);
            hookFuncFinal = F.getParent()->getOrInsertFunction("printFinally", finalFuncType);
            ScalarEvolution &SE = getAnalysis<ScalarEvolution> ();
            if (!F.isDeclaration()) {
                LoopInfo& LI= getAnalysis<LoopInfo>();
                for (LoopInfo::iterator LIT = LI.begin(), LEND = LI.end(); LIT != LEND; ++LIT) {
                    handleLoop(SE, *LIT, F);
                }
                //if (loopcounter > 0){
                //  errs() << F.getName() <<" - Number of loops " << loopcounter << "\n";
                //}
                if (F.getName() == "main"){
                    Function::iterator bit = F.end();
                    bit--;
                    LLVMContext &Ctx = F.getContext();
                    string m_name = ToString(F.getParent()->getModuleIdentifier());
                    char *loop_id_cstr = new char[m_name.length() + 1];
                    strcpy(loop_id_cstr, m_name.c_str());
                    // compute hash for string
                    unsigned long mod_hash = hash(loop_id_cstr);
                    llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64, mod_hash))};
                    Instruction *newInst = CallInst::Create(hookFuncFinal, arg, "");
                    BasicBlock::iterator iit = bit->end();
                    iit--;
                    newInst->insertBefore(iit);
                }
            }
            return false;
        }
        void runOnEntryBlock(BasicBlock* Preheader, unsigned long loop_idx, Function &F)
        {
            LLVMContext &Ctx = F.getContext();
            llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64,loop_idx ))};
            Instruction *newInst = CallInst::Create(hookFuncRecordStart, arg, "");
            BasicBlock::iterator pit = Preheader->end();
            --pit;
            newInst->insertBefore(pit);
        }
        
        void runOnExitBlock(BasicBlock* exitingBlock, unsigned long loop_idx, Function &F)
        {
            LLVMContext &Ctx = F.getContext();
            llvm::Value* arg []= {llvm::ConstantInt::get(Ctx , llvm::APInt( 64,loop_idx ))};
            Instruction *newInst = CallInst::Create(hookFuncRecordFinish, arg, "");
            newInst->insertBefore(exitingBlock->begin());
        }
        
        void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<ProfileInfo>();
            AU.addRequired<LoopInfo>();
            AU.addRequired<ScalarEvolution>();
            AU.setPreservesAll
            ();
        }
        //converts StringRef to string
        template <typename T>
        string ToString(T val)
        {
            stringstream stream;
            stream << val;
            return stream.str();
        }
        //hashes c_string to int, makes it easy to pass loop_id to external function
        unsigned long hash(char *str)
        {
            unsigned long hash = 5381;
            int c;
            c = *str;
            while (c != '\0'){
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
                c = *(++str);
            }
            return hash;
        }
        
        
    };
}

char TimeMeasurePass::ID = 0;
static RegisterPass<TimeMeasurePass> X("mytimepass", "my Pass", false, false);

#ifdef JB_LOCAL_ENV
Pass *llvm::createTimeMeasurePass() {
    return new TimeMeasurePass();
}
#endif
