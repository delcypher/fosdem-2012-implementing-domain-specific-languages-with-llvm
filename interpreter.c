#include "AST.h"
#include <strings.h> 
#include <stdio.h> 

// The current state for the interpreter
struct InterpreterState {
  // The local registers
  int16_t a[10];
  // The global registers
  int16_t g[10];
  // The current cell value
  int16_t v;
  // The width of the grid
  int16_t width;
  // The height of the grid
  int16_t height;
  // The x coordinate of the current cell
  int16_t x;
  // The y coordinate of the current cell
  int16_t y;
  // The grid itself
  int16_t *grid;
};

int interpret(struct ASTNode *ast, struct InterpreterState *state);

// Runs a single step
void runOneStep(int16_t *oldgrid, int16_t *newgrid, int16_t width, int16_t height, struct ASTNode **ast, uintptr_t count)
{
  struct InterpreterState state = {0};
  state.grid = oldgrid;
  state.width = width;
  state.height = height;
  int i=0;
  for (int x=0 ; x<width ; x++) {
    for (int y=0 ; y<height ; y++,i++) {
      state.v = oldgrid[i];
      state.x = x;
      state.y = y;
      bzero(state.a, sizeof(state.a));
      for (int step=0 ; step<count ; step++) {
        interpret(ast[step], &state);
      }
      newgrid[i] = state.v;
    }
  }
}

static void
storeInLValue(uintptr_t reg, int val, struct InterpreterState *state) {
  reg >>= 2;
  if (reg < 10) {
    state->a[reg] = val;
  } else if (reg < 20) {
    state->g[reg - 10] = val;
  } else if (reg == 21) {
    state->v = val;
  }
}

static int getRValue(uintptr_t val, struct InterpreterState *state) {
  // If the low bit is 1, then this is either an immediate or a register
  if (val & 1) {
    val >>= 1;
    // Second lowest bit indicates that this is a register
    if (val & 1) {
      val >>= 1;
      if (val < 10) {
        return state->a[val];
      }
      if (val < 20) {
        return state->g[val - 10];
      }
      // Undefined values
      if (val > 21) {
        return -1;
      }
      return state->v;
    }
    // Literal
    return val >> 1;
  }
  // If the low bit is 0, this is a pointer to an AST node
  return interpret((struct ASTNode*)val, state);
}

int interpret(struct ASTNode *ast, struct InterpreterState *state) {
  switch (ast->type) {
    case NTNeighbours:
      // For each of the (valid) neighbours
      for (int x = state->x - 1 ; x <= state->x + 1 ; x++) {
        if (x < 0 || x >= state->width) continue;
        for (int y = state->y - 1 ; y <= state->y + 1 ; y++) {
          if (y < 0 || y >= state->height) continue;
          if (x == state->x && y == state->y) continue;
          for (int i=0 ; i<ast->val[0]; i++) {
            state->a[0] = state->grid[x*state->width + y];
            interpret(((struct ASTNode**)ast->val[1])[i], state);
          }
        }
      }
      break;
    case NTRangeMap: {
      struct RangeMap *rm = (struct RangeMap*)ast->val[0];
      int rvalue = getRValue(rm->value, state);
      for (int i=0 ; i<rm->count ; i++) {
        struct RangeMapEntry *re = &rm->entries[i];
        if ((rvalue >= (re->min >> 2)) && (rvalue <= (re->max >> 2))) {
          return getRValue(re->val, state);
        }
      }
      return 0;
    }
    case NTOperatorAdd:
    case NTOperatorSub:
    case NTOperatorMul:
    case NTOperatorDiv:
    case NTOperatorAssign:
    case NTOperatorMin:
    case NTOperatorMax: {
      int lvalue = getRValue(ast->val[0], state);
      int rvalue = getRValue(ast->val[1], state);
      switch (ast->type) {
        case NTOperatorAdd:
          rvalue = lvalue + rvalue;
          break;
        case NTOperatorSub:
          rvalue = lvalue - rvalue;
          break;
        case NTOperatorMul:
          rvalue = lvalue * rvalue;
          break;
        case NTOperatorDiv:
          rvalue = lvalue / rvalue;
          break;
        case NTOperatorMin:
          if (rvalue > lvalue)
            rvalue = lvalue;
          break;
        case NTOperatorMax: 
          if (rvalue < lvalue)
            rvalue = lvalue;
        default: break;
      }
      storeInLValue(ast->val[0], rvalue, state);
    }
  }
  return 0;
}

void printAST(struct ASTNode *ast) {
  uintptr_t val = (uintptr_t)ast;
  if (val & 1) {
    val >>= 1;
    // Second lowest bit indicates that this is a register
    if (val & 1) {
      val >>= 1;
      if (val < 10) {
        printf("a%d ", (int)val);
      } else if (val < 20) {
        printf("g%d ", (int)val - 10);
        return;
      } else if (val == 21) {
        printf("v ");
      }
    } else {
      // Literal
      printf("%d ", (int)val>>1);
    }
    return;
  }
  switch (ast->type) {
    case NTNeighbours:{
      printf("neighbours (\n");
          for (int i=0 ; i<ast->val[0]; i++) {
            printAST(((struct ASTNode**)ast->val[1])[i]);
      }
      printf(")\n");
      break;
    }
    case NTRangeMap: {
      printf("[ ");
      struct RangeMap *rm = (struct RangeMap*)ast->val[0];
      printAST((struct ASTNode*)rm->value);
      printf("| ");
      for (int i=0 ; i<rm->count ; i++) {
        struct RangeMapEntry *re = &rm->entries[i];
        printf("(");
        printAST((struct ASTNode*)re->min);
        printf(", ");
        printAST((struct ASTNode*)re->max);
        printf(") => ");
        printAST((struct ASTNode*)re->val);
        printf(",");
      }
      printf(" ]");
      break;
    }
    case NTOperatorAdd:
    case NTOperatorSub:
    case NTOperatorMul:
    case NTOperatorDiv:
    case NTOperatorAssign:
    case NTOperatorMin:
    case NTOperatorMax: {
      switch (ast->type) {
        case NTOperatorAssign:
          printf("= ");
          break;
        case NTOperatorAdd:
          printf("+ ");
          break;
        case NTOperatorSub:
          printf("- ");
          break;
        case NTOperatorMul:
          printf("* ");
          break;
        case NTOperatorDiv:
          printf("/ ");
          break;
        case NTOperatorMin:
          printf("min ");
          break;
        case NTOperatorMax: 
          printf("max ");
        default: break;
      }
      printAST((struct ASTNode*)ast->val[0]);
      printAST((struct ASTNode*)ast->val[1]);
      break;
    }
  }
}
