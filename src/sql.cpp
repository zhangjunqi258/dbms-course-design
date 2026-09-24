/* ============================================================
 * sql.cpp  —  SQL 层实现
 * ------------------------------------------------------------
 * 支持的语句（与课程设计要求一致）：
 *   DDL:
 *     CREATE TABLE T ( F Type KEY NULL VALID, ... ) INTO DB;
 *     EDIT   TABLE T ( F Type KEY NULL VALID  [或 F RENAME TO G] ) IN DB;
 *     RENAME TABLE T1 T2 IN DB;
 *     DROP   TABLE T IN DB;
 *   DML:
 *     INSERT INTO T VALUES ( v1, v2, ... ) IN DB;
 *     DELETE FROM T WHERE F = v [AND/OR ...] IN DB;
 *     UPDATE T ( SET F1 = v1, F2 = v2 WHERE F = v ) IN DB;
 *   QUERY:
 *     SELECT * | f1, f2 | T.f FROM T1, T2 [WHERE 条件];
 *
 * 说明：
 *   - 支持半角与全角括号/逗号/等号；
 *   - 条件支持 = <> != > < >= <=，多条件用 AND / OR 连接（从左到右）；
 *   - 数值比较：当比较双方都能识别为数字时按数值比较，否则按字符串比较。
 * ============================================================ */
#include "dbms.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <iostream>

namespace dbms {

/* ==================== 词法分析 ==================== */

/* 全角字符统一成半角 */
static void normalize(std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0xEF && i + 2 < s.size()) {         /* UTF-8 全角符号 */
            unsigned int cp = ((c & 0x0F) << 12) |
                              (((unsigned char)s[i + 1] & 0x3F) << 6) |
                              ((unsigned char)s[i + 2] & 0x3F);
            i += 3;
            switch (cp) {
                case 0xFF08: r += '('; continue;      /* （ */
                case 0xFF09: r += ')'; continue;      /* ） */
                case 0xFF0C: r += ','; continue;      /* ， */
                case 0xFF1B: r += ';'; continue;      /* ； */
                case 0xFF1D: r += '='; continue;      /* ＝ */
                case 0x3000: r += ' '; continue;      /* 全角空格 */
                case 0xFF1E: r += '>'; continue;      /* ＞ */
                case 0xFF1C: r += '<'; continue;      /* ＜ */
                default: break;
            }
            r += (char)c; r += s[i - 2]; r += s[i - 1];
            continue;
        }
        r += (char)c;
        ++i;
    }
    s = r;
}

static std::vector<std::string> tokenize(const std::string& sql) {
    std::string s = sql;
    normalize(s);
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        if (c == '(' || c == ')' || c == ',' || c == ';') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            out.push_back(std::string(1, c));
            continue;
        }
        if (c == '\'' || c == '"') {              /* 字符串常量 */
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            char q = c;
            std::string lit;
            ++i;
            for (; i < s.size() && s[i] != q; ++i) lit += s[i];
            out.push_back(lit);
            continue;
        }
        if (c == '=' || c == '<' || c == '>' || c == '!') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            char nxt = (i + 1 < s.size()) ? s[i + 1] : 0;
            if ((c == '<' && nxt == '>') || (c == '!' && nxt == '=')) {
                out.push_back("<>"); ++i; continue;
            }
            if ((c == '<' && nxt == '=') || (c == '>' && nxt == '=')) {
                out.push_back(std::string(1, c) + nxt); ++i; continue;
            }
            out.push_back(std::string(1, c));
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/* ==================== 条件表达式 ==================== */

struct Cond {
    std::string left;
    std::string op;
    std::string right;
    bool rightIsField;
    std::string logic;      /* 与前一个条件的关系：AND / OR */
};

/* 从 tokens 中解析条件列表，遇到 IN/INTO/')' 或结束即停止 */
static bool parseConds(const std::vector<std::string>& t, size_t& p,
                       std::vector<Cond>& conds, std::string& err) {
    while (p < t.size()) {
        std::string up = upperStr(t[p]);
        if (up == "IN" || up == "INTO" || t[p] == ")") break;
        Cond c;
        c.logic = conds.empty() ? "AND" : "AND";
        if (up == "AND" || up == "OR") {
            if (conds.empty()) { err = "条件表达式不能以 AND/OR 开头"; return false; }
            c.logic = up;
            ++p;
            if (p >= t.size()) { err = "AND/OR 后缺少条件"; return false; }
            up = upperStr(t[p]);
        }
        if (p + 2 >= t.size()) { err = "条件表达式不完整"; return false; }
        c.left = t[p++];
        c.op = t[p++];
        if (c.op != "=" && c.op != "<>" && c.op != ">" && c.op != "<" &&
            c.op != ">=" && c.op != "<=") {
            err = "不支持的比较运算符：" + c.op;
            return false;
        }
        c.right = t[p++];
        std::string rup = upperStr(c.right);
        /* 能作为字段名解析的当字段；数字和 NULL 当字面量 */
        if (isNumber(c.right) || rup == "NULL") c.rightIsField = false;
        else c.rightIsField = true;   /* 解析失败时再退化为字符串常量 */
        conds.push_back(c);
    }
    return true;
}

