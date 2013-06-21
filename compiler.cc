#include "llvm/LinkAllPasses.h"
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Linker.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/GlobalVariable.h>
#include <llvm/Module.h>
#include <llvm/LLVMContext.h>
#include <llvm/PassManager.h>
#include "llvm/Analysis/Verifier.h"
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/MemoryBuffer.h>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <llvm/Target/TargetData.h>
#include <llvm/Support/system_error.h>
#include <llvm/Support/TargetSelect.h>


#include "AST.h"

using namespace llvm;

namespace {
  class CellularAutomatonCompiler {
    // LLVM uses a context object to allow multiple threads
    LLVMContext &C;
    // The compilation unit that we are generating
    Module *Mod;
    // The function representing the program
    Function *F;
    // A helper class for generating instructions
    IRBuilder<> B;
    // The 10 local registers in the source language
    Value *a[10];
    // The 10 global registers in the source language
    Value *g[10];
    // The input grid (passed as an argument)
    Value *oldGrid;
    // The output grid (passed as an argument)
    Value *newGrid;
    // The width of the grid (passed as an argument)
    Value *width;
    // The height of the grid (passed as an argument)
    Value *height;
    // The x coordinate of the current cell (passed as an argument)
    Value *x;
    // The y coordinate of the current cell (passed as an argument)
    Value *y;
    // The value of the current cell (passed as an argument, returned at the end)
    Value *v;
    // The type of our registers (currently i16)
    Type *regTy;
    // Stores a value in the specified register.
    void storeInLValue(uintptr_t reg, Value *val) {
      reg >>= 2;
      assert(reg < 22);
      if (reg < 10) {
        B.CreateStore(val, a[reg]);
      } else if (reg < 20) {
        B.CreateStore(val, g[reg-10]);
      } else if (reg == 21) {
        B.CreateStore(val, v);
      }
    }
    // Loads a value from an AST-encoded form.  This may be either a register,
    // a literal (constant), or a pointer to an expression.
    Value *getRValue(uintptr_t val) {
      // If the low bit is 1, then this is either an immediate or a register
      if (val & 1) {
        val >>= 1;
        // Second lowest bit indicates that this is a register
        if (val & 1) {
          val >>= 1;
          assert(val < 22);
          if (val < 10) {
            return B.CreateLoad(a[val]);
          }
          if (val < 20) {
            return B.CreateLoad(g[val - 10]);
          }
          return B.CreateLoad(v);
        }
        // Literal
        return ConstantInt::get(regTy, val >> 1);
      }
      // If the low bit is 0, this is a pointer to an AST node
      return emitStatement((struct ASTNode*)val);
    }

    // A helper function when debugging to allow you to print a register-sized
    // value.  This will print the string in the first argument, followed by
    // the value, and then a newline.  
    void dumpRegister(const char *str, Value *val) {
#ifdef DEBUG_CODEGEN
      std::string format(str);

      format += "%hd\n";
      // Create an array constant with the string data
      Constant *ConstStr = ConstantArray::get(C, format.c_str());
      // Create a global variable storing it
      ConstStr = new GlobalVariable(*Mod, ConstStr->getType(), true,
                                      GlobalValue::InternalLinkage, ConstStr, str);
      // Create the GEP indexes
      Constant *Idxs[] = {ConstantInt::get(Type::getInt32Ty(C), 0), 0 };
      Idxs[1] = Idxs[0];

      std::vector<Type*> Params;
      Params.push_back(PointerType::getUnqual(Type::getInt8Ty(C)));
      // Get the printf() function (takes an i8* followed by variadic parameters)
      Value *PrintF = Mod->getOrInsertFunction("printf",
          FunctionType::get(Type::getVoidTy(C), Params, true));
      // Call printf
      B.CreateCall2(PrintF, ConstantExpr::getGetElementPtr(ConstStr, Idxs, 2), val);
#endif
    }

