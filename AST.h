#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The AST node structure.  This contains a node type and two values that can
// be used to represent children.  There are three kinds of AST elements,
// identified by the low two bits in a pointer.  If the low bit is 0, then it
// is a pointer to one of these structures.  If it is 1, then the value is
// either an integer literal or a register.
struct ASTNode {
  enum {
    NTNeighbours,
    NTRangeMap,
    NTOperatorAdd,
    NTOperatorSub,
    NTOperatorMul,
    NTOperatorDiv,
    NTOperatorAssign,
    NTOperatorMin,
    NTOperatorMax
  } type;
  uintptr_t val[2];
};
// A complete program.  This is an array of AST nodes representing single
// statements.
struct statements
{
  uintptr_t count;
  struct ASTNode *list[0];
};


// An entry in a range map.  This
struct RangeMapEntry {
  intptr_t min;
  intptr_t max;
  intptr_t val;
};
// Range expressions use this structure in one of the val pointers.  It
// contains an array of RangeMapEntries
struct RangeMap {
  intptr_t value;
  intptr_t count;
  struct RangeMapEntry entries[0];
};

void printAST(struct ASTNode *ast);
void runOneStep(int16_t *oldgrid, int16_t *newgrid, int16_t width, int16_t height, struct ASTNode **ast, uintptr_t count);
typedef void(*automaton)(int16_t *oldgrid, int16_t *newgrid, int16_t width, int16_t height);
automaton compile(struct ASTNode **ast, uintptr_t count, int optimiseLevel);
#ifdef __cplusplus
}
#endif