/* ==================== 字段定义解析 ==================== */

struct FieldSpec {
    FieldDef f;
    bool rename;              /* 是否为“改字段名”操作 */
    std::string newName;
    FieldSpec() : rename(false) {}
};

static bool parseTypeSize(const std::string& raw, std::string& typeName, int& size) {
    std::string s = raw;
    size = 0;
    size_t b = s.find('[');
    if (b == std::string::npos) b = s.find('(');
    if (b != std::string::npos) {
        size_t e = s.find(']', b);
        if (e == std::string::npos) e = s.find(')', b);
        if (e == std::string::npos) return false;
        std::string num = s.substr(b + 1, e - b - 1);
        size = atoi(num.c_str());
        if (size <= 0) return false;
        s = s.substr(0, b);
    }
    typeName = s;
    if (typeName.empty()) return false;
    if (size <= 0) size = defaultSize(typeName);
    return true;
}

/* 解析一个字段定义：名字 类型[字长] 标志位... */
static bool parseFieldSpec(const std::vector<std::string>& t, size_t& p,
                           FieldSpec& spec, std::string& err) {
    if (p >= t.size()) { err = "字段定义不完整"; return false; }
    spec.f.name = t[p++];

    if (p < t.size() && upperStr(t[p]) == "RENAME") {
        ++p;
        if (p >= t.size() || upperStr(t[p]) != "TO") { err = "RENAME 之后应为 TO 新字段名"; return false; }
        ++p;
        if (p >= t.size()) { err = "RENAME TO 之后缺少新字段名"; return false; }
        spec.newName = t[p++];
        spec.rename = true;
        return true;
    }

    if (p >= t.size() || t[p] == "," || t[p] == ")") { err = "字段 " + spec.f.name + " 缺少类型"; return false; }
    if (!parseTypeSize(t[p], spec.f.type, spec.f.size)) {
        err = "字段类型写法错误：" + t[p];
        return false;
    }
    ++p;

    spec.f.isKey = false;
    spec.f.allowNull = false;
    spec.f.valid = true;
    while (p < t.size() && t[p] != "," && t[p] != ")") {
        std::string up = upperStr(t[p]);
        if (up == "KEY" || up == "PRIMARY" || up == "PRIMARY_KEY" || up == "PRIMARYKEY") {
            spec.f.isKey = true;
        } else if (up == "NOT_KEY" || up == "NO_KEY" || up == "NOTKEY" || up == "NONKEY") {
            spec.f.isKey = false;
        } else if (up == "NULL") {
            spec.f.allowNull = true;
        } else if (up == "NOT_NULL" || up == "NO_NULL" || up == "NOTNULL" || up == "NONULL") {
            spec.f.allowNull = false;
        } else if (up == "VALID") {
            spec.f.valid = true;
        } else if (up == "INVALID" || up == "NOT_VALID" || up == "NOVALID") {
            spec.f.valid = false;
        } else {
            err = "无法识别的字段标志位：" + t[p];
            return false;
        }
        ++p;
    }
    return true;
}

/* ==================== 数据库切换 ==================== */

static bool useDb(Database& db, const std::string& name, bool createIfMissing,
                  std::string& err) {
    if (name.empty()) {
        if (db.name.empty()) { err = "当前没有打开的数据库，请先建库或切换库"; return false; }
        return true;
    }
    if (name.size() >= (size_t)FILE_NAME_LENGTH) { err = "数据库名过长（最多14个字符）"; return false; }
    if (!db.name.empty() && iequals(db.name, name)) return true;

    Database tmp;
    if (loadDatabase(name, tmp)) { db = tmp; return true; }
    if (createIfMissing) { db.name = name; db.tables.clear(); return true; }
    err = "数据库 " + name + " 不存在";
    return false;
}

/* 从 tokens 的当前位置读取 IN/INTO 后的数据库名（没有则返回 ""） */
static std::string readDbName(const std::vector<std::string>& t, size_t& p) {
    if (p < t.size()) {
        std::string up = upperStr(t[p]);
        if (up == "IN" || up == "INTO") {
            ++p;
            if (p < t.size()) return t[p++];
        }
    }
    return "";
}

static bool checkName(const std::string& n, const char* what, std::string& err) {
    if (n.empty()) { err = std::string(what) + "不能为空"; return false; }
    if (n.size() >= (size_t)FILE_NAME_LENGTH) { err = std::string(what) + "过长（最多14个字符）"; return false; }
    return true;
}

/* ==================== 取值与比较 ==================== */

