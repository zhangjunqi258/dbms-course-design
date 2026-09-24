# 简易数据库管理系统（DBMS）

> 数据库原理课程设计作品 —— 用 C++ 从零实现一个带自定义文件存储格式的迷你数据库管理系统。

本项目实现了课程设计要求中的 **全部功能模块**：

1. 表结构模式（字段名/类型/字长/KEY/NULL/VALID）在 `.dbf` 文件中的自定义存储；
2. 表记录在 `.dat` 文件中的自定义存储；
3. 表结构的创建与修改（增字段、删字段、改字段名、改字段类型）；
4. 记录的插入、删除、修改与查询；
5. DDL（CREATE / EDIT / RENAME / DROP TABLE）与 DML（INSERT / DELETE / UPDATE）的语句解析与执行；
6. 单表选择、投影查询与多表连接查询（SELECT … WHERE …）。

---

## 一、运行环境

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11（也可在 Linux/macOS 下用 g++ 编译） |
| 编译器 | MinGW-w64 的 `g++`（支持 C++11 即可），已在 g++ 13 上测试通过 |
| 编辑器 | VS Code / Dev-C++ / Visual Studio 均可 |
| 编码 | 源文件与终端均使用 UTF-8 |

检查编译器是否就绪：

```bash
g++ --version
```

## 二、编译与运行

方式一：双击 `build.bat`（Windows 一键编译）。

方式二：命令行：

```bash
g++ -std=c++11 -Wall -Wextra -O2 -o dbms.exe src/main.cpp src/storage.cpp src/sql.cpp
dbms.exe
```

方式三：使用 Makefile：

```bash
mingw32-make          # 编译
mingw32-make run      # 编译并运行
```

> 提示：若 Windows 控制台中文显示异常，先执行 `chcp 65001` 再运行程序。

## 三、目录结构

```
DBMS/
├── src/
│   ├── dbms.h        公共头文件：数据结构定义 + 所有模块接口
│   ├── storage.cpp   存储层：.dbf / .dat 文件的读写（自定义二进制格式）
│   ├── sql.cpp       语义层：词法分析、语法分析、语句执行（DDL/DML/SELECT）
│   └── main.cpp      界面层：控制台菜单交互
├── sample/
│   └── demo.sql      示范用例（建3张表、插入记录、查询、改结构、删表）
├── docs/
│   ├── 课程设计报告.md    课程设计报告草稿（可直接改用）
│   ├── 课程设计报告.docx  同上，Word 版
│   └── PPT大纲.md         3分钟答辩 PPT 提纲
├── Makefile
├── build.bat
├── README.md
└── .gitignore
```

## 四、文件存储格式（本项目的核心）

### 4.1 数据库文件 `xxx.dbf`（存表结构）

```
┌──────┬────────────┬──────────┬───────────────────────────┐
│ '~'  │ 表名 char[15] │ 字段个数 int │ 字段结构 TableMode × n      │
└──────┴────────────┴──────────┴───────────────────────────┘
   ↑ 以上是“一张表”；紧接着从 '~' 开始存放第二张表 ……
```

其中每个字段结构（与课程设计要求给出的定义一致）：

```c
typedef struct {
    char sFieldName[15];  // 字段名
    char sType[8];        // 字段类型
    int  iSize;           // 字长
    char bKey;            // 是否 KEY 键  y/n
    char bNullFlag;       // 是否允许为空 y/n
    char bValidFlag;      // 字段是否有效 y/n（预留，用于删除字段）
} TableMode;
```

### 4.2 数据文件 `xxx.dat`（存表记录）

```
┌──────┬────────────┬────────────┬────────────┬──────────────────┬────────────────────┐
│ '~'  │ 表名 char[15] │ 记录总数 int │ 字段个数 int │ 有效标识 char[n]   │ 记录 1…记录 n        │
└──────┴────────────┴────────────┴────────────┴──────────────────┴────────────────────┘
```

每条记录按字段顺序、以“字段字长”定长存放（不足部分用空格补齐）。  
记录的删除采用 **逻辑删除**：把对应位置的有效标识置 `'0'`，数据仍然保留，便于日后恢复。

一个数据库 = `xxx.dbf`（结构） + `xxx.dat`（数据）两个文件，两个文件都可存放多张表。

