/* ============================================================
 * storage.cpp  —  存储层实现：.dbf（表结构）与 .dat（表记录）
 * ------------------------------------------------------------
 * 数据库文件 .dbf 的存储结构（可存放多张表）：
 *   [表分隔符 '~'] [表名 char[15]] [字段个数 int]
 *   [字段结构 TableMode] * 字段个数
 *   ... 下一张表从 '~' 开始 ...
 *
 * 数据文件 .dat 的存储结构（可存放多张表的记录）：
 *   [标识 '~'] [表名 char[15]] [记录总数 int] [字段个数 int]
 *   [各记录有效标识 char[记录总数]]
 *   [记录1 字段1][记录1 字段2]...[记录1 字段n]
 *   [记录2 字段1]......        （字段按字长定长存放，空格补齐）
 *   ... 下一张表从 '~' 开始 ...
 * 说明：记录采用“逻辑删除”，被删除记录仅把有效标识置 '0'，
 *       数据仍保留，便于以后恢复。
 * ============================================================ */
#include "dbms.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace dbms {

/* ================= 通用工具 ================= */

std::string trimStr(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string upperStr(std::string s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] - 'a' + 'A');
    return s;
}

bool iequals(const std::string& a, const std::string& b) {
    return upperStr(a) == upperStr(b);
}

bool isNumber(const std::string& s) {
    std::string t = trimStr(s);
    if (t.empty()) return false;
    char* end = 0;
    strtod(t.c_str(), &end);
    return end != 0 && *end == '\0';
}

int defaultSize(const std::string& type) {
    std::string t = upperStr(trimStr(type));
    if (t == "INT" || t == "INTEGER") return 4;
    if (t == "FLOAT" || t == "DOUBLE" || t == "REAL") return 8;
    if (t == "DATE") return 10;
    return 10;                       /* char / varchar 默认 10 */
}

std::string toSqlErr(const std::string& msg) {
    return "错误：" + msg;
}

/* ================= TableDef / Database / DataFile ================= */

int TableDef::keyIndex() const {
    for (size_t i = 0; i < fields.size(); ++i)
        if (fields[i].isKey) return (int)i;
    return -1;
}

int TableDef::findField(const std::string& n) const {
    for (size_t i = 0; i < fields.size(); ++i)
        if (iequals(fields[i].name, n)) return (int)i;
    return -1;
}

int Database::findTable(const std::string& n) const {
    for (size_t i = 0; i < tables.size(); ++i)
        if (iequals(tables[i].name, n)) return (int)i;
    return -1;
}

int DataFile::find(const std::string& t) const {
    for (size_t i = 0; i < names.size(); ++i)
        if (iequals(names[i], t)) return (int)i;
    return -1;
}

/* ================= 文件名拼接 ================= */

static std::string dbfPath(const std::string& dbName) {
    return dbName + ".dbf";
}

static std::string datPath(const std::string& dbName) {
    return dbName + ".dat";
}

/* 把文件中的 15 字节表名字段转成 std::string（遇到 '\0' 截断） */
static std::string fixedName(const char* buf, int n) {
    char tmp[64];
    int i = 0;
    for (; i < n - 1 && i < 63 && buf[i] != '\0'; ++i) tmp[i] = buf[i];
    tmp[i] = '\0';
    return std::string(tmp);
}

/* ================= .dbf 读写 ================= */

bool saveDatabase(const Database& db, const std::string& dbName) {
    FILE* fp = fopen(dbfPath(dbName).c_str(), "wb");
    if (!fp) return false;

    for (size_t t = 0; t < db.tables.size(); ++t) {
        const TableDef& tb = db.tables[t];
        if (!tb.name.empty() && tb.name.size() >= (size_t)FILE_NAME_LENGTH) {
            fclose(fp);
            std::remove(dbfPath(dbName).c_str());
            return false;
        }
        char sep = '~';
        fwrite(&sep, 1, 1, fp);

        char nameBuf[FILE_NAME_LENGTH];
        memset(nameBuf, 0, sizeof(nameBuf));
        strncpy(nameBuf, tb.name.c_str(), FILE_NAME_LENGTH - 1);
        fwrite(nameBuf, 1, FILE_NAME_LENGTH, fp);

        int n = (int)tb.fields.size();
        fwrite(&n, sizeof(int), 1, fp);

        for (int i = 0; i < n; ++i) {
            TableMode tm;
            memset(&tm, 0, sizeof(tm));
            strncpy(tm.sFieldName, tb.fields[i].name.c_str(), FIELD_NAME_LENGTH - 1);
            strncpy(tm.sType, tb.fields[i].type.c_str(), TYPE_LENGTH - 1);
            tm.iSize      = tb.fields[i].size;
            tm.bKey       = tb.fields[i].isKey      ? 'y' : 'n';
            tm.bNullFlag  = tb.fields[i].allowNull  ? 'y' : 'n';
            tm.bValidFlag = tb.fields[i].valid      ? 'y' : 'n';
            fwrite(&tm, sizeof(TableMode), 1, fp);
        }
    }
    fclose(fp);
    return true;
}