struct Ctx {
    std::vector<const TableDef*> tabs;
    std::vector<const Record*>   rows;
};

/* 把名字解析为 (表下标, 字段下标)；带 '.' 的是限定名 */
static bool resolveName(const Ctx& ctx, const std::string& name,
                        int& ti, int& fi, std::string& err) {
    size_t dot = name.find('.');
    if (dot != std::string::npos) {
        std::string tn = name.substr(0, dot);
        std::string fn = name.substr(dot + 1);
        ti = -1;
        for (size_t i = 0; i < ctx.tabs.size(); ++i)
            if (iequals(ctx.tabs[i]->name, tn)) { ti = (int)i; break; }
        if (ti < 0) { err = "表 " + tn + " 不在本次查询的 FROM 列表中"; return false; }
        fi = ctx.tabs[ti]->findField(fn);
        if (fi < 0) { err = "表 " + tn + " 中不存在字段 " + fn; return false; }
        return true;
    }
    int found = -1, foundTi = -1;
    for (size_t i = 0; i < ctx.tabs.size(); ++i) {
        int k = ctx.tabs[i]->findField(name);
        if (k >= 0) {
            if (found >= 0) { err = "字段名 " + name + " 在多张表中出现，请使用 表名.字段名"; return false; }
            found = k; foundTi = (int)i;
        }
    }
    if (found < 0) { err = "不存在字段 " + name; return false; }
    ti = foundTi; fi = found;
    return true;
}

static const std::string& cell(const Ctx& ctx, int ti, int fi) {
    static const std::string empty = "";
    if (ti < 0 || ti >= (int)ctx.rows.size() || ctx.rows[ti] == 0) return empty;
    const Record& r = *ctx.rows[ti];
    if (fi < 0 || fi >= (int)r.values.size()) return empty;
    return r.values[fi];
}

/* 只检查条件里引用的字段是否存在（用于空表时也能报错） */
static bool validateConds(const Ctx& ctx, const std::vector<Cond>& conds,
                          std::string& err) {
    for (size_t i = 0; i < conds.size(); ++i) {
        int a, b;
        if (!resolveName(ctx, conds[i].left, a, b, err)) return false;
        if (conds[i].rightIsField) {
            std::string e2;
            int c2, d2;
            if (resolveName(ctx, conds[i].right, c2, d2, e2)) continue;
            /* 解析不了则当作字符串常量，属正常情况 */
        }
    }
    return true;
}

static bool cmpValues(const std::string& a, const std::string& b,
                      const std::string& op, bool& res) {
    if (a.empty() || b.empty()) {         /* 含空值时比较结果为假 */
        res = false;
        return true;
    }
    int c;
    if (isNumber(a) && isNumber(b)) {
        double x = atof(a.c_str()), y = atof(b.c_str());
        c = (x < y) ? -1 : (x > y ? 1 : 0);
    } else {
        c = a.compare(b);
        c = (c < 0) ? -1 : (c > 0 ? 1 : 0);
    }
    if (op == "=")  res = (c == 0);
    else if (op == "<>") res = (c != 0);
    else if (op == ">")  res = (c > 0);
    else if (op == "<")  res = (c < 0);
    else if (op == ">=") res = (c >= 0);
    else if (op == "<=") res = (c <= 0);
    else return false;
    return true;
}

static bool evalConds(const Ctx& ctx, const std::vector<Cond>& conds,
                      std::string& err) {
    bool total = true;
    for (size_t i = 0; i < conds.size(); ++i) {
        const Cond& c = conds[i];
        int lt, lf;
        if (!resolveName(ctx, c.left, lt, lf, err)) return false;
        std::string lv = cell(ctx, lt, lf);

        std::string rv;
        if (c.rightIsField) {
            int rt, rf;
            std::string e2;
            if (!resolveName(ctx, c.right, rt, rf, e2)) {
                rv = c.right;              /* 不是字段名 → 当字符串常量 */
            } else {
                rv = cell(ctx, rt, rf);
            }
        } else {
            rv = c.right;
        }

        bool r = false;
        if (!cmpValues(lv, rv, c.op, r)) { err = "无法比较"; return false; }
        if (i == 0) total = r;
        else if (c.logic == "OR") total = total || r;
        else total = total && r;
    }
    return total;
}

/* ==================== 值的检查 ==================== */

static bool checkValue(const FieldDef& f, const std::string& v, std::string& err) {
    if (v.empty()) {
        if (!f.allowNull) { err = "字段 " + f.name + " 不允许为空"; return false; }
        return true;
    }
    std::string t = upperStr(f.type);
    if (t == "INT" || t == "INTEGER") {
        if (!isNumber(v) || v.find('.') != std::string::npos) {
            err = "字段 " + f.name + " 需要整数，实际为 " + v; return false;
        }
    } else if (t == "FLOAT" || t == "DOUBLE" || t == "REAL") {
        if (!isNumber(v)) { err = "字段 " + f.name + " 需要数值，实际为 " + v; return false; }
    }
    if ((int)v.size() > f.size) {
        std::ostringstream os;
        os << "字段 " << f.name << " 的值 \"" << v << "\" 长度 " << v.size()
           << " 超出字长 " << f.size;
        err = os.str();
        return false;
    }
    return true;
}

