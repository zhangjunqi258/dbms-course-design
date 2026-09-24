# 简易数据库管理系统（DBMS）—— 数据库原理课程设计
#
# 用法（Windows + MinGW）：
#     mingw32-make          # 编译
#     mingw32-make run      # 编译并运行
#     mingw32-make clean    # 清理

CXX      := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O2
TARGET   := dbms.exe
SRCS     := src/main.cpp src/storage.cpp src/sql.cpp
HDRS     := src/dbms.h

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	-del /Q $(TARGET) 2>nul
	-del /Q *.o 2>nul
