CC=clang
#LLVM_LIBS=analysis archive bitreader bitwriter codegen core engine executionengine instrumentation interpreter ipa ipo jit linker native nativecodegen scalaropts selectiondag support target transformutils
LLVM_LIBS=all

all: cellatom

cellatom: interpreter.o main.o grammar.o compiler.o runtime.bc
	clang++ compiler.o interpreter.o grammar.o main.o `llvm-config --ldflags --libs ${LLVM_LIBS}` -o cellatom

interpreter.o: interpreter.c AST.h
main.o: main.c AST.h grammar.h

runtime.bc: runtime.c
	clang -c -emit-llvm runtime.c -o runtime.bc -O0

compiler.o: compiler.cc AST.h
	clang++ -std=c++0x `llvm-config --cxxflags` -c compiler.cc -g -O0 -fno-inline
grammar.h: grammar.c

grammar.c: grammar.y AST.h lemon
	./lemon grammar.y


lemon: lemon.c
	cc lemon.c -o lemon

clean:
	rm -f interpreter.o main.o grammar.o compiler.o runtime.bc grammar.h grammar.out cellatom lemon