/* ==================== 各语句的执行 ==================== */

static bool execCreate(Database& db, const std::vector<std::string>& t,
                       size_t& p, std::string& out) {
    if (p >= t.size() || upperStr(t[p]) != "TABLE") return false;
    ++p;
    if (p >= t.size()) { out = "缺少表名"; return false; }
    std::string tname = t[p++];
    std::string err;
    if (!checkName(tname, "表名", err)) { out = err; return false; }

    if (p >= t.size() || t[p] != "(") { out = "CREATE TABLE 缺少 ( "; return false; }
    ++p;

    TableDef tb;
    tb.name = tname;
    while (p < t.size() && t[p] != ")") {
        FieldSpec spec;
        if (!parseFieldSpec(t, p, spec, err)) { out = err; return false; }
        if (spec.rename) { out = "CREATE TABLE 中不支持 RENAME"; return false; }
        for (size_t i = 0; i < tb.fields.size(); ++i)
            if (iequals(tb.fields[i].name, spec.f.name)) {
                out = "字段名重复：" + spec.f.name;
                return false;
            }
        if ((int)tb.fields.size() >= MAX_FIELDS) { out = "字段数量超过上限"; return false; }
        tb.fields.push_back(spec.f);
        if (p < t.size() && t[p] == ",") ++p;
    }
    if (p >= t.size() || t[p] != ")") { out = "CREATE TABLE 缺少 ) "; return false; }
    ++p;

    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, true, err)) { out = err; return false; }
    if (db.findTable(tname) >= 0) { out = "表 " + tname + " 已存在"; return false; }
    if (tb.fields.empty()) { out = "表至少需要一个字段"; return false; }

    db.tables.push_back(tb);
    if (!saveDatabase(db, db.name)) { out = "写入数据库文件失败"; return false; }

    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }
    ensureTableSection(df, tname);
    if (!saveDataFile(db.name, db, df)) { out = "写入数据文件失败"; return false; }

    std::ostringstream os;
    os << "成功创建表 " << tname << "（" << tb.fields.size() << " 个字段），"
       << "数据库 " << db.name << "。\n" << formatTable(tb);
    out = os.str();
    return true;
}

static bool execEdit(Database& db, const std::vector<std::string>& t,
                     size_t& p, std::string& out) {
    if (p >= t.size() || upperStr(t[p]) != "TABLE") return false;
    ++p;
    if (p >= t.size()) { out = "缺少表名"; return false; }
    std::string tname = t[p++];

    bool hasParen = (p < t.size() && t[p] == "(");
    if (hasParen) ++p;

    FieldSpec spec;
    std::string err;
    if (!parseFieldSpec(t, p, spec, err)) { out = err; return false; }
    if (hasParen) {
        if (p >= t.size() || t[p] != ")") { out = "EDIT TABLE 缺少 ) "; return false; }
        ++p;
    }

    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    int ti = db.findTable(tname);
    if (ti < 0) { out = "表 " + tname + " 不存在"; return false; }
    TableDef& tb = db.tables[ti];

    /* --- 改字段名 --- */
    if (spec.rename) {
        int fi = tb.findField(spec.f.name);
        if (fi < 0) { out = "字段 " + spec.f.name + " 不存在"; return false; }
        if (tb.findField(spec.newName) >= 0) { out = "字段 " + spec.newName + " 已存在"; return false; }
        if ((int)spec.newName.size() >= FIELD_NAME_LENGTH) { out = "新字段名过长"; return false; }
        tb.fields[fi].name = spec.newName;
        if (!saveDatabase(db, db.name)) { out = "写入数据库文件失败"; return false; }
        out = "已将表 " + tname + " 的字段 " + spec.f.name + " 改名为 " + spec.newName + "。";
        return true;
    }

    int fi = tb.findField(spec.f.name);
    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }

    if (fi >= 0) {
        /* 修改已有字段：类型/字长不变时只改标志位；字长变化时保持原值 */
        bool sizeChanged = (tb.fields[fi].size != spec.f.size) ||
                           (!iequals(tb.fields[fi].type, spec.f.type));
        tb.fields[fi].type = spec.f.type;
        tb.fields[fi].size = spec.f.size;
        tb.fields[fi].isKey = spec.f.isKey;
        tb.fields[fi].allowNull = spec.f.allowNull;
        bool wasValid = tb.fields[fi].valid;
        tb.fields[fi].valid = spec.f.valid;

        if (!saveDatabase(db, db.name)) { out = "写入数据库文件失败"; return false; }
        /* 字长变化时把所有记录的值重新裁到新字长 */
        if (sizeChanged) {
            int si = df.find(tname);
            if (si >= 0) {
                int w = tb.fields[fi].size;
                for (size_t r = 0; r < df.records[si].size(); ++r) {
                    std::string& v = df.records[si][r].values[fi];
                    if ((int)v.size() > w) v = v.substr(0, w);
                }
                saveDataFile(db.name, db, df);
            }
        }
        std::ostringstream os;
        os << "已修改表 " << tname << " 的字段 " << spec.f.name << "（字长 " << spec.f.size;
        if (!wasValid && !spec.f.valid) os << "，已标记为无效";
        else if (wasValid && !spec.f.valid) os << "，已标记为无效（逻辑删除）";
        else if (!wasValid && spec.f.valid) os << "，已恢复为有效";
        os << "）。";
        out = os.str();
        return true;
    }

    /* 新增字段 */
    if ((int)tb.fields.size() >= MAX_FIELDS) { out = "字段数量超过上限"; return false; }
    tb.fields.push_back(spec.f);
    if (!saveDatabase(db, db.name)) { out = "写入数据库文件失败"; return false; }

    int si = df.find(tname);
    if (si >= 0) {
        for (size_t r = 0; r < df.records[si].size(); ++r)
            df.records[si][r].values.push_back("");
        saveDataFile(db.name, db, df);
    }
    out = "已为表 " + tname + " 增加字段 " + spec.f.name + "。";
    return true;
}

