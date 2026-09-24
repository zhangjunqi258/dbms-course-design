/* ============================================================
 * main.cpp  —  交互界面（控制台菜单）
 * ------------------------------------------------------------
 * 所有菜单操作最终都通过 SQL 层（execSQL）完成，
 * 既演示了 SQL 解析执行，又保证界面与命令行行为一致。
 * ============================================================ */
#include "dbms.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

namespace dbms {

/* ==================== 输入辅助 ==================== */

static void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static string readLine(const string& prompt) {
    cout << prompt;
    cout.flush();
    string s;
    if (!getline(cin, s)) return "";
    return trimStr(s);
}

static int readInt(const string& prompt, int defVal) {
    string s = readLine(prompt);
    if (s.empty()) return defVal;
    std::istringstream is(s);
    int v;
    if (is >> v) return v;
    return defVal;
}

static string readYN(const string& prompt, const string& def) {
    string s = upperStr(readLine(prompt));
    if (s.empty()) return def;
    return s;
}

/* ==================== 通用显示 ==================== */

static bool needDatabase(const Database& db) {
    if (db.name.empty()) {
        cout << ">> 当前没有打开的数据库，请先选择菜单 1 新建/切换数据库。\n";
        return false;
    }
    return true;
}

static int pickTable(const Database& db) {
    if (db.tables.empty()) {
        cout << ">> 数据库 " << db.name << " 中还没有任何表。\n";
        return -1;
    }
    cout << "\n数据库 " << db.name << " 中的表：\n";
    for (size_t i = 0; i < db.tables.size(); ++i) {
        cout << "  " << (i + 1) << ". " << db.tables[i].name
             << "  (" << db.tables[i].fields.size() << " 个字段)\n";
    }
    int n = readInt("请选择表序号（0 取消）：", 0);
    if (n <= 0 || n > (int)db.tables.size()) return -1;
    return n - 1;
}

static void runSQL(Database& db, const string& sql, bool showResult = true,
                   QueryResult* qr = 0) {
    string out;
    cout << "\n[SQL] " << sql << "\n";
    bool ok = execSQL(db, sql, out, qr);
    if (!out.empty()) cout << (ok ? ">> " : "!! ") << out << "\n";
    if (ok && qr && showResult) printQueryResult(*qr);
    if (!ok) cout << ">> 语句执行失败。\n";
}

/* ==================== 1. 新建 / 切换数据库 ==================== */

void menuSwitchDatabase(Database& db) {
    cout << "\n--- 新建 / 切换数据库 ---\n";
    cout << "说明：数据库对应一对文件 <名字>.dbf（表结构）与 <名字>.dat（表记录）。\n";
    string name = readLine("请输入数据库名（不含扩展名，0 取消）：");
    if (name.empty() || name == "0") return;
    if (name.size() >= (size_t)FILE_NAME_LENGTH) {
        cout << ">> 数据库名过长（最多 14 个字符）。\n";
        return;
    }
    Database tmp;
    tmp.name = name;
    FILE* fp = fopen((name + ".dbf").c_str(), "rb");
    if (fp) {
        fclose(fp);
        db = tmp;                       /* 只设名字，下面统一 load */
        if (!loadDatabase(name, db)) { cout << ">> 读取数据库失败。\n"; db.name.clear(); return; }
        cout << ">> 已打开已存在的数据库 " << name << "，共有 "
             << db.tables.size() << " 张表。\n";
    } else {
        db = tmp;
        db.tables.clear();
        cout << ">> 数据库 " << name << " 尚不存在，已作为新建库打开；\n"
             << "   创建第一张表时自动生成 .dbf 文件。\n";
    }
}

/* ==================== 2. 建表 ==================== */

void menuCreateTable(Database& db) {
    cout << "\n--- 新建表（CREATE TABLE） ---\n";
    if (!needDatabase(db)) return;

    string tname = readLine("请输入表名（0 取消）：");
    if (tname.empty() || tname == "0") return;
    if (tname.size() >= (size_t)FILE_NAME_LENGTH) { cout << ">> 表名过长。\n"; return; }
    if (db.findTable(tname) >= 0) { cout << ">> 表 " << tname << " 已存在。\n"; return; }

    std::ostringstream sql;
    sql << "CREATE TABLE " << tname << " (\n";
    int cnt = 0;
    while (true) {
        cout << "\n-- 第 " << (cnt + 1) << " 个字段 --\n";
        string fn = readLine("  字段名：");
        if (fn.empty()) { cout << ">> 字段名不能为空，已结束输入。\n"; break; }
        if (fn.size() >= (size_t)FIELD_NAME_LENGTH) { cout << ">> 字段名过长（最多 14 个字符）。\n"; continue; }

        string type = upperStr(readLine("  类型（char/int/float/date，默认 char）："));
        if (type.empty()) type = "CHAR";
        int defSize = defaultSize(type);
        int size = readInt("  字长（回车用默认 " + to_string(defSize) + "）：", defSize);
        if (size <= 0) size = defSize;

        string key = readYN("  是否 KEY 键？(y/n，默认 n)：", "N");
        string nn = readYN("  是否允许为 NULL？(y/n，默认 n 即 NOT_NULL)：", "N");

        if (cnt > 0) sql << ",\n";
        sql << "  " << fn << " " << type << "[" << size << "] "
            << (key == "Y" ? "KEY" : "NOT_KEY") << " "
            << (nn == "Y" ? "NULL" : "NO_NULL") << " VALID";
        ++cnt;

        string more = readYN("继续添加字段？(y/n，默认 y)：", "Y");
        if (more != "Y") break;
    }
    if (cnt == 0) { cout << ">> 未定义任何字段，已取消建表。\n"; return; }
    sql << "\n) INTO " << db.name << ";";

    runSQL(db, sql.str());
}

/* ==================== 3. 查看表结构 ==================== */

void menuShowTables(Database& db) {
    cout << "\n--- 查看表结构 ---\n";
    if (!needDatabase(db)) return;
    if (db.tables.empty()) { cout << ">> 数据库 " << db.name << " 中还没有表。\n"; return; }

    cout << "数据库 " << db.name << " 中共有 " << db.tables.size() << " 张表：\n";
    for (size_t i = 0; i < db.tables.size(); ++i)
        cout << "  " << (i + 1) << ". " << db.tables[i].name << "\n";

    string s = readLine("请输入要查看的表名（回车显示全部表结构）：");
    if (s.empty()) {
        for (size_t i = 0; i < db.tables.size(); ++i)
            cout << "\n" << formatTable(db.tables[i]);
        return;
    }
    int ti = db.findTable(s);
    if (ti < 0) { cout << ">> 表 " << s << " 不存在。\n"; return; }
    cout << "\n" << formatTable(db.tables[ti]);
}

/* ==================== 4. 修改表结构 ==================== */

void menuAlterTable(Database& db) {
    cout << "\n--- 修改表结构 ---\n";
    if (!needDatabase(db)) return;

    cout << "  1. 增加新的字段\n";
    cout << "  2. 修改字段类型 / 字长 / KEY / NULL\n";
    cout << "  3. 修改字段名（RENAME）\n";
    cout << "  4. 删除字段（逻辑删除，置为 INVALID）\n";
    cout << "  5. 恢复被删除的字段\n";
    cout << "  6. 更改表名（RENAME TABLE）\n";
    cout << "  7. 删除表（DROP TABLE）\n";
    cout << "  0. 返回\n";

    int op = readInt("请选择：", 0);
    if (op == 0) return;

    int ti = pickTable(db);
    if (ti < 0) return;
    TableDef tb = db.tables[ti];

    std::ostringstream sql;
    if (op == 1 || op == 2 || op == 4 || op == 5) {
        string fn;
        if (op == 1 || op == 2) fn = readLine("字段名：");
        else {
            cout << "\n表 " << tb.name << " 的字段：\n";
            for (size_t i = 0; i < tb.fields.size(); ++i)
                cout << "  " << tb.fields[i].name << "  "
                     << tb.fields[i].type << "[" << tb.fields[i].size << "]  "
                     << (tb.fields[i].valid ? "有效" : "已删除") << "\n";
            fn = readLine("字段名：");
        }
        if (fn.empty()) { cout << ">> 未输入字段名，已取消。\n"; return; }

        int fi = tb.findField(fn);
        string type;
        int size = 0;
        if (op == 2 || op == 4 || op == 5) {
            if (fi < 0) { cout << ">> 字段 " << fn << " 不存在。\n"; return; }
            type = tb.fields[fi].type;
            size = tb.fields[fi].size;
        } else {
            string t = upperStr(readLine("类型（char/int/float/date，默认 char）："));
            type = t.empty() ? "CHAR" : t;
            int defSize = defaultSize(type);
            size = readInt("字长（回车用默认 " + to_string(defSize) + "）：", defSize);
            if (size <= 0) size = defSize;
        }
        string keyFlag = "NOT_KEY", nullFlag = "NO_NULL", validFlag = "VALID";
        if (op == 2) {
            keyFlag = readYN("是否 KEY 键？(y/n，默认 n)：", "N") == "Y" ? "KEY" : "NOT_KEY";
            nullFlag = readYN("是否允许 NULL？(y/n，默认 n)：", "N") == "Y" ? "NULL" : "NO_NULL";
        } else if (op == 4) { validFlag = "INVALID"; }
        else if (op == 5) { validFlag = "VALID"; }

        sql << "EDIT TABLE " << tb.name << " ( " << fn << " " << type
            << "[" << size << "] " << keyFlag << " " << nullFlag << " "
            << validFlag << " ) IN " << db.name << ";";
    } else if (op == 3) {
        string o = readLine("原字段名：");
        string n = readLine("新字段名：");
        if (o.empty() || n.empty()) { cout << ">> 已取消。\n"; return; }
        sql << "EDIT TABLE " << tb.name << " ( " << o << " RENAME TO " << n
            << " ) IN " << db.name << ";";
    } else if (op == 6) {
        string n = readLine("新表名：");
        if (n.empty()) { cout << ">> 已取消。\n"; return; }
        sql << "RENAME TABLE " << tb.name << " " << n << " IN " << db.name << ";";
    } else if (op == 7) {
        string c = readYN("确认删除表 " + tb.name + " 及其全部记录？(y/n)：", "N");
        if (c != "Y") { cout << ">> 已取消。\n"; return; }
        sql << "DROP TABLE " << tb.name << " IN " << db.name << ";";
    } else {
        cout << ">> 无效的选择。\n";
        return;
    }

    runSQL(db, sql.str());
}

/* ==================== 5. 插入记录 ==================== */

void menuInsertRecord(Database& db) {
    cout << "\n--- 插入记录（INSERT） ---\n";
    if (!needDatabase(db)) return;
    int ti = pickTable(db);
    if (ti < 0) return;
    const TableDef& tb = db.tables[ti];

    cout << "\n" << formatTable(tb);
    cout << "请输入各字段的值（直接回车表示空值 NULL）：\n";

    std::ostringstream vals;
    bool first = true;
    for (size_t i = 0; i < tb.fields.size(); ++i) {
        const FieldDef& f = tb.fields[i];
        if (!f.valid) continue;
        std::ostringstream p;
        p << "  " << f.name << " (" << f.type << "[" << f.size << "]"
          << (f.isKey ? ",KEY" : "") << (f.allowNull ? ",NULL" : ",NOT NULL") << ")：";
        string v = readLine(p.str());
        if (!first) vals << ", ";
        vals << "'" << v << "'";
        first = false;
    }

    std::ostringstream sql;
    sql << "INSERT INTO " << tb.name << " VALUES ( " << vals.str()
        << " ) IN " << db.name << ";";
    runSQL(db, sql.str());
}

/* ==================== 6. 查看记录 ==================== */

void menuViewRecords(Database& db) {
    cout << "\n--- 查看记录（SELECT） ---\n";
    if (!needDatabase(db)) return;
    int ti = pickTable(db);
    if (ti < 0) return;
    const TableDef& tb = db.tables[ti];

    cout << "\n  1. 查看全部记录\n";
    cout << "  2. 带条件查询 / 自定义 SELECT\n";
    int op = readInt("请选择（默认 1）：", 1);

    if (op == 2) {
        cout << "示例：SELECT * FROM " << tb.name << " WHERE Age > 20\n";
        string sql = readLine("请输入 SQL：");
        if (sql.empty()) return;
        QueryResult qr;
        runSQL(db, sql, true, &qr);
        return;
    }
    QueryResult qr;
    runSQL(db, "SELECT * FROM " + tb.name + " IN " + db.name + ";", false, &qr);
    printQueryResult(qr);
}

/* ==================== 7. 删除记录 ==================== */

void menuDeleteRecord(Database& db) {
    cout << "\n--- 删除记录（DELETE） ---\n";
    if (!needDatabase(db)) return;
    int ti = pickTable(db);
    if (ti < 0) return;
    const TableDef& tb = db.tables[ti];

    cout << "\n表 " << tb.name << " 的字段：\n";
    for (size_t i = 0; i < tb.fields.size(); ++i)
        if (tb.fields[i].valid)
            cout << "  " << tb.fields[i].name << "\n";

    cout << "提示：不输入条件将删除全部记录。\n";
    string field = readLine("条件字段名（回车=全部删除）：");
    std::ostringstream sql;
    sql << "DELETE FROM " << tb.name;
    if (!field.empty()) {
        string op = readLine("比较运算符（= <> > < >= <=，默认 =）：");
        if (op.empty()) op = "=";
        string val = readLine("值：");
        sql << " WHERE " << field << " " << op << " '" << val << "'";
    }
    sql << " IN " << db.name << ";";

    if (field.empty()) {
        string c = readYN("确认删除表 " + tb.name + " 的全部记录？(y/n)：", "N");
        if (c != "Y") { cout << ">> 已取消。\n"; return; }
    }
    runSQL(db, sql.str());
}

/* ==================== 8. 修改记录 ==================== */

void menuUpdateRecord(Database& db) {
    cout << "\n--- 修改记录（UPDATE） ---\n";
    if (!needDatabase(db)) return;
    int ti = pickTable(db);
    if (ti < 0) return;
    const TableDef& tb = db.tables[ti];

    cout << "\n要修改哪个字段？表 " << tb.name << " 的字段：\n";
    for (size_t i = 0; i < tb.fields.size(); ++i)
        if (tb.fields[i].valid)
            cout << "  " << tb.fields[i].name << "\n";
    string field = readLine("字段名：");
    if (field.empty()) { cout << ">> 已取消。\n"; return; }
    string value = readLine("新值：");

    cout << "修改哪些记录？（WHERE 条件）\n";
    string cf = readLine("条件字段名（回车=修改全部记录）：");
    std::ostringstream sql;
    sql << "UPDATE " << tb.name << " ( SET " << field << " = '" << value << "'";
    if (!cf.empty()) {
        string op = readLine("比较运算符（= <> > < >= <=，默认 =）：");
        if (op.empty()) op = "=";
        string cv = readLine("条件值：");
        sql << " WHERE " << cf << " " << op << " '" << cv << "'";
    }
    sql << " ) IN " << db.name << ";";

    runSQL(db, sql.str());
}

/* ==================== 9. 直接执行 SQL ==================== */

static void menuRawSQL(Database& db) {
    cout << "\n--- 执行 SQL 语句 ---\n";
    cout << "示例：\n";
    cout << "  CREATE TABLE Student ( Sno char[10] KEY NO_NULL VALID, Name char[10] NOT_KEY NO_NULL VALID, Age int NOT_KEY NULL VALID ) INTO MyDB;\n";
    cout << "  INSERT INTO Student VALUES ( '1001', 'ZhangSan', '20' ) IN MyDB;\n";
    cout << "  SELECT Sno, Name FROM Student WHERE Age > 18 IN MyDB;\n";
    cout << "  UPDATE Student ( SET Age = '21' WHERE Sno = '1001' ) IN MyDB;\n";
    cout << "  DELETE FROM Student WHERE Sno = '1001' IN MyDB;\n";
    cout << "输入 end 结束。\n";

    string line;
    while (true) {
        cout << "\nsql> ";
        cout.flush();
        if (!getline(cin, line)) break;
        line = trimStr(line);
        if (line.empty()) continue;
        if (upperStr(line) == "END" || upperStr(line) == "EXIT") break;
        if (upperStr(line) == "EXIT;") break;

        QueryResult qr;
        string out;
        bool ok = execSQL(db, line, out, &qr);
        if (!out.empty()) cout << (ok ? ">> " : "!! ") << out << "\n";
        if (ok && !qr.headers.empty()) printQueryResult(qr);
    }
}

/* ==================== 主菜单 ==================== */

static void printBanner(const Database& db) {
    cout << "\n";
    cout << "============================================================\n";
    cout << "        简易数据库管理系统 (DBMS)  —— 课程设计作品\n";
    cout << "        华东理工大学  数据库原理课程设计\n";
    cout << "============================================================\n";
    cout << "  当前数据库：" << (db.name.empty() ? string("(未打开)") : db.name)
         << "    表数量：" << db.tables.size() << "\n";
    cout << "------------------------------------------------------------\n";
    cout << "   1. 新建 / 切换数据库\n";
    cout << "   2. 建表（CREATE TABLE）\n";
    cout << "   3. 查看表结构（SHOW / DESCRIBE）\n";
    cout << "   4. 修改表结构（增删改字段 / 改表名 / 删表）\n";
    cout << "   5. 插入记录（INSERT）\n";
    cout << "   6. 查看记录 / 查询（SELECT）\n";
    cout << "   7. 删除记录（DELETE）\n";
    cout << "   8. 修改记录（UPDATE）\n";
    cout << "   9. 直接执行 SQL 语句\n";
    cout << "   0. 退出\n";
    cout << "------------------------------------------------------------\n";
}

} /* namespace dbms */

using namespace dbms;

int main() {
    initConsole();
    Database db;

    /* 若默认数据库存在则自动打开 */
    {
        FILE* fp = fopen("MyDB.dbf", "rb");
        if (fp) { fclose(fp); loadDatabase("MyDB", db); }
    }

    while (true) {
        printBanner(db);
        int op = readInt("请选择操作：", -1);

        switch (op) {
            case 1: menuSwitchDatabase(db); break;
            case 2: menuCreateTable(db); break;
            case 3: menuShowTables(db); break;
            case 4: menuAlterTable(db); break;
            case 5: menuInsertRecord(db); break;
            case 6: menuViewRecords(db); break;
            case 7: menuDeleteRecord(db); break;
            case 8: menuUpdateRecord(db); break;
            case 9: menuRawSQL(db); break;
            case 0:
                cout << "\n再见！\n";
                return 0;
            default:
                cout << "\n>> 无效的选择，请重新输入。\n";
        }
    }
    return 0;
}