    public:
    CellularAutomatonCompiler() : C(getGlobalContext()), B(C){
      // Load the bitcode for the runtime helper code
      OwningPtr<MemoryBuffer> buffer;
      MemoryBuffer::getFile("runtime.bc", buffer);
      Mod = ParseBitcodeFile(buffer.get(), C);
      // Get the stub (prototype) for the cell function
      F = Mod->getFunction("cell");
      // Set it to have private linkage, so that it can be removed after being
      // inlined.
      F->setLinkage(GlobalValue::PrivateLinkage);
      // Add an entry basic block to this function and set it
      BasicBlock *entry = BasicBlock::Create(C, "entry", F);
      B.SetInsertPoint(entry);
      // Cache the type of registers
      regTy = Type::getInt16Ty(C);

      // Collect the function parameters
      auto args = F->arg_begin();
      oldGrid = args++;
      newGrid = args++;
      width = args++;
      height = args++;
      x = args++;
      y = args++;

      // Create space on the stack for the local registers
      for (int i=0 ; i<10 ; i++) {
        a[i] = B.CreateAlloca(regTy);
      }
      // Create a space on the stack for the current value.  This can be
      // assigned to, and will be returned at the end.  Store the value passed
      // as a parameter in this.
      v = B.CreateAlloca(regTy);
      B.CreateStore(args++, v);

      // Create a load of pointers to the global registers.
      Value *gArg = args;
      for (int i=0 ; i<10 ; i++) {
        B.CreateStore(ConstantInt::get(regTy, 0), a[i]);
        g[i] = B.CreateConstGEP1_32(gArg, i);
      }
    }