static bool execRenameTable(Database& db, const std::vector<std::string>& t,
                            size_t& p, std::string& out) {
    if (p >= t.size() || upperStr(t[p]) != "TABLE") return false;
    ++p;
    if (p + 1 >= t.size()) { out = "RENAME TABLE 需要 旧表名 新表名"; return false; }
    std::string o = t[p++], n = t[p++];
    std::string err;
    if (!checkName(n, "新表名", err)) { out = err; return false; }

    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    int ti = db.findTable(o);
    if (ti < 0) { out = "表 " + o + " 不存在"; return false; }
    if (db.findTable(n) >= 0) { out = "表 " + n + " 已存在"; return false; }

    /* 注意：必须先按“旧表名”读入 .dat，再修改 .dbf 中的表名 */
    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }

    db.tables[ti].name = n;
    if (!saveDatabase(db, db.name)) { out = "写入数据库文件失败"; return false; }

    renameTableRecords(df, o, n);
    if (!saveDataFile(db.name, db, df)) { out = "写入数据文件失败"; return false; }

    out = "已将表 " + o + " 改名为 " + n + "。";
    return true;
}

static bool execDropTable(Database& db, const std::vector<std::string>& t,
                          size_t& p, std::string& out) {
    if (p >= t.size() || upperStr(t[p]) != "TABLE") return false;
    ++p;
    if (p >= t.size()) { out = "缺少表名"; return false; }
    std::string tname = t[p++];
    std::string err;
    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    int ti = db.findTable(tname);
    if (ti < 0) { out = "表 " + tname + " 不存在"; return false; }

    /* 注意：必须先按表名读入 .dat，再从 .dbf 中删除该表 */
    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }

    db.tables.erase(db.tables.begin() + ti);
    if (!saveDatabase(db, db.name)) { out = "写入数据库文件失败"; return false; }

    dropTableRecords(df, tname);
    if (!saveDataFile(db.name, db, df)) { out = "写入数据文件失败"; return false; }

    out = "已删除表 " + tname + "。";
    return true;
}

