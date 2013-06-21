%include {
	#include "AST.h"
	#include <assert.h>
	#include <stdlib.h>
	#include <string.h>

	static inline void* allocNode(int op, void *reg, void *rval)
	{
		struct ASTNode *node = calloc(1, sizeof(struct ASTNode));
		node->type = op;
		node->val[0] = (uintptr_t)reg;
		node->val[1] = (uintptr_t)rval;
		return node;
	}
	static inline void *combineRanges(struct RangeMap *rl, struct RangeMap *r)
	{
		if (rl == NULL) { return r; }
		rl = realloc(rl, sizeof(struct RangeMap) + (sizeof(struct RangeMapEntry) *(rl->count + r->count)));
		memcpy(&rl->entries[rl->count], r->entries, r->count * sizeof(struct RangeMapEntry));
		rl->count += r->count;
		free(r);
		return rl;
	}
}
%extra_argument {void **p}
%name CellAtomParse
%token_type {void*}
%syntax_error {
	fprintf(stderr, "Syntax error!\n");
	exit(-1);
}

program ::= statement_list(L) .
{
	*p = L;
}

statement_list(L) ::= statement(S) statement_list(SL).
{
	// This is really inefficient, but it's good enough for a toy parser.
	uintptr_t *l = (uintptr_t*)SL;
	if (l == NULL)
	{
		l = calloc(sizeof(uintptr_t), 2);
		l[0] = 1;
		l[1] = (uintptr_t)S;
		L = (void*)l;
	}
	else
	{
		uintptr_t count = l[0];
		l = realloc(l, (count + 2) * sizeof(uintptr_t)); 
		memmove(&l[2], &l[1], count * sizeof(uintptr_t));
		l[0]++;
		l[1] = (uintptr_t)S;
	}
	L = (void*)l;
}

statement_list(L) ::= .
{
	L = NULL;
}

statement(S) ::= expression(E) .
{
	S = E;
}

expression(E) ::= NUMBER(N) .
{
	E = N;
}

expression(E) ::= REGISTER(R) .
{
	E = R;
}

expression(E) ::= LSQ REGISTER(R) BAR range_list(L) .
{
	struct RangeMap *rl = L;
	rl->value = (uintptr_t)R;
	E = allocNode(NTRangeMap, rl, 0);
}

range_list(L) ::= ranges(R) RSQ.
{
	L = R;
}
range_list(L) ::= ranges(RL) range(R) RSQ.
{
	L = combineRanges(RL, R);
}
ranges(L) ::= ranges(RL) range(R) COMMA .
{
	L = combineRanges(RL, R);
}
ranges ::= .


range(R) ::= LBR NUMBER(L) COMMA NUMBER(G) RBR EQ GT expression(V) .
{
	struct RangeMap *rm =
		calloc(1, sizeof(struct RangeMap) + sizeof(struct RangeMapEntry));
	rm->count = 1;
	rm->entries[0].min = (uintptr_t)L;
	rm->entries[0].max = (uintptr_t)G;
	rm->entries[0].val = (uintptr_t)V;
	R = rm;
}
range(R) ::= NUMBER(N) EQ GT expression(V) .
{
	struct RangeMap *rm =
		calloc(1, sizeof(struct RangeMap) + sizeof(struct RangeMapEntry));
	rm->count = 1;
	rm->entries[0].min = (uintptr_t)N;
	rm->entries[0].max = (uintptr_t)N;
	rm->entries[0].val = (uintptr_t)V;
	R = rm;
}



statement(S) ::= PLUS REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorAdd, R, E);
}
statement(S) ::= SUB REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorSub, R, E);
}
statement(S) ::= MUL REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorMul, R, E);
}
statement(S) ::= DIV REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorDiv, R, E);
}
statement(S) ::= MIN REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorMin, R, E);
}
statement(S) ::= MAX REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorMax, R, E);
}
statement(S) ::= EQ REGISTER(R) expression(E) .
{
	S = allocNode(NTOperatorAssign, R, E);
}
statement(S) ::= NEIGHBOURS LBR statement_list(L) RBR .
{
	uintptr_t *l = (uintptr_t*)L;
	uintptr_t count = l[0];
	memmove(l, l+1, count * sizeof(uintptr_t));
	S = allocNode(NTNeighbours, (void*)count, l);
}