    // Emits a statement or expression in the source language.  For
    // expressions, returns the result, for statements returns NULL.
    Value *emitStatement(struct ASTNode *ast) {
      switch (ast->type) {
        // All of the arithmetic statements have roughly the same format: load
        // the value from a register, use it in a computation, store the result
        // back in the register.
        case ASTNode::NTOperatorAdd:
        case ASTNode::NTOperatorSub:
        case ASTNode::NTOperatorMul:
        case ASTNode::NTOperatorDiv:
        case ASTNode::NTOperatorAssign:
        case ASTNode::NTOperatorMin:
        case ASTNode::NTOperatorMax: {
          // Load the value from the register
          Value *reg = getRValue(ast->val[0]);
          // Evaluate the expression
          Value *expr = getRValue(ast->val[1]);
          // Now perform the operation
          switch (ast->type) {
            // Simple arithmetic operations are single LLVM instructions
            case ASTNode::NTOperatorAdd:
              expr = B.CreateAdd(reg, expr);
              break;
            case ASTNode::NTOperatorSub:
              expr = B.CreateSub(reg, expr);
              break;
            case ASTNode::NTOperatorMul:
              expr = B.CreateMul(reg, expr);
              break;
            case ASTNode::NTOperatorDiv:
              expr = B.CreateSDiv(reg, expr);
              break;
            // Min and Max are implemented by an integer compare (icmp)
            // instruction followed by a select.  The select chooses between
            // two values based on a predicate.
            case ASTNode::NTOperatorMin: {
              Value *gt = B.CreateICmpSGT(expr, reg);
              expr = B.CreateSelect(gt, reg, expr);
              break;
            }
            case ASTNode::NTOperatorMax: {
              Value *gt = B.CreateICmpSGT(expr, reg);
              expr = B.CreateSelect(gt, expr, reg);
              break;
            }
            default: break;
          }
          // Now store the result back in the register.
          storeInLValue(ast->val[0], expr);
          break;
        }
        // Range expressions are more complicated.  They involve some flow
        // control, because we select a different value.
        case ASTNode::NTRangeMap: {
          // Get the structure describing this node.
          struct RangeMap *rm = (struct RangeMap*)ast->val[0];
          // Load the register that we're mapping
          Value *reg = getRValue(rm->value);
          // Now create a basic block for continuation.  This is the block that
          // will be reached after the range expression.
          BasicBlock *cont = BasicBlock::Create(C, "range_continue", F);
          // In this block, create a PHI node that contains the result.  
          PHINode *phi = PHINode::Create(regTy, rm->count, "range_result", cont);
          // Now loop over all of the possible ranges and create a test for each one
          BasicBlock *current= B.GetInsertBlock();
          for (int i=0 ; i<rm->count ; i++) {
            struct RangeMapEntry *re = &rm->entries[i];
            Value *match;
            // If the min and max values are the same, then we just need an
            // equals-comparison
            if (re->min == re->max) {
              Value *val = ConstantInt::get(regTy, (re->min >> 2));
              match = B.CreateICmpEQ(reg, val);
            } else {
              // Otherwise we need to emit both calues and then compare if
              // we're greater-than-or-equal-to the smaller, and
              // less-than-or-equal-to the larger.
              Value *min = ConstantInt::get(regTy, (re->min >> 2));
              Value *max = ConstantInt::get(regTy, (re->max >> 2));
              match = B.CreateAnd(B.CreateICmpSGE(reg, min), B.CreateICmpSLE(reg, max));
            }
            // The match value is now a boolean (i1) indicating whether the
            // value matches this range.  Create a pair of basic blocks, one
            // for the case where we did match the specified range, and one for
            // the case where we didn't.
            BasicBlock *expr = BasicBlock::Create(C, "range_result", F);
            BasicBlock *next = BasicBlock::Create(C, "range_next", F);
            // Branch to the correct block
            B.CreateCondBr(match, expr, next);
            // Now construct the block for the case where we matched a value
            B.SetInsertPoint(expr);
            // getRValue() may emit some complex code, so we need to leave
            // everything set up for it to (potentially) write lots of
            // instructions and create more basic blocks (imagine nested range
            // expressions).  If this is just a constant, then the next basic
            // block will be empty, but the SimplifyCFG pass will remove it.
            phi->addIncoming(getRValue(re->val), B.GetInsertBlock());
            // Now that we've generated the correct value, branch to the
            // continuation block.
            B.CreateBr(cont);
            // ...and repeat
            current = next;
            B.SetInsertPoint(current);
          }
          // Branch to the continuation block if we've fallen off the end, and
          // set the value to 0 for this case.
          B.CreateBr(cont);
          phi->addIncoming(ConstantInt::get(regTy, 0), current);
          B.SetInsertPoint(cont);
          return phi;
        }
        case ASTNode::NTNeighbours: {
          // For each of the (valid) neighbours
          // Start by identifying the bounds
          Value *XMin = B.CreateSub(x, ConstantInt::get(regTy, 1));
          Value *XMax = B.CreateAdd(x, ConstantInt::get(regTy, 1));
          Value *YMin = B.CreateSub(y, ConstantInt::get(regTy, 1));
          Value *YMax = B.CreateAdd(y, ConstantInt::get(regTy, 1));
          // Now clamp them to the grid
          XMin = B.CreateSelect(B.CreateICmpSLT(XMin, ConstantInt::get(regTy, 0)), x, XMin);
          YMin = B.CreateSelect(B.CreateICmpSLT(YMin, ConstantInt::get(regTy, 0)), y, YMin);
          XMax = B.CreateSelect(B.CreateICmpSGE(XMax, width), x, XMax);
          YMax = B.CreateSelect(B.CreateICmpSGE(YMax, height), y, YMax);

          // Now create the loops
          BasicBlock *start = B.GetInsertBlock();
          BasicBlock *xLoopStart = BasicBlock::Create(C, "x_loop_start", F);
          BasicBlock *yLoopStart = BasicBlock::Create(C, "y_loop_start", F);
          Value *I = B.CreateMul(XMin, width);
          B.CreateBr(xLoopStart);
          B.SetInsertPoint(xLoopStart);
          PHINode *XPhi = B.CreatePHI(regTy, 2);
          XPhi->addIncoming(XMin, start);
          B.CreateBr(yLoopStart);
          B.SetInsertPoint(yLoopStart);
          PHINode *YPhi = B.CreatePHI(regTy, 2);
          YPhi->addIncoming(YMin, xLoopStart);

          BasicBlock *endY = BasicBlock::Create(C, "y_loop_end", F);
          BasicBlock *body = BasicBlock::Create(C, "body", F);

          B.CreateCondBr(B.CreateAnd(B.CreateICmpEQ(x, XPhi), B. CreateICmpEQ(y, YPhi)), endY, body);
          B.SetInsertPoint(body);


          for (int i=0 ; i<ast->val[0]; i++) {
            Value *idx = B.CreateAdd(YPhi, B.CreateMul(XPhi, width));
            B.CreateStore(B.CreateLoad(B.CreateGEP(oldGrid, idx)), a[0]);
            emitStatement(((struct ASTNode**)ast->val[1])[i]);
          }
          B.CreateBr(endY);
          B.SetInsertPoint(endY);
          BasicBlock *endX = BasicBlock::Create(C, "x_loop_end", F);
          BasicBlock *cont = BasicBlock::Create(C, "continue", F);
          // Increment the loop country for the next iteration
          YPhi->addIncoming(B.CreateAdd(YPhi, ConstantInt::get(regTy, 1)), endY);
          B.CreateCondBr(B.CreateICmpEQ(YPhi, YMax), endX, yLoopStart);

          B.SetInsertPoint(endX);
          XPhi->addIncoming(B.CreateAdd(XPhi, ConstantInt::get(regTy, 1)), endX);
          B.CreateCondBr(B.CreateICmpEQ(XPhi, XMax), cont, xLoopStart);
          B.SetInsertPoint(cont);

          break;
        }
      }
      return 0;
    }