static bool execInsert(Database& db, const std::vector<std::string>& t,
                       size_t& p, std::string& out) {
    if (p >= t.size() || upperStr(t[p]) != "INTO") return false;
    ++p;
    if (p >= t.size()) { out = "缺少表名"; return false; }
    std::string tname = t[p++];

    if (p >= t.size() || upperStr(t[p]) != "VALUES") { out = "INSERT 需要 VALUES 子句"; return false; }
    ++p;
    if (p >= t.size() || t[p] != "(") { out = "INSERT 缺少 ( "; return false; }
    ++p;

    std::vector<std::string> vals;
    while (p < t.size() && t[p] != ")") {
        std::string v = t[p++];
        if (upperStr(v) == "NULL") v = "";
        vals.push_back(v);
        if (p < t.size() && t[p] == ",") ++p;
        else break;
    }
    if (p >= t.size() || t[p] != ")") { out = "INSERT 缺少 ) "; return false; }
    ++p;

    std::string err;
    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    int ti = db.findTable(tname);
    if (ti < 0) { out = "表 " + tname + " 不存在"; return false; }
    const TableDef& tb = db.tables[ti];

    /* 只对有效字段赋值 */
    std::vector<int> validIdx;
    for (size_t i = 0; i < tb.fields.size(); ++i)
        if (tb.fields[i].valid) validIdx.push_back((int)i);
    if (vals.size() != validIdx.size()) {
        std::ostringstream os;
        os << "VALUES 的值个数(" << vals.size() << ")与表 " << tname
           << " 的有效字段个数(" << validIdx.size() << ")不一致";
        out = os.str();
        return false;
    }

    Record rec;
    rec.values.assign(tb.fields.size(), "");
    for (size_t i = 0; i < validIdx.size(); ++i) {
        const FieldDef& f = tb.fields[validIdx[i]];
        if (!checkValue(f, vals[i], err)) { out = err; return false; }
        rec.values[validIdx[i]] = vals[i];
    }

    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }
    ensureTableSection(df, tname);
    int si = df.find(tname);

    /* 主键（KEY）唯一性检查 */
    int ki = tb.keyIndex();
    if (ki >= 0 && !rec.values[ki].empty()) {
        for (size_t r = 0; r < df.records[si].size(); ++r) {
            if (!df.records[si][r].valid) continue;
            if (df.records[si][r].values[ki] == rec.values[ki]) {
                out = "违反 KEY 约束：字段 " + tb.fields[ki].name +
                      " 的值 " + rec.values[ki] + " 已存在";
                return false;
            }
        }
    }

    df.records[si].push_back(rec);
    if (!saveDataFile(db.name, db, df)) { out = "写入数据文件失败"; return false; }

    std::ostringstream os;
    os << "成功向表 " << tname << " 插入 1 条记录，当前共 "
       << df.records[si].size() << " 条（含已删除）。";
    out = os.str();
    return true;
}

static bool execDelete(Database& db, const std::vector<std::string>& t,
                       size_t& p, std::string& out) {
    if (p >= t.size() || upperStr(t[p]) != "FROM") return false;
    ++p;
    if (p >= t.size()) { out = "缺少表名"; return false; }
    std::string tname = t[p++];

    std::vector<Cond> conds;
    std::string err;
    if (p < t.size() && upperStr(t[p]) == "WHERE") {
        ++p;
        if (!parseConds(t, p, conds, err)) { out = err; return false; }
    }
    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    int ti = db.findTable(tname);
    if (ti < 0) { out = "表 " + tname + " 不存在"; return false; }
    const TableDef& tb = db.tables[ti];

    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }
    int si = df.find(tname);
    if (si < 0) { out = "表 " + tname + " 中暂无记录。"; return true; }
    if (!conds.empty()) {
        Ctx probe;
        probe.tabs.push_back(&tb);
        std::vector<const Record*> nr(1, (const Record*)0);
        probe.rows = nr;
        if (!validateConds(probe, conds, err)) { out = err; return false; }
    }

    int cnt = 0;
    for (size_t r = 0; r < df.records[si].size(); ++r) {
        if (!df.records[si][r].valid) continue;
        Ctx ctx;
        ctx.tabs.push_back(&tb);
        ctx.rows.push_back(&df.records[si][r]);
        std::string e2;
        bool match = conds.empty() ? true : evalConds(ctx, conds, e2);
        if (!match) {
            if (!e2.empty()) { out = e2; return false; }
            continue;
        }
        df.records[si][r].valid = false;      /* 逻辑删除 */
        ++cnt;
    }
    if (!saveDataFile(db.name, db, df)) { out = "写入数据文件失败"; return false; }

    std::ostringstream os;
    os << "已删除表 " << tname << " 中 " << cnt << " 条记录（逻辑删除，数据仍保留在文件中）。";
    out = os.str();
    return true;
}

