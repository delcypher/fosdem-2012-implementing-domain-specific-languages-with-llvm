#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "grammar.h"
#include "AST.h"

void *CellAtomParseAlloc(void *(*mallocProc)(size_t));
void CellAtomParse(void *yyp, int yymajor, void *yyminor, void* p);
void CellAtomParseFree(void *p, void (*freeProc)(void*));

void CellAtomParseTrace(FILE *stream, char *zPrefix);

#ifdef DEBUG_LEXER
#define CellAtomParse(a,b,c,d) do {\
  fprintf(stderr, "Parsing " #b "\n");\
  CellAtomParse(a,b,c,d);\
}while(0)
#endif

static int enableTiming = 0;

static void logTimeSince(clock_t c1, char *msg)
{
  if (!enableTiming) { return; }
  clock_t c2 = clock();
  struct rusage r;
  getrusage(RUSAGE_SELF, &r);
  fprintf(stderr, "%s took %f seconds.  Peak used %ldKB.\n", msg,
    ((double)c2 - (double)c1) / (double)CLOCKS_PER_SEC, r.ru_maxrss);
}

int main(int argc, char **argv)
{
#ifdef DEBUG_PARSER
  CellAtomParseTrace(stderr, "PARSER: ");
#endif
  int iterations = 1;
  int useJIT = 0;
  int optimiseLevel = 0;
  int gridSize = 5;
  int maxValue = 1;
  clock_t c1;
  int c, f;
  while ((c = getopt(argc, argv, "ji:to:x:m:")) != -1) {
    switch (c) {
      case 'j':
        useJIT = 1;
        break;
      case 'x':
        gridSize = strtol(optarg, 0, 10);
        break;
      case 'm':
        maxValue = strtol(optarg, 0, 10);
        break;
      case 'i':
        iterations = strtol(optarg, 0, 10);
        break;
      case 't':
        enableTiming = 1;
        break;
      case 'o':
        optimiseLevel = strtol(optarg, 0, 10);
    }
  }

  void *parser = CellAtomParseAlloc(malloc);
  char ch;
  struct statements *result;
  while ((ch = getchar()) != EOF) {
resume_parsing:
    if (isspace(ch)) continue;
    switch (ch)
    {
      case '+': CellAtomParse(parser, PLUS, 0, &result); break;
      case '-': CellAtomParse(parser, SUB, 0, &result); break;
      case '*': CellAtomParse(parser, MUL, 0, &result); break;
      case '/': CellAtomParse(parser, DIV, 0, &result); break;
      case '=': CellAtomParse(parser, EQ, 0, &result); break;
      case '>': CellAtomParse(parser, GT, 0, &result); break;
      case '(': CellAtomParse(parser, LBR, 0, &result); break;
      case ')': CellAtomParse(parser, RBR, 0, &result); break;
      case '[': CellAtomParse(parser, LSQ, 0, &result); break;
      case ']': CellAtomParse(parser, RSQ, 0, &result); break;
      case ',': CellAtomParse(parser, COMMA, 0, &result); break;
      case '|': CellAtomParse(parser, BAR, 0, &result); break;
      case 'n': {
        if (!(('e' == getchar()) &&
              ('i' == getchar()) &&
              ('g' == getchar()) &&
              ('h' == getchar()) &&
              ('b' == getchar()) &&
              ('o' == getchar()) &&
              ('u' == getchar()) &&
              ('r' == getchar()) &&
              ('s' == getchar()))) {
          fprintf(stderr, "Unhelpful parser error!\n");
          exit(-1);
        }
        CellAtomParse(parser, NEIGHBOURS, 0, &result);
        break;
      }
      case 'm': {
        ch = getchar();
        if (ch == 'i' && getchar() == 'n') {
          CellAtomParse(parser, MIN, 0, &result);
        } else if (ch == 'a' && getchar() == 'x') {
          CellAtomParse(parser, MAX, 0, &result);
        } else {
          fprintf(stderr, "Unhelpful parser error!\n");
          exit(-1);
        }
        break;
      }
      case 'a': {
        ch = getchar();
        if (!isdigit(ch)) {
          fprintf(stderr, "Unhelpful parser error!\n");
          exit(-1);
        }
        uintptr_t regnum = (digittoint(ch) << 2) | 3;
        CellAtomParse(parser, REGISTER, (void*)regnum, &result);
        break;
      }
      case 'g': {
        ch = getchar();
        if (!isdigit(ch)) {
          fprintf(stderr, "Unhelpful parser error!\n");
          exit(-1);
        }
        uintptr_t regnum = ((digittoint(ch) + 10) << 2) | 3;
        CellAtomParse(parser, REGISTER, (void*)regnum, &result);
        break;
      }
      case 'v': {
        uintptr_t regnum = (21 << 2) | 3;
        CellAtomParse(parser, REGISTER, (void*)regnum, &result);
        break;
      }
      default: {
        if (isdigit(ch)) {
          intptr_t literal = 0;
          do {
            literal *= 10;
            literal += digittoint(ch);
            ch = getchar();
          } while (isdigit(ch));
          literal <<= 2;
          literal |= 1;
          CellAtomParse(parser, NUMBER, (void*)literal, &result);
          // We've consumed one more character than we should, so skip back to
          // the top of the loop without getting a new one.
          goto resume_parsing;
        }
        fprintf(stderr, "Unhelpful parser error!\n");
        exit(-1);
      }
    }
  }
  CellAtomParse(parser, 0, 0, &result);
  CellAtomParseFree(parser, free);
#ifdef DUMP_AST
  for (uintptr_t i=0 ; i<result->count ; i++) {
    printAST(result->list[i]);
    putchar('\n');
  }
#endif
  /*
  int16_t oldgrid[] = {
     0,0,0,0,0,
     0,0,0,0,0,
     0,1,1,1,0,
     0,0,0,0,0,
     0,0,0,0,0
  };
  int16_t newgrid[25];
  */
  int16_t *g1 = malloc(gridSize * sizeof(int16_t) * gridSize);
  for (int i=0 ; i<(gridSize*gridSize) ; i++) {
    g1[i] = random() % (maxValue + 1);
  }
  int16_t *g2 = malloc(gridSize * sizeof(int16_t) * gridSize);
  c1 = clock();
  logTimeSince(c1, "Generating random grid");
  int i=0;
  if (useJIT) {
    c1 = clock();
    automaton ca = compile(result->list, result->count, optimiseLevel);
    logTimeSince(c1, "Compiling");
    c1 = clock();
    for (int i=0 ; i<iterations ; i++) {
      int16_t *tmp = g1;
      ca(g1, g2, gridSize, gridSize);
      g1 = g2;
      g2 = tmp;
    }
    logTimeSince(c1, "Running compiled version");
  } else {
    c1 = clock();
    for (int i=0 ; i<iterations ; i++) {
      int16_t *tmp = g1;
      runOneStep(g1, g2, gridSize, gridSize, result->list, result->count);
      g1 = g2;
      g2 = tmp;
    }
    logTimeSince(c1, "Interpreting");
  }
  for (int x=0 ; x<gridSize ; x++) {
    for (int y=0 ; y<gridSize ; y++) {
      printf("%d ", g1[i++]);
    }
    putchar('\n');
  }
  return 0;
}
