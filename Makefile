# ----------------------------------------------------------------------------
#  Magic Splash Wand - C++ / Win32 GUI
#
#  Build environment : MSYS2  ->  "MSYS2 UCRT64" shell
#  Sources           : src/*.cpp, src/resource.rc
#  Objects           : build/obj/
#  Final executable  : dist/MagicSplashWand.exe   (statically linked, no DLLs needed)
#
#  Usage (in the UCRT64 shell, inside this folder):
#      make            build  ->  dist/MagicSplashWand.exe
#      make run        build and start the program
#      make clean      remove build/ and dist/
#      make rebuild    clean + build
#      make -j8        parallel build
# ----------------------------------------------------------------------------

APP       := MagicSplashWand
SRC_DIR   := src
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
DIST_DIR  := dist
TARGET    := $(DIST_DIR)/$(APP).exe

CXX       := g++
WINDRES   := windres

CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra \
             -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
             -MMD -MP
# -mwindows : GUI subsystem (no console window)   -municode : wWinMain entry point
# -static   : zlib, libstdc++, libgcc, winpthread are linked in -> single portable .exe
LDFLAGS   := -static -static-libgcc -static-libstdc++ -mwindows -municode -s
LDLIBS    := -lgdiplus -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lz

SRCS      := $(wildcard $(SRC_DIR)/*.cpp)
OBJS      := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
RES_OBJ   := $(OBJ_DIR)/resource.o

.PHONY: all run clean rebuild
all: $(TARGET)
	@echo "==> Done: $(TARGET)"

$(TARGET): $(OBJS) $(RES_OBJ) | $(DIST_DIR)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# windres runs inside src/ so that "app.ico" / "app.manifest" are found.
$(RES_OBJ): $(SRC_DIR)/resource.rc $(SRC_DIR)/app.manifest $(SRC_DIR)/app.ico | $(OBJ_DIR)
	cd $(SRC_DIR) && $(WINDRES) -O coff -i resource.rc -o ../$(RES_OBJ)

$(OBJ_DIR) $(DIST_DIR):
	mkdir -p $@

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

rebuild: clean all

-include $(OBJS:.o=.d)