    // Returns a function pointer for the automaton at the specified
    // optimisation level.
    automaton getAutomaton(int optimiseLevel) {
      // We've finished generating code, so add a return statement - we're
      // returning the value  of the v register.
      B.CreateRet(B.CreateLoad(v));
#ifdef DEBUG_CODEGEN
      // If we're debugging, then print the module in human-readable form to
      // the standard error and verify it.
      Mod->dump();
      verifyModule(*Mod);
#endif
      // Now we need to construct the set of optimisations that we're going to
      // run.
      PassManagerBuilder PMBuilder;
      // Set the optimisation level.  This defines what optimisation passes
      // will be added.
      PMBuilder.OptLevel = optimiseLevel;
      // Create a basic inliner.  This will inline the cell function that we've
      // just created into the automaton function that we're going to create.
      PMBuilder.Inliner = createFunctionInliningPass(275);
      // Now create a function pass manager that is responsible for running
      // passes that optimise functions, and populate it.
      FunctionPassManager *PerFunctionPasses= new FunctionPassManager(Mod);
      PMBuilder.populateFunctionPassManager(*PerFunctionPasses);

      // Run all of the function passes on the functions in our module
      for (Module::iterator I = Mod->begin(), E = Mod->end() ;
           I != E ; ++I) {
          if (!I->isDeclaration())
              PerFunctionPasses->run(*I);
      }
      // Clean up
      PerFunctionPasses->doFinalization();
      delete PerFunctionPasses;
      // Run the per-module passes
      PassManager *PerModulePasses = new PassManager();
      PMBuilder.populateModulePassManager(*PerModulePasses);
      PerModulePasses->run(*Mod);
      delete PerModulePasses;

      // Now we are ready to generate some code.  First create the execution
      // engine (JIT)
      std::string error;
      ExecutionEngine *EE = ExecutionEngine::create(Mod, false, &error);
      if (!EE) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        exit(-1);
      }
      // Now tell it to compile
      return (automaton)EE->getPointerToFunction(Mod->getFunction("automaton"));
    }

  };
}

extern "C"
automaton compile(struct ASTNode **ast, uintptr_t count, int optimiseLevel) {
  // These functions do nothing, they just ensure that the correct modules are
  // not removed by the linker.
  InitializeNativeTarget();
  LLVMLinkInJIT();
  CellularAutomatonCompiler compiler;
  // For each statement, generate some IR
  for (int i=0 ; i<count ; i++) {
    compiler.emitStatement(ast[i]);
  }
  // And then return the compiled version.
  return compiler.getAutomaton(optimiseLevel);
}
