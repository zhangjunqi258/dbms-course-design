/* ============================================================
 * dbms.h  —  数据库原理课程设计  简易DBMS  公共头文件
 * ------------------------------------------------------------
 * 设计说明：
 *   1) 表结构模式存放在自定义的 .dbf 二进制文件中（可存多张表）；
 *   2) 表记录存放在自定义的 .dat 二进制文件中（可存多张表的记录）；
 *   3) 支持 DDL（CREATE/EDIT/RENAME/DROP TABLE）、
 *      DML（INSERT/DELETE/UPDATE）、查询（SELECT 选择/投影/连接）。
 * ============================================================ */
#ifndef DBMS_H
#define DBMS_H

#include <string>
#include <vector>

namespace dbms {

/* ---------- 常量定义（与课程设计要求保持一致） ---------- */
const int FIELD_NAME_LENGTH = 15;  /* 字段名最大长度（含'\0'） */
const int FILE_NAME_LENGTH  = 15;  /* 表名/文件名最大长度（含'\0'） */
const int TYPE_LENGTH       = 8;   /* 类型名最大长度（含'\0'） */
const int MAX_FIELDS        = 50;  /* 单表最大字段数 */

/* ------------------------------------------------------------
 * TableMode：表结构模式中“一个字段”的存储结构
 * （与课程设计内容提要给出的定义完全一致）
 * ------------------------------------------------------------ */
typedef struct {
    char sFieldName[FIELD_NAME_LENGTH]; /* 字段名 */
    char sType[TYPE_LENGTH];            /* 字段类型 */
    int  iSize;                         /* 字长 */
    char bKey;                          /* 该字段是否为KEY键 y/n */
    char bNullFlag;                     /* 该字段是否允许为空 y/n */
    char bValidFlag;                    /* 该字段是否有效 y/n（预留，用于删除字段） */
} TableMode, *PTableMode;

/* ---------- 内存中的表结构定义 ---------- */
struct FieldDef {
    std::string name;       /* 字段名 */
    std::string type;       /* 类型：char / int / float / date */
    int  size;              /* 字长 */
    bool isKey;             /* 是否 KEY */
    bool allowNull;         /* 是否允许 NULL */
    bool valid;             /* 是否有效 */

    FieldDef() : size(0), isKey(false), allowNull(false), valid(true) {}
};

struct TableDef {
    std::string name;
    std::vector<FieldDef> fields;
    int keyIndex() const;                     /* KEY 字段下标，无则 -1 */
    int findField(const std::string& n) const;/* 字段下标，无则 -1 */
};

/* ---------- 内存中的记录 ---------- */
struct Record {
    std::vector<std::string> values;  /* 各字段的文本值 */
    bool valid;                       /* 有效标志（用于删除/恢复） */
    Record() : valid(true) {}
};

/* ---------- 一个数据库（= 一个 .dbf 文件 + 一个 .dat 文件） ---------- */
struct Database {
    std::string name;                 /* 数据库名（不含扩展名） */
    std::vector<TableDef> tables;
    int findTable(const std::string& n) const;   /* 表下标，无则 -1 */
};

/* ---------- .dat 数据文件在内存中的表示 ---------- */
struct DataFile {
    std::vector<std::string> names;                    /* 各表名 */
    std::vector<std::vector<Record> > records;         /* 各表的记录 */
    int find(const std::string& t) const;              /* 表下标，无则 -1 */
};

/* ================= 通用工具 ================= */
std::string trimStr(const std::string& s);
std::string upperStr(std::string s);
bool iequals(const std::string& a, const std::string& b);
int  defaultSize(const std::string& type);
bool isNumber(const std::string& s);
std::string toSqlErr(const std::string& msg);

/* ================= 存储层（.dbf / .dat 读写） ================= */
bool loadDatabase(const std::string& dbName, Database& db);
bool saveDatabase(const Database& db, const std::string& dbName);

/* 说明：.dat 文件中不保存字段字长（与课程设计要求一致），
 * 解析记录时必须依赖 .dbf 中的表结构，故需要传入 db。 */
bool loadDataFile(const std::string& dbName, const Database& db, DataFile& df);
bool saveDataFile(const std::string& dbName, const Database& db, const DataFile& df);

bool dropTableRecords(DataFile& df, const std::string& tableName);
bool renameTableRecords(DataFile& df, const std::string& oldName,
                        const std::string& newName);
bool ensureTableSection(DataFile& df, const std::string& tableName);

/* ================= 查询结果 ================= */
struct QueryResult {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string> > rows;
};

/* ================= SQL 层 ================= */
/* 执行一条 SQL 语句：
 *   db      —— 当前数据库（若语句中指定了别的库，会自动切换）
 *   sql     —— SQL 文本
 *   outline —— 输出：执行结果的可读文本
 *   qr      —— 可选：SELECT 的结果集
 * 返回 true 表示执行成功。 */
bool execSQL(Database& db, const std::string& sql,
             std::string& outline, QueryResult* qr = 0);

/* ================= 输出辅助 ================= */
void printQueryResult(const QueryResult& r);
std::string formatTable(const TableDef& t);

/* ================= 交互菜单（main.cpp） ================= */
void menuSwitchDatabase(Database& db);
void menuCreateTable(Database& db);
void menuShowTables(Database& db);
void menuAlterTable(Database& db);
void menuInsertRecord(Database& db);
void menuViewRecords(Database& db);
void menuDeleteRecord(Database& db);
void menuUpdateRecord(Database& db);

} /* namespace dbms */
#endif /* DBMS_H */