static bool execUpdate(Database& db, const std::vector<std::string>& t,
                       size_t& p, std::string& out) {
    if (p >= t.size()) { out = "缺少表名"; return false; }
    std::string tname = t[p++];

    if (p < t.size() && t[p] == "(") ++p;      /* 允许 UPDATE T ( SET ... ) */
    if (p >= t.size() || upperStr(t[p]) != "SET") { out = "UPDATE 需要 SET 子句"; return false; }
    ++p;

    std::vector<std::pair<std::string, std::string> > sets;
    while (p < t.size()) {
        std::string up = upperStr(t[p]);
        if (up == "WHERE" || up == "IN" || up == "INTO" || t[p] == ")") break;
        std::string fn = t[p++];
        if (p >= t.size() || t[p] != "=") { out = "SET 子句应为 字段=值"; return false; }
        ++p;
        if (p >= t.size()) { out = "SET 子句缺少值"; return false; }
        std::string v = t[p++];
        if (upperStr(v) == "NULL") v = "";
        sets.push_back(std::make_pair(fn, v));
        if (p < t.size() && t[p] == ",") ++p;
        else break;
    }
    if (sets.empty()) { out = "SET 子句为空"; return false; }

    std::vector<Cond> conds;
    std::string err;
    if (p < t.size() && upperStr(t[p]) == "WHERE") {
        ++p;
        if (!parseConds(t, p, conds, err)) { out = err; return false; }
    }
    if (p < t.size() && t[p] == ")") ++p;
    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    int ti = db.findTable(tname);
    if (ti < 0) { out = "表 " + tname + " 不存在"; return false; }
    const TableDef& tb = db.tables[ti];

    std::vector<int> setIdx;
    for (size_t i = 0; i < sets.size(); ++i) {
        int fi = tb.findField(sets[i].first);
        if (fi < 0) { out = "字段 " + sets[i].first + " 不存在"; return false; }
        if (!tb.fields[fi].valid) { out = "字段 " + sets[i].first + " 已失效"; return false; }
        if (!checkValue(tb.fields[fi], sets[i].second, err)) { out = err; return false; }
        setIdx.push_back(fi);
    }

    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }
    int si = df.find(tname);
    if (si < 0) { out = "表 " + tname + " 中暂无记录。"; return true; }
    if (!conds.empty()) {
        Ctx probe;
        probe.tabs.push_back(&tb);
        std::vector<const Record*> nr(1, (const Record*)0);
        probe.rows = nr;
        if (!validateConds(probe, conds, err)) { out = err; return false; }
    }

    int ki = tb.keyIndex();
    int cnt = 0;
    std::vector<Record>& recs = df.records[si];
    for (size_t r = 0; r < recs.size(); ++r) {
        if (!recs[r].valid) continue;
        Ctx ctx;
        ctx.tabs.push_back(&tb);
        ctx.rows.push_back(&recs[r]);
        std::string e2;
        if (!conds.empty() && !evalConds(ctx, conds, e2)) continue;

        /* 主键唯一性检查（若改动的字段包含 KEY） */
        for (size_t k = 0; k < setIdx.size(); ++k) {
            if (setIdx[k] == ki && !sets[k].second.empty()) {
                for (size_t r2 = 0; r2 < recs.size(); ++r2) {
                    if (r2 == r || !recs[r2].valid) continue;
                    if (recs[r2].values[ki] == sets[k].second) {
                        out = "违反 KEY 约束：" + tb.fields[ki].name +
                              " 的值 " + sets[k].second + " 已存在";
                        return false;
                    }
                }
            }
        }
        for (size_t k = 0; k < setIdx.size(); ++k)
            recs[r].values[setIdx[k]] = sets[k].second;
        ++cnt;
    }
    if (!saveDataFile(db.name, db, df)) { out = "写入数据文件失败"; return false; }

    std::ostringstream os;
    os << "已修改表 " << tname << " 中 " << cnt << " 条记录。";
    out = os.str();
    return true;
}

/* -------------------- SELECT -------------------- */

struct SelItem {
    std::string text;      /* 用户书写的名字 */
    int ti, fi;
};

