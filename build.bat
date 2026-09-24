@echo off
chcp 65001 >nul
echo ============================================
echo   编译 简易数据库管理系统 (DBMS)
echo ============================================
g++ -std=c++11 -Wall -Wextra -O2 -o dbms.exe src\main.cpp src\storage.cpp src\sql.cpp
if %errorlevel%==0 (
    echo.
    echo [成功] 已生成 dbms.exe
    echo        双击或在命令行输入 dbms.exe 即可运行
) else (
    echo.
    echo [失败] 编译出错，请检查是否已把 MinGW 的 g++ 加入 PATH
)
echo.
pause
