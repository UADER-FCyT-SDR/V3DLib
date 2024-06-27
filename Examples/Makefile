# Root directory of QPULib repository
ROOT = ../Lib; 

# Compiler and default flags
CXX = g++
CXX_FLAGS = -fpermissive -Wconversion -std=c++0x -I $(ROOT)

# Object directory
OBJ_DIR = obj

# Debug mode
ifeq ($(DEBUG), 1)
  CXX_FLAGS += -DDEBUG
  OBJ_DIR := $(OBJ_DIR)-debug
endif

# QPU or emulation mode

#*--- (PEC) fix to force QPU mode
#ifeq ($(QPU), 1)
#  CXX_FLAGS += -DQPU_MODE
#  OBJ_DIR := $(OBJ_DIR)-qpu
#else
#  CXX_FLAGS += -DEMULATION_MODE
#endif

#*--- (PEC) Fix to force QPU mode

CXX_FLAGS += -DQPU_MODE
OBJ_DIR := $(OBJ_DIR)-qpu



# Object files
#OBJ =                         \
#  Kernel.o                    \
#  Source/Syntax.o             \
#  Source/Int.o                \
#  Source/Float.o              \
#  Source/Stmt.o               \
#  Source/Pretty.o             \
#  Source/Translate.o          \
#  Source/Interpreter.o        \
#  Source/Gen.o                \
#  Target/Syntax.o             \
#  Target/SmallLiteral.o       \
#  Target/Pretty.o             \
#  Target/RemoveLabels.o       \

# Top-level targets

.PHONY: top clean

top:
	@echo Please supply a target to build, e.g. \'make GCD\'
	@echo

clean:
	rm -rf obj obj-debug obj-qpu obj-debug-qpu
	rm -f Tri GCD Print MultiTri AutoTest OET Hello ReqRecv Rot3D ID *.o
	rm -f HeatMap

LIB = $(patsubst %,$(OBJ_DIR)/%,$(OBJ))

ID: ID.o $(LIB)
	@echo Linking...
	@$(CXX) $^ -o $@ $(CXX_FLAGS)

Hello: Hello.o $(LIB)
	@echo Linking...
	@$(CXX) $^ -o $@ $(CXX_FLAGS)