static bool execSelect(Database& db, const std::vector<std::string>& t,
                       size_t& p, std::string& out, QueryResult* qr) {
    std::vector<std::string> rawItems;
    bool star = false;
    if (p < t.size() && t[p] == "*") { star = true; ++p; }
    else {
        while (p < t.size()) {
            std::string up = upperStr(t[p]);
            if (up == "FROM") break;
            if (t[p] != ",") rawItems.push_back(t[p]);
            ++p;
        }
        if (rawItems.empty()) { out = "SELECT 后缺少字段列表"; return false; }
    }
    if (p >= t.size() || upperStr(t[p]) != "FROM") { out = "SELECT 缺少 FROM 子句"; return false; }
    ++p;

    std::vector<std::string> tabNames;
    while (p < t.size()) {
        std::string up = upperStr(t[p]);
        if (up == "WHERE" || up == "IN" || up == "INTO") break;
        if (t[p] != ",") tabNames.push_back(t[p]);
        ++p;
    }
    if (tabNames.empty()) { out = "FROM 后缺少表名"; return false; }

    std::vector<Cond> conds;
    std::string err;
    if (p < t.size() && upperStr(t[p]) == "WHERE") {
        ++p;
        if (!parseConds(t, p, conds, err)) { out = err; return false; }
    }
    std::string dbName = readDbName(t, p);
    if (!useDb(db, dbName, false, err)) { out = err; return false; }

    /* 收集表 */
    std::vector<const TableDef*> tabs;
    for (size_t i = 0; i < tabNames.size(); ++i) {
        int ti = db.findTable(tabNames[i]);
        if (ti < 0) { out = "表 " + tabNames[i] + " 不存在"; return false; }
        tabs.push_back(&db.tables[ti]);
    }

    /* 载入各表记录 */
    DataFile df;
    if (!loadDataFile(db.name, db, df)) { out = "读取数据文件失败"; return false; }
    std::vector<std::vector<const Record*> > rowSets(tabs.size());
    for (size_t i = 0; i < tabs.size(); ++i) {
        int si = df.find(tabs[i]->name);
        if (si >= 0)
            for (size_t r = 0; r < df.records[si].size(); ++r)
                if (df.records[si][r].valid)
                    rowSets[i].push_back(&df.records[si][r]);
        if (rowSets[i].empty()) rowSets[i].push_back(0);   /* 空表占位，保证连接结果为空 */
    }

    /* 解析输出列 */
    QueryResult res;
    std::vector<SelItem> items;
    Ctx probe;
    probe.tabs = tabs;
    std::vector<const Record*> nullRows(tabs.size(), (const Record*)0);
    probe.rows = nullRows;

    if (star) {
        for (size_t i = 0; i < tabs.size(); ++i) {
            for (size_t c = 0; c < tabs[i]->fields.size(); ++c) {
                if (!tabs[i]->fields[c].valid) continue;
                SelItem it;
                it.ti = (int)i; it.fi = (int)c;
                it.text = (tabs.size() > 1)
                              ? (tabs[i]->name + "." + tabs[i]->fields[c].name)
                              : tabs[i]->fields[c].name;
                items.push_back(it);
            }
        }
    } else {
        for (size_t i = 0; i < rawItems.size(); ++i) {
            SelItem it;
            it.text = rawItems[i];
            if (!resolveName(probe, it.text, it.ti, it.fi, err)) { out = err; return false; }
            if (!tabs[it.ti]->fields[it.fi].valid) { out = "字段 " + it.text + " 已失效"; return false; }
            items.push_back(it);
        }
    }
    for (size_t i = 0; i < items.size(); ++i) res.headers.push_back(items[i].text);

    /* 连接 + 筛选（多重循环） */
    std::vector<size_t> idx(tabs.size(), 0);
    bool finished = false;
    bool condErr = false;
    while (!finished) {
        Ctx ctx;
        ctx.tabs = tabs;
        std::vector<const Record*> cur(tabs.size(), (const Record*)0);
        bool anyNull = false;
        for (size_t i = 0; i < tabs.size(); ++i) {
            cur[i] = rowSets[i][idx[i]];
            if (cur[i] == 0) anyNull = true;
        }
        ctx.rows = cur;
        if (!anyNull) {
            std::string e2;
            bool pass = conds.empty() ? true : evalConds(ctx, conds, e2);
            if (!pass && !e2.empty()) { condErr = true; err = e2; }
            if (pass) {
                std::vector<std::string> row;
                for (size_t i = 0; i < items.size(); ++i)
                    row.push_back(cell(ctx, items[i].ti, items[i].fi));
                res.rows.push_back(row);
            }
        }
        /* 进位 */
        int k = (int)tabs.size() - 1;
        while (k >= 0) {
            idx[k]++;
            if (idx[k] < rowSets[k].size()) break;
            idx[k] = 0;
            --k;
        }
        if (k < 0) finished = true;
    }
    if (condErr) { out = err; return false; }

    if (qr) *qr = res;
    std::ostringstream os;
    os << "查询完成，共 " << res.rows.size() << " 条记录。";
    out = os.str();
    return true;
}

/* ==================== execSQL 总入口 ==================== */

bool execSQL(Database& db, const std::string& sql, std::string& outline, QueryResult* qr) {
    outline.clear();
    std::vector<std::string> t = tokenize(sql);
    if (t.empty()) { outline = "SQL 语句为空"; return false; }
    if (t[t.size() - 1] == ";") t.pop_back();
    if (t.empty()) { outline = "SQL 语句为空"; return false; }

    size_t p = 0;
    std::string cmd = upperStr(t[p]);
    ++p;

    /* 允许以 ; 结尾的多余分号 */
    while (p < t.size() && t[p] == ";") ++p;

    if (cmd == "CREATE") {
        if (p < t.size() && upperStr(t[p]) == "TABLE") return execCreate(db, t, p, outline);
        outline = "目前只支持 CREATE TABLE";
        return false;
    }
    if (cmd == "EDIT" || cmd == "ALTER") {
        if (p < t.size() && upperStr(t[p]) == "TABLE") return execEdit(db, t, p, outline);
        outline = "目前只支持 EDIT TABLE";
        return false;
    }
    if (cmd == "RENAME") {
        if (p < t.size() && upperStr(t[p]) == "TABLE") return execRenameTable(db, t, p, outline);
        outline = "目前只支持 RENAME TABLE";
        return false;
    }
    if (cmd == "DROP") {
        if (p < t.size() && upperStr(t[p]) == "TABLE") return execDropTable(db, t, p, outline);
        outline = "目前只支持 DROP TABLE";
        return false;
    }
    if (cmd == "INSERT") return execInsert(db, t, p, outline);
    if (cmd == "DELETE") return execDelete(db, t, p, outline);
    if (cmd == "UPDATE") return execUpdate(db, t, p, outline);
    if (cmd == "SELECT") return execSelect(db, t, p, outline, qr);

    outline = "不支持的语句：" + cmd;
    return false;
}

} /* namespace dbms */