bool loadDatabase(const std::string& dbName, Database& db) {
    db.name = dbName;
    db.tables.clear();

    FILE* fp = fopen(dbfPath(dbName).c_str(), "rb");
    if (!fp) return false;                 /* 数据库不存在，按空库处理 */

    while (true) {
        char sep = 0;
        if (fread(&sep, 1, 1, fp) != 1) break;      /* 文件结束 */
        if (sep != '~') { fclose(fp); return false; }

        TableDef tb;
        char nameBuf[FILE_NAME_LENGTH];
        if (fread(nameBuf, 1, FILE_NAME_LENGTH, fp) != (size_t)FILE_NAME_LENGTH) {
            fclose(fp); return false;
        }
        tb.name = fixedName(nameBuf, FILE_NAME_LENGTH);

        int n = 0;
        if (fread(&n, sizeof(int), 1, fp) != 1) { fclose(fp); return false; }
        if (n < 0 || n > MAX_FIELDS) { fclose(fp); return false; }

        for (int i = 0; i < n; ++i) {
            TableMode tm;
            if (fread(&tm, sizeof(TableMode), 1, fp) != 1) { fclose(fp); return false; }
            FieldDef f;
            f.name      = fixedName(tm.sFieldName, FIELD_NAME_LENGTH);
            f.type      = fixedName(tm.sType, TYPE_LENGTH);
            f.size      = tm.iSize;
            f.isKey     = (tm.bKey == 'y' || tm.bKey == 'Y');
            f.allowNull = (tm.bNullFlag == 'y' || tm.bNullFlag == 'Y');
            f.valid     = (tm.bValidFlag == 'y' || tm.bValidFlag == 'Y');
            if (f.size <= 0) f.size = defaultSize(f.type);
            tb.fields.push_back(f);
        }
        db.tables.push_back(tb);
    }
    fclose(fp);
    return true;
}

/* ================= .dat 读写 ================= */

bool loadDataFile(const std::string& dbName, const Database& db, DataFile& df) {
    df.names.clear();
    df.records.clear();

    FILE* fp = fopen(datPath(dbName).c_str(), "rb");
    if (!fp) return true;                  /* 尚无记录文件，视为空 */

    while (true) {
        char sep = 0;
        if (fread(&sep, 1, 1, fp) != 1) break;
        if (sep != '~') { fclose(fp); return false; }

        char nameBuf[FILE_NAME_LENGTH];
        if (fread(nameBuf, 1, FILE_NAME_LENGTH, fp) != (size_t)FILE_NAME_LENGTH) {
            fclose(fp); return false;
        }
        std::string tname = fixedName(nameBuf, FILE_NAME_LENGTH);

        int recCount = 0, fieldCount = 0;
        if (fread(&recCount, sizeof(int), 1, fp) != 1) { fclose(fp); return false; }
        if (fread(&fieldCount, sizeof(int), 1, fp) != 1) { fclose(fp); return false; }
        if (recCount < 0 || fieldCount < 0 || fieldCount > MAX_FIELDS) {
            fclose(fp); return false;
        }

        /* 依据 .dbf 中的表结构取得各字段字长 */
        int tIdx = db.findTable(tname);
        if (tIdx < 0) { fclose(fp); return false; }   /* .dbf 与 .dat 不一致 */
        const TableDef& tb = db.tables[tIdx];
        if ((int)tb.fields.size() != fieldCount) { fclose(fp); return false; }

        std::vector<Record> recs;
        if (recCount > 0) {
            std::vector<char> flags(recCount, '1');
            if (fread(&flags[0], 1, recCount, fp) != (size_t)recCount) {
                fclose(fp); return false;
            }
            for (int r = 0; r < recCount; ++r) {
                Record rec;
                rec.valid = (flags[r] != '0');
                rec.values.resize(fieldCount);
                for (int c = 0; c < fieldCount; ++c) {
                    int w = tb.fields[c].size > 0 ? tb.fields[c].size : 1;
                    std::vector<char> buf(w, 0);
                    if (fread(&buf[0], 1, w, fp) != (size_t)w) { fclose(fp); return false; }
                    rec.values[c] = trimStr(std::string(&buf[0], w));
                }
                recs.push_back(rec);
            }
        }
        df.names.push_back(tname);
        df.records.push_back(recs);
    }
    fclose(fp);
    return true;
}