## 五、SQL 语法

| 类别 | 语句 |
| --- | --- |
| 定义表 | `CREATE TABLE 表名 ( 字段 类型[字长] KEY标志 空值标志 VALID标志, … ) INTO 库名;` |
| 修改表 | `EDIT TABLE 表名 ( 字段 类型[字长] … ) IN 库名;` |
| 改字段名 | `EDIT TABLE 表名 ( 旧字段 RENAME TO 新字段 ) IN 库名;` |
| 改表名 | `RENAME TABLE 旧表名 新表名 IN 库名;` |
| 删表 | `DROP TABLE 表名 IN 库名;` |
| 插入 | `INSERT INTO 表名 VALUES ( 值1, 值2, … ) IN 库名;` |
| 删除 | `DELETE FROM 表名 WHERE 字段 运算符 值 IN 库名;` |
| 修改 | `UPDATE 表名 ( SET 字段=值 [, …] WHERE 字段=值 ) IN 库名;` |
| 查询 | `SELECT * \| 字段1, 字段2 … FROM 表1, 表2 … [WHERE 条件];` |

约定说明：

- 标志位：`KEY / NOT_KEY`、`NULL / NO_NULL`、`VALID / INVALID`；
- 类型与字长：`char[10]`、`int`、`float`、`date`，不写 `[n]` 时按默认字长（char=10, int=4, float=8, date=10）；
- 比较运算符：`=  <>  >  <  >=  <=`，多条件用 `AND` / `OR` 连接（从左到右求值）；
- 字符串常量可用单引号或不加引号；
- 支持全角括号、全角逗号，方便直接复制课件的语句；
- 多表查询时，同名字段请写成 `表名.字段名`。

### 示例

```sql
CREATE TABLE Student (
  Sno   char[10] KEY     NO_NULL VALID,
  Sname char[10] NOT_KEY NO_NULL VALID,
  Sage  int      NOT_KEY NULL    VALID
) INTO MyDB;

INSERT INTO Student VALUES ( '1001', 'ZhangSan', '20' ) IN MyDB;

SELECT Student.Sname, SC.Grade
FROM Student, SC
WHERE Student.Sno = SC.Sno IN MyDB;

UPDATE Student ( SET Sage = '21' WHERE Sno = '1001' ) IN MyDB;

DELETE FROM Student WHERE Sno = '1004' IN MyDB;
```

## 六、功能菜单

```
1. 新建 / 切换数据库
2. 建表（CREATE TABLE）
3. 查看表结构（SHOW / DESCRIBE）
4. 修改表结构（增删改字段 / 改表名 / 删表）
5. 插入记录（INSERT）
6. 查看记录 / 查询（SELECT）
7. 删除记录（DELETE）
8. 修改记录（UPDATE）
9. 直接执行 SQL 语句
0. 退出
```

菜单操作最终都会转换为 SQL 语句交给语义层执行，因此界面操作和手写 SQL 的行为完全一致。

## 七、快速体验

1. 编译并运行 `dbms.exe`；
2. 选 `1`，输入数据库名 `MyDB`；
3. 选 `9`，把 `sample/demo.sql` 的内容逐行粘贴进去（输入 `end` 退出）；
4. 选 `3` / `6` 查看当前库中表结构和记录。

也可以直接在菜单里按提示一步步操作，无需手写 SQL。

## 八、可继续改进的方向

- 增加索引（按 KEY 字段建立简单的索引文件，加速查询）；
- 把定长的 `TableMode` 改为变长存储，支持更多字段；
- 增加 `ORDER BY` / `GROUP BY` / 聚合函数（SUM/AVG/COUNT）；
- 增加事务支持（操作日志 + 回滚）；
- 把菜单升级为图形界面（Qt / Win32 / Web 前端 + 后端接口）；
- 增加 `SHOW TABLES`、`DESCRIBE 表名` 等 SQL 语句。

## 九、Git 使用建议

```bash
git init
git add .
git commit -m "feat: 完成数据库原理课程设计 - 简易DBMS"
git branch -M main
git remote add origin <你的仓库地址>
git push -u origin main
```

建议后续每完成一个功能就提交一次，提交信息写清楚做了什么，方便回退和展示开发过程。