bool saveDataFile(const std::string& dbName, const Database& db, const DataFile& df) {
    FILE* fp = fopen(datPath(dbName).c_str(), "wb");
    if (!fp) return false;

    for (size_t i = 0; i < df.names.size(); ++i) {
        int tIdx = db.findTable(df.names[i]);
        if (tIdx < 0) continue;                      /* 已删除的表不再写入 */
        const TableDef& tb = db.tables[tIdx];
        int fieldCount = (int)tb.fields.size();

        char sep = '~';
        fwrite(&sep, 1, 1, fp);

        char nameBuf[FILE_NAME_LENGTH];
        memset(nameBuf, 0, sizeof(nameBuf));
        strncpy(nameBuf, df.names[i].c_str(), FILE_NAME_LENGTH - 1);
        fwrite(nameBuf, 1, FILE_NAME_LENGTH, fp);

        int recCount = (int)df.records[i].size();
        fwrite(&recCount, sizeof(int), 1, fp);
        fwrite(&fieldCount, sizeof(int), 1, fp);

        if (recCount > 0) {
            std::vector<char> flags(recCount, '1');
            for (int r = 0; r < recCount; ++r)
                flags[r] = df.records[i][r].valid ? '1' : '0';
            fwrite(&flags[0], 1, recCount, fp);
        }

        for (int r = 0; r < recCount; ++r) {
            const Record& rec = df.records[i][r];
            for (int c = 0; c < fieldCount; ++c) {
                int w = tb.fields[c].size > 0 ? tb.fields[c].size : 1;
                std::string v = (c < (int)rec.values.size()) ? rec.values[c] : "";
                if ((int)v.size() > w) v = v.substr(0, w);   /* 超长截断 */
                std::string buf = v;
                buf.resize(w, ' ');                          /* 空格补齐 */
                fwrite(buf.data(), 1, w, fp);
            }
        }
    }
    fclose(fp);
    return true;
}

/* ================= .dat 中的表段辅助操作 ================= */

bool dropTableRecords(DataFile& df, const std::string& tableName) {
    int idx = df.find(tableName);
    if (idx < 0) return true;
    df.names.erase(df.names.begin() + idx);
    df.records.erase(df.records.begin() + idx);
    return true;
}

bool renameTableRecords(DataFile& df, const std::string& oldName,
                        const std::string& newName) {
    int idx = df.find(oldName);
    if (idx < 0) return true;
    df.names[idx] = newName;
    return true;
}

bool ensureTableSection(DataFile& df, const std::string& tableName) {
    if (df.find(tableName) >= 0) return true;
    df.names.push_back(tableName);
    df.records.push_back(std::vector<Record>());
    return true;
}

/* ================= 输出辅助 ================= */

void printQueryResult(const QueryResult& r) {
    size_t nCol = r.headers.size();
    if (nCol == 0) { std::cout << "(无字段)\n"; return; }

    std::vector<size_t> w(nCol, 0);
    for (size_t c = 0; c < nCol; ++c) w[c] = r.headers[c].size();
    for (size_t i = 0; i < r.rows.size(); ++i)
        for (size_t c = 0; c < nCol && c < r.rows[i].size(); ++c)
            if (r.rows[i][c].size() > w[c]) w[c] = r.rows[i][c].size();

    std::string line = "+";
    for (size_t c = 0; c < nCol; ++c) line += std::string(w[c] + 2, '-') + "+";
    std::cout << line << "\n|";
    for (size_t c = 0; c < nCol; ++c)
        std::cout << " " << std::left << std::setw((int)w[c]) << r.headers[c] << " |";
    std::cout << "\n" << line << "\n";
    for (size_t i = 0; i < r.rows.size(); ++i) {
        std::cout << "|";
        for (size_t c = 0; c < nCol; ++c) {
            std::string v = (c < r.rows[i].size()) ? r.rows[i][c] : "";
            std::cout << " " << std::left << std::setw((int)w[c]) << v << " |";
        }
        std::cout << "\n";
    }
    std::cout << line << "\n";
    std::cout << "共 " << r.rows.size() << " 条记录。\n";
}

std::string formatTable(const TableDef& t) {
    std::ostringstream os;
    os << "表名：" << t.name << "\n";
    os << "+-----+----------------+----------+------+-------+-------+-------+\n";
    os << "| 序号| 字段名         | 类型     | 字长 | KEY   | NULL  | 有效  |\n";
    os << "+-----+----------------+----------+------+-------+-------+-------+\n";
    for (size_t i = 0; i < t.fields.size(); ++i) {
        const FieldDef& f = t.fields[i];
        char buf[256];
        sprintf(buf, "| %-3d | %-14s | %-8s | %-4d | %-5s | %-5s | %-5s |",
                (int)i + 1, f.name.c_str(), f.type.c_str(), f.size,
                f.isKey ? "YES" : "NO", f.allowNull ? "YES" : "NO",
                f.valid ? "YES" : "NO");
        os << buf << "\n";
    }
    os << "+-----+----------------+----------+------+-------+-------+-------+\n";
    return os.str();
}

} /* namespace dbms */
