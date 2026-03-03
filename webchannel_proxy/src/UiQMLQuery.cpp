/**
 * UiQMLQuery.cpp  —  Qt 5.15.x
 *
 * parseToken 重构：手写递归下降语法解析器
 * ────────────────────────────────────────────────────────────────
 * 旧方案：正则匹配，无法清晰表达 token 的语法结构，
 *         错误时静默失败，调试困难。
 *
 * 新方案：手写递归下降解析器（Recursive Descent Parser）
 *   BNF：
 *     token        := type_part modifier*
 *     type_part    := IDENT | '*' | ε           (ε = 省略则为通配符)
 *     modifier     := attr_selector | pseudo_class
 *     attr_selector:= '[' IDENT (op value)? ']'
 *     pseudo_class := ':' pseudo_name '(' INTEGER ')'
 *     pseudo_name  := 'nth-child' | 'nth-last-child'
 *     op           := '=' | '~=' | '^=' | '$=' | '*=' | '|='
 *     value        := QUOTED_STRING | UNQUOTED_VALUE
 *     IDENT        := [a-zA-Z_-][a-zA-Z0-9_-]*
 *     INTEGER      := [0-9]+
 *
 *   解析失败时抛出 SelectorParseError，携带位置和原因。
 *
 * 新增伪类支持：
 *   :nth-child(n)      选择父元素第 n 个声明子元素（1-based）
 *   :nth-last-child(n) 选择父元素倒数第 n 个声明子元素
 */

 #include "UiQMLQuery.h"

 #include <QMetaObject>
 #include <QMetaProperty>
 #include <QSet>
 #include <QRegularExpression>
 
 #include <QtQml/QQmlProperty>
 #include <QtQuick/QQuickItem>
 #include <QtGui/QWindow>

 namespace {
 // ════════════════════════════════════════════════════════════════
 //  文件内部工具
 // ════════════════════════════════════════════════════════════════
 
 // ────────────────────────────────────────────────────────────────
 //  splitByComma — 将顶层逗号分隔的复合选择器拆分为子选择器列表
 //
 //  规则：只在「方括号深度为 0」时将逗号视为分隔符，
 //        跳过属性选择器 [...] 内部的逗号，避免误拆
 //        Button[title='a,b'] 这类含逗号的属性值。
 //
 //  示例：
 //    "Rectangle, Button[text='a,b'], Text"
 //      → ["Rectangle", "Button[text='a,b']", "Text"]
 // ────────────────────────────────────────────────────────────────
 static QStringList splitByComma(const QString& selector)
 {
     QStringList parts;
     QString current;
     int depth = 0;
     for (const QChar ch : selector) {
         if      (ch == '[')               { ++depth; current += ch; }
         else if (ch == ']')               { --depth; current += ch; }
         else if (ch == ',' && depth == 0) { const QString p = current.trimmed();
                                             if (!p.isEmpty()) parts.append(p);
                                             current.clear(); }
         else                              { current += ch; }
     }
     const QString last = current.trimmed();
     if (!last.isEmpty()) parts.append(last);
     return parts;
 }
 

 // ════════════════════════════════════════════════════════════════
 //  内部辅助：错误输出
 // ════════════════════════════════════════════════════════════════
 // ────────────────────────────────────────────────────────────────
 //  setError / clearError — 错误输出辅助
 //
 //  error 为 nullptr 时静默忽略（兼容旧调用代码）。
 //  成功路径调用 clearError() 保证调用方能通过 error->isEmpty()
 //  区分「查无结果」与「有错误」两种情况。
 // ────────────────────────────────────────────────────────────────
 static void setError(QString* error, const QString& msg)
 {
     if (error) *error = msg;
 }
 static void clearError(QString* error)
 {
     if (error) error->clear();
 }

}  // namespace

// ════════════════════════════════════════════════════════════════
 //  构造
 // ════════════════════════════════════════════════════════════════
 QmlQuerySelector::QmlQuerySelector(QObject* parent)
     : QObject(parent)
 {
 }
 
 
 // ════════════════════════════════════════════════════════════════
 //  公开 API
 //
 //  error 参数：
 //    nullptr         → 忽略错误（与旧代码兼容）
 //    非空 QString*   → 成功时写入空串；失败时写入原因，例如：
 //      "选择器为空"
 //      "root 节点为空"
 //      "parseToken: 意外字符 '!' at pos 6 in "Button!""
 //      "parseToken: 不支持的伪类 ':hover' at pos 6 in "Button:hover""
 // ════════════════════════════════════════════════════════════════
 QObject* QmlQuerySelector::querySelector(QObject* root, const QString& selector,
                                           QString* error, bool debug)  
 {
     if (!root) {
         setError(error, "querySelector: root 节点为空");
         return nullptr;
     }
     if (selector.trimmed().isEmpty()) {
         setError(error, "querySelector: 选择器为空");
         return nullptr;
     }
     if (debug) {
         LOG("Selector", QString("querySelector: %1").arg(selector));
         debugTree(root, 0);
     }
 
     // 逗号选择器：委托给 querySelectorAll 取第一个结果
     if (splitByComma(selector).size() > 1) {
         const QList<QObject*> all = querySelectorAll(root, selector, error);
         return all.isEmpty() ? nullptr : all.first();
     }
 
     try {
         // ── 构建视觉父节点映射表 ──────────────────────────────
         //  在执行任何匹配之前，先以 root 为起点 DFS 遍历整棵树，
         //  将「遍历时发现的父子关系」写入 parentMap_。
         //  这样即使某个节点的 QObject::parent() 返回 null
         //  （Qt QML 中 QQuickItem 视觉树与 QObject 树脱钩的常见现象），
         //  matchToken / matchChainRTL / siblings 也能通过 visualParent()
         //  正确找到其逻辑父节点。
         parentMap_.clear();
         buildParentMap(root, nullptr);

         SelectorChain chain = parse(selector.trimmed());
         QList<QObject*> results;
         collectAll(root, chain, results, /*stopAtFirst=*/true);
         clearError(error);
         return results.isEmpty() ? nullptr : results.first();
     } catch (const SelectorParseError& e) {
         setError(error, QString::fromStdString(e.what()));
         return nullptr;
     } catch (const std::exception& e) {
         setError(error, QString("querySelector: 内部错误: %1").arg(e.what()));
         return nullptr;
     }
 }
 
 QList<QObject*> QmlQuerySelector::querySelectorAll(QObject* root, const QString& selector,
                                                     QString* error, bool debug)
 {
     if (!root) {
         setError(error, "querySelectorAll: root 节点为空");
         return {};
     }
     if (selector.trimmed().isEmpty()) {
         setError(error, "querySelectorAll: 选择器为空");
         return {};
     }
     if (debug) {
         debugTree(root, 0);
     }
 
     try {
         parentMap_.clear();
         buildParentMap(root, nullptr);

         const QStringList parts = splitByComma(selector);
 
         if (parts.size() <= 1) {
             // 单选择器快速路径
             QList<QObject*> results;
             collectAll(root, parse(selector.trimmed()), results, /*stopAtFirst=*/false);
             clearError(error);
             return results;
         }
 
         // 逗号选择器：逐段解析，任一段失败则整体失败
         QSet<QObject*> matched;
         for (const QString& part : parts) {
             SelectorChain chain = parse(part); // 可能抛异常
             QList<QObject*> sub;
             collectAll(root, chain, sub, false);
             for (QObject* obj : sub) matched.insert(obj);
         }
 
         QList<QObject*> ordered;
         collectOrdered(root, matched, ordered);
         clearError(error);
         return ordered;
 
     } catch (const SelectorParseError& e) {
         setError(error, QString::fromStdString(e.what()));
         return {};
     } catch (const std::exception& e) {
         setError(error, QString("querySelectorAll: 内部错误: %1").arg(e.what()));
         return {};
     }
 }
 
 void QmlQuerySelector::collectOrdered(QObject*              node,
                                        const QSet<QObject*>& matched,
                                        QList<QObject*>&      ordered)
 {
     if (!node) return;
     if (matched.contains(node)) ordered.append(node);
     for (QObject* child : visualChildren(node))
         collectOrdered(child, matched, ordered);
 }
 
 // ════════════════════════════════════════════════════════════════
 //  解析阶段 — 整体选择器链（逗号已由外层剔除）
 // ════════════════════════════════════════════════════════════════
 // ════════════════════════════════════════════════════════════════
 //  parse — 将单条选择器字符串（不含顶层逗号）解析为 SelectorChain
 //
 //  逐字符扫描，识别三类元素：
 //    · 空白          → Combinator::Descendant（后代组合器）
 //    · > + ~         → 对应的显式组合器
 //    · 其他字符串    → token 文本，截取后交给 parseToken()
 //
 //  括号深度追踪（depth）确保 [...] 和 (...) 内部的组合器字符
 //  不被误当作分隔符处理。
 //
 //  解析结果缓存在 parseCache_ 中，相同选择器字符串只解析一次。
 // ════════════════════════════════════════════════════════════════
 SelectorChain QmlQuerySelector::parse(const QString& selector)
 {
     if (parseCache_.contains(selector))
         return parseCache_.value(selector);
 
     SelectorChain chain;
 
     // 将选择器字符串分割为 [combinator?, tokenSrc] 序列
     // 策略：逐字符扫描，遇到组合器字符或空白则切分
     const QString s = selector.trimmed();
     int pos = 0;
     const int len = s.length();
 
     Combinator pendingComb = Combinator::Descendant;
     bool firstToken = true;
 
     while (pos < len) {
         const QChar ch = s[pos];
 
         // ── 跳过空白，空白本身是 Descendant 组合器 ──────────
         if (ch.isSpace()) {
             if (!firstToken && pendingComb == Combinator::Descendant)
                 pendingComb = Combinator::Descendant; // 已默认，保持
             ++pos;
             // 空白后可能跟着 >, +, ~ 组合器，需要继续读
             while (pos < len && s[pos].isSpace()) ++pos;
             continue;
         }
 
         // ── 显式组合器 ───────────────────────────────────────
         if (ch == '>' || ch == '+' || ch == '~') {
             if      (ch == '>') pendingComb = Combinator::Child;
             else if (ch == '+') pendingComb = Combinator::Adjacent;
             else                pendingComb = Combinator::Sibling;
             ++pos;
             while (pos < len && s[pos].isSpace()) ++pos;
             continue;
         }
 
         // ── Token ────────────────────────────────────────────
         // 截取从 pos 到下一个顶层组合器之间的文本，交给 parseToken
         // 需要跳过 [...] 和 (...) 内部的组合器字符
         int tokenStart = pos;
         int depth = 0;
         while (pos < len) {
             const QChar c = s[pos];
             if      (c == '[' || c == '(') { ++depth; ++pos; }
             else if (c == ']' || c == ')') { --depth; ++pos; }
             else if (depth == 0 && (c.isSpace() || c == '>' || c == '+' || c == '~'))
                 break;
             else ++pos;
         }
 
         const QString tokenSrc = s.mid(tokenStart, pos - tokenStart).trimmed();
         if (tokenSrc.isEmpty()) continue;
 
         int tpos = 0;
         SelectorSegment seg;
         seg.token      = parseToken(tokenSrc, tpos); // 可能抛 SelectorParseError
         seg.combinator = firstToken ? Combinator::Descendant : pendingComb;
         chain.append(seg);
         firstToken  = false;
         pendingComb = Combinator::Descendant;
     }
 
     parseCache_.insert(selector, chain);
     return chain;
 }

QString QmlQuerySelector::debugParse(const QString& selector)
{
    QString out;
    SelectorChain chain = parse(selector);
    for (int i = 0; i < chain.size(); ++i) {
        const SelectorSegment& seg = chain[i];
        out += QString("[%1] comb=%2 type=%3").arg(i)
            .arg((int)seg.combinator)
            .arg(seg.token.typeName.isEmpty() ? QStringLiteral("*") : seg.token.typeName);
        if (!seg.token.attributes.isEmpty()) out += " attrs";
        if (!seg.token.pseudos.isEmpty()) out += " pseudos";
        out += "\n";
    }
    return out;
}

QString QmlQuerySelector::debugParsePublic(const QString& selector)
{
    return debugParse(selector);
}
 
 // ════════════════════════════════════════════════════════════════
 //  Token 递归下降解析器
 //
 //  Grammar:
 //    token        := type_part modifier*
 //    type_part    := IDENT | '*' | ε
 //    modifier     := attr_selector | pseudo_class
 //    attr_selector:= '[' IDENT (op value)? ']'
 //    pseudo_class := ':' pseudo_name '(' INTEGER ')'
 //    pseudo_name  := 'nth-child' | 'nth-last-child'
 //    op           := '=' | '~=' | '^=' | '$=' | '*=' | '|='
 //    value        := QUOTED_STRING | UNQUOTED_VALUE
 // ════════════════════════════════════════════════════════════════
 // ════════════════════════════════════════════════════════════════
 //  parseToken — 单个 token 的递归下降解析入口
 //
 //  Grammar:
 //    token     := type_part modifier*
 //    type_part := IDENT        QML 类型名，如 "TextField"
 //               | '*'          显式通配符
 //               | ε            省略类型名，等价于通配符
 //    modifier  := attr_selector | pseudo_class
 //
 //  pos 为当前解析位置（in/out），解析完毕后指向 token 结束位置。
 //  语法错误时抛出 SelectorParseError，携带出错位置和原因。
 // ════════════════════════════════════════════════════════════════
 SelectorToken QmlQuerySelector::parseToken(const QString& src, int& pos)
 {
     SelectorToken token;
     skipSpaces(src, pos);
 
     if (pos >= src.length()) return token; // 空串 → 通配符
 
     const QChar first = src[pos];
 
     // ── type_part ────────────────────────────────────────────
     if (first == '*') {
         ++pos;                       // 显式通配符，typeName 留空
     } else if (first == '[' || first == ':') {
         // 省略类型名，也是通配符
     } else if (first.isLetter() || first == '_') {
         token.typeName = parseIdent(src, pos);
     } else {
         throw SelectorParseError(
             QString("parseToken: 意外字符 '%1' at pos %2 in \"%3\"")
                 .arg(first).arg(pos).arg(src));
     }
 
     // ── modifier* ────────────────────────────────────────────
     while (pos < src.length()) {
         const QChar ch = src[pos];
         if      (ch == '[') parseAttrSelector(src, pos, token);
         else if (ch == ':') parsePseudoClass (src, pos, token);
         else break; // 遇到其他字符（组合器等），token 解析结束
     }
 
     return token;
 }
 
 // ────────────────────────────────────────────────────────────────
 //  attr_selector := '[' IDENT (op value)? ']'
 // ────────────────────────────────────────────────────────────────
 // ════════════════════════════════════════════════════════════════
 //  parseAttrSelector — 解析属性选择器 [attr] 或 [attr op value]
 //
 //  Grammar:
 //    attr_selector := '[' IDENT (op value)? ']'
 //
 //  支持的操作符（parseAttrOp 负责识别）：
 //    =   精确匹配
 //    ~=  空格分隔词中包含指定词
 //    ^=  前缀匹配
 //    $=  后缀匹配
 //    *=  子串匹配
 //    |=  等于或以"值-"开头（语言代码惯例）
 //
 //  省略 op 和 value 时（如 [visible]）表示「属性存在性」检查，
 //  op 字段留空，matchToken() 对空 op 直接通过。
 // ════════════════════════════════════════════════════════════════
 void QmlQuerySelector::parseAttrSelector(const QString& src, int& pos,
                                           SelectorToken& token)
 {
     expect(src, pos, '[');
     skipSpaces(src, pos);
 
     AttributeCondition cond;
     cond.name = parseIdent(src, pos);
     if (cond.name.isEmpty())
         throw SelectorParseError(
             QString("parseAttrSelector: 属性名不能为空 at pos %1 in \"%2\"")
                 .arg(pos).arg(src));
 
     skipSpaces(src, pos);
 
     if (pos < src.length() && src[pos] != ']') {
         cond.op    = parseAttrOp(src, pos);
         skipSpaces(src, pos);
         cond.value = parseAttrValue(src, pos);
         skipSpaces(src, pos);
     }
 
     expect(src, pos, ']');
     token.attributes.append(cond);
 }
 
 // ────────────────────────────────────────────────────────────────
 //  pseudo_class := ':' pseudo_name '(' INTEGER ')'
 // ────────────────────────────────────────────────────────────────
 // ════════════════════════════════════════════════════════════════
 //  parsePseudoClass — 解析伪类选择器 :pseudo-name(n)
 //
 //  Grammar:
 //    pseudo_class := ':' pseudo_name '(' INTEGER ')'
 //    pseudo_name  := 'nth-child' | 'nth-last-child'
 //
 //  当前支持的伪类：
 //    :nth-child(n)      父元素声明子列表中正数第 n 个（1-based）
 //    :nth-last-child(n) 父元素声明子列表中倒数第 n 个（1-based）
 //
 //  n 必须 >= 1，传入 0 或负数时抛出 SelectorParseError。
 //  遇到不支持的伪类名（如 :hover）也立即抛出错误，
 //  避免静默匹配到错误节点。
 // ════════════════════════════════════════════════════════════════
 void QmlQuerySelector::parsePseudoClass(const QString& src, int& pos,
                                          SelectorToken& token)
 {
     expect(src, pos, ':');
 
     const QString name = parseIdent(src, pos);
     if (name.isEmpty())
         throw SelectorParseError(
             QString("parsePseudoClass: 伪类名不能为空 at pos %1 in \"%2\"")
                 .arg(pos).arg(src));
 
     PseudoClass pc;
     if      (name == "nth-child")      pc.type = PseudoClass::NthChild;
     else if (name == "nth-last-child") pc.type = PseudoClass::NthLastChild;
     else
         throw SelectorParseError(
             QString("parsePseudoClass: 不支持的伪类 ':%1' at pos %2 in \"%3\"")
                 .arg(name).arg(pos).arg(src));
 
     expect(src, pos, '(');
     skipSpaces(src, pos);
     pc.n = parseInteger(src, pos);
     if (pc.n < 1)
         throw SelectorParseError(
             QString("parsePseudoClass: :nth 参数必须 >= 1，got %1 in \"%2\"")
                 .arg(pc.n).arg(src));
     skipSpaces(src, pos);
     expect(src, pos, ')');
 
     token.pseudos.append(pc);
 }
 
 // ────────────────────────────────────────────────────────────────
 //  IDENT := [a-zA-Z_][a-zA-Z0-9_-]*
 //  支持连字符（nth-child、my-component 等）
 // ────────────────────────────────────────────────────────────────
 // ════════════════════════════════════════════════════════════════
 //  parseIdent — 解析标识符
 //
 //  Grammar:
 //    IDENT := [a-zA-Z0-9_-]+
 //
 //  允许连字符，支持 "nth-child"、"my-component" 等名称。
 //  遇到非标识符字符时停止，返回已读取部分（可能为空串）。
 //  调用方负责检查返回值是否为空并决定是否抛出错误。
 // ════════════════════════════════════════════════════════════════
 QString QmlQuerySelector::parseIdent(const QString& src, int& pos)
 {
     QString result;
     while (pos < src.length()) {
         const QChar ch = src[pos];
         if (ch.isLetterOrNumber() || ch == '_' || ch == '-') {
             result += ch;
             ++pos;
         } else {
             break;
         }
     }
     return result;
 }
 
 // ────────────────────────────────────────────────────────────────
 //  op := '~=' | '^=' | '$=' | '*=' | '|=' | '='
 //  多字符操作符优先
 // ────────────────────────────────────────────────────────────────
 // ════════════════════════════════════════════════════════════════
 //  parseAttrOp — 解析属性操作符
 //
 //  Grammar:
 //    op := '~=' | '^=' | '$=' | '*=' | '|=' | '='
 //
 //  优先尝试双字符操作符（向前看一个字符），再尝试单字符 '='。
 //  其他字符组合视为语法错误，立即抛出 SelectorParseError。
 // ════════════════════════════════════════════════════════════════
 QString QmlQuerySelector::parseAttrOp(const QString& src, int& pos)
 {
     if (pos >= src.length())
         throw SelectorParseError(
             QString("parseAttrOp: 意外结束 at pos %1 in \"%2\"").arg(pos).arg(src));
 
     const QChar ch = src[pos];
 
     // 双字符操作符
     if (pos + 1 < src.length() && src[pos + 1] == '=') {
         if (ch == '~' || ch == '^' || ch == '$' || ch == '*' || ch == '|') {
             QString op = QString(ch) + '=';
             pos += 2;
             return op;
         }
     }
 
     // 单字符 =
     if (ch == '=') { ++pos; return "="; }
 
     throw SelectorParseError(
         QString("parseAttrOp: 未知操作符 '%1' at pos %2 in \"%3\"")
             .arg(ch).arg(pos).arg(src));
 }
 
 // ────────────────────────────────────────────────────────────────
 //  value := QUOTED_STRING | UNQUOTED_VALUE
 //  QUOTED_STRING := '\'' [^']* '\'' | '"' [^"]* '"'
 //  UNQUOTED_VALUE:= 到 ']' 之前的非空白字符
 // ────────────────────────────────────────────────────────────────
 // ════════════════════════════════════════════════════════════════
 //  parseAttrValue — 解析属性值
 //
 //  Grammar:
 //    value := QUOTED_STRING | UNQUOTED_VALUE
 //    QUOTED_STRING  := '\'' [^\']* '\'' | '"' [^"]* '"'
 //    UNQUOTED_VALUE := ( 非 ']' 非空白 )+
 //
 //  引号字符串：单引号或双引号均支持，内部不支持转义序列，
 //              引号未闭合时抛出 SelectorParseError。
 //  无引号字符串：读到 ']' 或空白为止，结果为空时抛出错误。
 //
 //  建议在选择器中始终使用引号包裹属性值，
 //  以避免含空格或特殊字符的值被截断。
 // ════════════════════════════════════════════════════════════════
 QString QmlQuerySelector::parseAttrValue(const QString& src, int& pos)
 {
     if (pos >= src.length())
         throw SelectorParseError(
             QString("parseAttrValue: 意外结束 at pos %1 in \"%2\"").arg(pos).arg(src));
 
     const QChar quote = src[pos];
 
     if (quote == '\'' || quote == '"') {
         ++pos; // 跳过开引号
         QString result;
         while (pos < src.length() && src[pos] != quote) {
             result += src[pos++];
         }
         if (pos >= src.length())
             throw SelectorParseError(
                 QString("parseAttrValue: 引号未闭合 in \"%1\"").arg(src));
         ++pos; // 跳过闭引号
         return result;
     }
 
     // 无引号：读到 ']' 或空白
     QString result;
     while (pos < src.length() && src[pos] != ']' && !src[pos].isSpace()) {
         result += src[pos++];
     }
     if (result.isEmpty())
         throw SelectorParseError(
             QString("parseAttrValue: 属性值为空 at pos %1 in \"%2\"").arg(pos).arg(src));
     return result;
 }
 
 // ────────────────────────────────────────────────────────────────
 //  INTEGER := [0-9]+
 // ────────────────────────────────────────────────────────────────
 // ════════════════════════════════════════════════════════════════
 //  parseInteger — 解析非负整数
 //
 //  Grammar:
 //    INTEGER := [0-9]+
 //
 //  当前位置不是数字时立即抛出 SelectorParseError。
 //  结果通过 QString::toInt() 转换，不检查溢出，
 //  实际上 :nth-child 的参数不会超过几百，无需特别处理。
 // ════════════════════════════════════════════════════════════════
 int QmlQuerySelector::parseInteger(const QString& src, int& pos)
 {
     if (pos >= src.length() || !src[pos].isDigit())
         throw SelectorParseError(
             QString("parseInteger: 期望数字 at pos %1 in \"%2\"").arg(pos).arg(src));
 
     QString digits;
     while (pos < src.length() && src[pos].isDigit())
         digits += src[pos++];
 
     return digits.toInt();
 }
 
 // ────────────────────────────────────────────────────────────────
 //  skipSpaces — 跳过当前位置起的连续空白字符
 //  用于 parseAttrSelector / parsePseudoClass 中吃掉可选空白。
 // ────────────────────────────────────────────────────────────────
 void QmlQuerySelector::skipSpaces(const QString& src, int& pos)
 {
     while (pos < src.length() && src[pos].isSpace()) ++pos;
 }
 
 // ────────────────────────────────────────────────────────────────
 //  expect — 断言当前字符为 ch 并推进 pos
 //
 //  若当前位置超出字符串或字符不匹配，抛出 SelectorParseError，
 //  错误信息包含期望字符、实际字符和位置，便于定位选择器错误。
 // ────────────────────────────────────────────────────────────────
 void QmlQuerySelector::expect(const QString& src, int& pos, QChar ch)
 {
     if (pos >= src.length())
         throw SelectorParseError(
             QString("expect '%1': 意外结束 in \"%2\"").arg(ch).arg(src));
     if (src[pos] != ch)
         throw SelectorParseError(
             QString("expect '%1': 实际为 '%2' at pos %3 in \"%4\"")
                 .arg(ch).arg(src[pos]).arg(pos).arg(src));
     ++pos;
 }
 
 // ════════════════════════════════════════════════════════════════
 //  匹配阶段
 // ════════════════════════════════════════════════════════════════
 //  matchToken — 判断单个节点是否满足一个 token 的全部约束
 //
 //  依次检查三层条件，任一层不满足则立即返回 false：
 //    ① 类型名：调用 resolveTypeName() 与 token.typeName 比较；
 //              typeName 为空时跳过（通配符匹配所有类型）。
 //    ② 属性条件：逐条调用 property() 读取属性值并按 op 比较；
 //               属性不存在视为不匹配；op 为空时仅检查存在性。
 //    ③ 伪类：通过 declaredChildren() 获取声明子列表，
 //            计算节点的 1-based 正向 / 反向位置与 n 比较。
 //            父节点为空时视为不匹配。
 // ════════════════════════════════════════════════════════════════
 bool QmlQuerySelector::matchToken(QObject* obj, const SelectorToken& token) const
 {
     if (!obj) return false;
 
     // ① 类型名（大小写不敏感比较，以支持 "compD" 匹配 "CompD"）
     // 但特殊处理：大写的自定义组件名作为"独立选择器"时不应匹配实例
     if (!token.typeName.isEmpty()) {
         QString resolvedName = resolveTypeName(obj);
         if (resolvedName.compare(token.typeName, Qt::CaseInsensitive) != 0)
             return false;
         
         // 额外检查：大写首字母且是自定义组件类型（_QMLTYPE_）的不直接匹配
         // 这防止"CompA, CompB"这样的纯大写选择器匹配组件实例
         QString className = QString::fromLatin1(obj->metaObject()->className());
         if (className.contains("_QMLTYPE_") && 
             !resolvedName.isEmpty() && resolvedName[0].isUpper()) {
             // 这是大写的自定义组件实例
             // 只有当选择器是小写（如"compA")或显式大小写匹配时才允许
             if (token.typeName[0].isUpper()) {
                 // 选择器是大写，不匹配（除非是部分链）
                 // TODO: 更精细的链检查可能需要在这里或上层
                 // 为简化，现在允许大写匹配（与"compD Text"兼容）
             }
         }
     }
 
     // ② 属性条件
     for (const AttributeCondition& cond : token.attributes) {
         const QVariant val = property(obj, cond.name);
         if (!val.isValid()) return false;
         if (cond.op.isEmpty()) continue; // 存在性检查通过
 
         const QString sv = val.toString().trimmed();
         bool ok = false;
         if      (cond.op == "=")  ok = (sv == cond.value);
         else if (cond.op == "~=") {
             const QStringList words = sv.split(QChar(' '), QString::SkipEmptyParts);
             ok = words.contains(cond.value);
         }
         else if (cond.op == "^=") ok = sv.startsWith(cond.value);
         else if (cond.op == "$=") ok = sv.endsWith(cond.value);
         else if (cond.op == "*=") ok = sv.contains(cond.value);
         else if (cond.op == "|=") ok = (sv == cond.value || sv.startsWith(cond.value + '-'));
         if (!ok) return false;
     }
 
     // ③ 伪类
     if (!token.pseudos.isEmpty()) {
         QObject* parent = visualParent(obj);
         if (!parent) return false;
 
         const QList<QObject*> sibs = declaredChildren(parent);
         const int total   = sibs.size();
         const int selfIdx = sibs.indexOf(obj); // 0-based
         if (selfIdx < 0) return false;
 
         const int pos1based      = selfIdx + 1;           // 1-based 正向位置
         const int posFromEnd     = total - selfIdx;       // 1-based 反向位置
 
         for (const PseudoClass& pc : token.pseudos) {
             bool ok = false;
             if      (pc.type == PseudoClass::NthChild)     ok = (pos1based  == pc.n);
             else if (pc.type == PseudoClass::NthLastChild)  ok = (posFromEnd == pc.n);
             if (!ok) return false;
         }
     }
 
     return true;
 }
 
 // ════════════════════════════════════════════════════════════════
 //  matchChainRTL — 从右向左回溯验证选择器链（Right-to-Left）
 //
 //  调用约定：
 //    obj  — 已匹配 chain[idx+1] 的节点（最右侧 token 由调用方验证）
 //    idx  — 当前待验证的 chain 下标（向左递减）
 //
 //  idx < 0 表示所有 token 均已通过，返回 true。
 //
 //  各组合器语义：
 //    Descendant  沿 QObject::parent() 链向上查找任意祖先
 //    Child       仅检查直接父节点
 //    Adjacent    取 declaredChildren 列表中紧邻的前一个兄弟
 //    Sibling     遍历 declaredChildren 列表中所有前方兄弟
 //
 //  RTL 策略优势（类似 Blink）：先用最右侧的高选择性 token
 //  过滤掉大量不匹配节点，再向上回溯，显著减少比较次数。
 // ════════════════════════════════════════════════════════════════
 bool QmlQuerySelector::matchChainRTL(QObject*             obj,
                                       const SelectorChain& chain,
                                       int                  idx) const
 {
     if (idx < 0) return true;
 
    const SelectorSegment& seg = chain[idx];
    // combinator 把在 token 上存为“前导组合器”，因此在 RTL 回溯时，
    // 用来连接 seg 与其右侧 token 的组合器实际上保存在 chain[idx+1].combinator。
    const Combinator comb = chain[idx + 1].combinator;
    switch (comb) {

    case Combinator::Descendant: {
       QObject* ancestor = visualParent(obj);
         while (ancestor) {
             if (matchToken(ancestor, seg.token) &&
                 matchChainRTL(ancestor, chain, idx - 1))
                 return true;
             ancestor = visualParent(ancestor);
         }
         return false;
     }
     case Combinator::Child: {
         QObject* p = visualParent(obj);
         return p && matchToken(p, seg.token) && matchChainRTL(p, chain, idx - 1);
     }
     case Combinator::Adjacent: {
         const QList<QObject*> sibs = siblings(obj, true);
         if (sibs.isEmpty()) return false;
         QObject* prev = sibs.last();
        {
            const bool mt = matchToken(prev, seg.token);
            LOG("SelectorDebug", QString("Adjacent: obj=%1 prev=%2 matchPrev=%3")
                .arg(obj ? obj->objectName() : QString()).arg(prev ? prev->objectName() : QString())
                .arg(mt ? "true" : "false"));
            return mt && matchChainRTL(prev, chain, idx - 1);
        }
     }
     case Combinator::Sibling: {
         for (QObject* sib : siblings(obj, true)) {
             if (matchToken(sib, seg.token) && matchChainRTL(sib, chain, idx - 1))
                 return true;
         }
         return false;
     }
     }
     return false;
 }
 
 // ════════════════════════════════════════════════════════════════
 //  isBuiltinQmlType — 判断是否为 Qt 内建 QML 类型
 //
 //  自定义 QML 组件应该被视为"原子容器"，不应自动遍历内部。
 //  此函数通过检查已知的 Qt Quick 内建类型名来识别。
 // ════════════════════════════════════════════════════════════════
 static bool isBuiltinQmlType(const QString& typeName) {
     // Qt Quick 标准内建类型集合
     static const QSet<QString> builtinTypes = {
         // 基础项
         "Item", "Rectangle", "Image", "Repeater", "Loader", "Component",
         // 文本与输入
         "Text", "TextEdit", "TextInput", "TextField",
         // 按钮与交互
         "Button", "CheckBox", "RadioButton", "Switch", "ScrollBar",
         // 容器与布局
         "Column", "Row", "Grid", "Flow", "Flickable", "ListView", "GridView",
         "ColumnLayout", "RowLayout", "GridLayout", "FlowLayout",
         // 控件与高级组件
         "ComboBox", "SpinBox", "Slider", "RangeSlider", "ProgressBar",
         "Dial", "ToolTip", "BusyIndicator", "DelayButton",
         // 菜单与弹窗
         "Menu", "MenuItem", "MenuBar", "MenuSeparator", "Popup",
         "Dialog", "ColorDialog", "FileDialog", "FolderDialog", "MessageDialog",
         // 状态栏与标签
         "StatusBar", "ToolBar", "TabBar", "TabButton", "TabBarLayout",
         "Label", "Frame", "GroupBox", "Pane",
         // 其他
         "Window", "ApplicationWindow", "Control", "AbstractButton",
         "AbstractSlider", "Container", "Control", "Popup",
         "MouseArea", "PathView", "SwipeView", "StackView", "Tumbler"
     };
     return builtinTypes.contains(typeName);
 }

 // ════════════════════════════════════════════════════════════════
 //  遍历阶段
 // ════════════════════════════════════════════════════════════════
 //  collectAll — DFS 遍历子树，收集所有匹配节点
 //
 //  对自定义组件的处理（原子容器策略）：
 //    当遇到自定义组件实例时，只有在以下情况才进入其内部：
 //      1. 链中的某个 token 明确匹配该组件的类型  
 //      2. 或当前节点本身就是链的根（初始调用）
 //    这确保全局选择器（如 "Rectangle"）不会进入 CompA、CompB 等组件
 //
 //  其他优化层次：
 //    1. Bloom Filter 前置剪枝：若当前子树的布隆过滤器判定
 //       「一定不含」最右侧 token 的类型名，跳过整棵子树。
 //    2. RTL 匹配：当前节点通过最右侧 token 后，再由
 //       matchChainRTL() 向左回溯验证完整链。
 //    3. stopAtFirst 短路：querySelector() 传入 true，
 //       找到第一个结果后立即停止整个 DFS。
 //
 //  子树遍历使用 visualChildren()（包含 QQuickItem::childItems()），
 //  保证可视节点不被遗漏。
 // ════════════════════════════════════════════════════════════════
 void QmlQuerySelector::collectAll(QObject*             node,
                                    const SelectorChain& chain,
                                    QList<QObject*>&     results,
                                    bool                 stopAtFirst)
 {
     if (!node || chain.isEmpty()) return;
 
     const SelectorSegment& rightmost = chain.last();
     if (!rightmost.token.typeName.isEmpty()) {
         BloomFilter bf = buildBloom(node);
         if (!bf.mayContain(rightmost.token.typeName)) {
            // Bloom 剪枝前，还要检查 Window 子节点（不在可视树里）
            goto check_window_children;
         }
     }
 
     if (matchToken(node, rightmost.token)) {
         if (matchChainRTL(node, chain, chain.size() - 2)) {
             results.append(node);
             if (stopAtFirst) return;
         }
     }
 
     // ── 原子容器策略：自定义组件不自动遍历 ──────────────────
     // 对于自定义 QML 组件实例（_QMLTYPE_ 标记），只在某些情况下进入内部：
     //   · 链中明确引用该组件的类型名
     //   · 或选择器为通配符
     // 对于内建类型（无 _QMLTYPE_）或容器型组件，总是进入
     {
         bool shouldTraverseChildren = true;
         
         // 检查是否是自定义组件（非容器型）
         QString className = QString::fromLatin1(node->metaObject()->className());
         QString nodeTypeName = resolveTypeName(node);
         bool isCustomComp = className.contains("_QMLTYPE_");
         
         if (isCustomComp) {
             // 检查是否为容器型（Item/Rectangle 等）,容器型仍可进入
             static const QSet<QString> containerTypes = {
                 "Item", "Rectangle", "Column", "Row", "Grid", "Flow",
                 "Flickable", "ListView", "GridView", "Repeater", "Component"
             };
             
             if (!containerTypes.contains(nodeTypeName)) {
                 // 这是"非容器型"自定义组件：检查是否应该进入
                 shouldTraverseChildren = false;
                 for (const SelectorSegment& seg : chain) {
                     if (seg.token.typeName.isEmpty()) {
                         // 通配符
                         shouldTraverseChildren = true;
                         break;
                     }
                     // 大小写不�敏感比较
                     if (seg.token.typeName.compare(nodeTypeName, Qt::CaseInsensitive) == 0) {
                         shouldTraverseChildren = true;
                         break;
                     }
                 }
             }
         }

         if (shouldTraverseChildren) {
             for (QObject* child : visualChildren(node)) {
                 collectAll(child, chain, results, stopAtFirst);
                 if (stopAtFirst && !results.isEmpty()) return;
             }
         }
     }

check_window_children:
    // ── 新增轨道：遍历直接 QWindow 子节点（如 CusMaskLayer）──
    for (QObject* child : node->children()) {
        if (!qobject_cast<QWindow*>(child)) continue;
        if (results.contains(child))        continue; // 去重
        collectAll(child, chain, results, stopAtFirst);
        if (stopAtFirst && !results.isEmpty()) return;
    }
 }
 
 // ════════════════════════════════════════════════════════════════
 //  buildBloom — 递归构建子树的布隆过滤器
 //
 //  将子树中每个节点的 QML 类型名加入过滤器。
 //  collectAll() 在遍历前先调用本函数，若目标类型名
 //  在过滤器中不存在（mayContain 返回 false），
 //  则可以安全跳过整棵子树，无需逐节点匹配。
 //
 //  注意：过滤器存储的是 resolveTypeName() 的结果（QML 名），
 //        与选择器中的 typeName 命名空间完全一致。
 // ════════════════════════════════════════════════════════════════
 BloomFilter QmlQuerySelector::buildBloom(QObject* node) const
 {
     BloomFilter bf;
     const QString name = resolveTypeName(node);
     if (!name.isEmpty()) bf.add(name);
     for (QObject* child : visualChildren(node))
         bf.merge(buildBloom(child));
     return bf;
 }
 
 // ════════════════════════════════════════════════════════════════
 //  节点访问：双轨策略
 // ════════════════════════════════════════════════════════════════
 // ════════════════════════════════════════════════════════════════
 //  visualChildren — 获取「可视子节点」列表（子树遍历专用）
 //
 //  对 QQuickItem 使用 childItems()，返回所有可渲染子项，
 //  包括 Layout 等容器注入的内部辅助 QQuickItem（幽灵项）。
 //  这些幽灵项在视觉树中真实存在，遍历时不应跳过。
 //
 //  非 QQuickItem 退回到 QObject::children()。
 //
 //  ⚠ 此函数仅用于 collectAll / buildBloom / collectOrdered
 //    的子树遍历，不用于兄弟顺序判断。
 //    兄弟判断请使用 declaredChildren()，两者职责严格分离。
 // ════════════════════════════════════════════════════════════════
 QList<QObject*> QmlQuerySelector::visualChildren(QObject* obj) const
 {
     if (!obj) return {};
     if (auto* qi = qobject_cast<QQuickItem*>(obj)) {
         QList<QObject*> result;
         const auto items = qi->childItems();
         result.reserve(items.size());
         for (QQuickItem* item : items) result.append(item);
         return result;
     }
     return obj->children();
 }
 
 // ════════════════════════════════════════════════════════════════
 //  declaredChildren — 获取「声明子节点」列表（兄弟判断专用）
 //
 //  基于 QObject::children()，严格反映 QML 源码的声明顺序。
 //
 //  过滤规则（保留以下两类，其余丢弃）：
 //    · resolveTypeName() 非空   → 有 QML 类型注册的用户控件
 //    · objectName() 非空        → 用户手动命名的对象
 //
 //  过滤的目的是排除 Layout 等容器向 QObject 树注入的
 //  内部辅助对象（如 QQuickLayoutAttached），这些对象
 //  在 QML 源码中没有对应声明，不应计入兄弟顺序。
 //
 //  ⚠ 此函数仅用于 siblings() / :nth-child 位置计算，
 //    不用于子树遍历，两者职责严格分离。
 // ════════════════════════════════════════════════════════════════
 QList<QObject*> QmlQuerySelector::declaredChildren(QObject* obj) const
 {
     if (!obj) return {};
    if (!obj) return {};

    // 首先尝试使用 QObject::children() 的过滤列表恢复 QML 源码的声明顺序。
    // 在多数场景 QObject::children() 更接近源码的声明顺序，能使 + / ~ / :nth-child
    // 判断更稳定；当 QObject::children() 过滤后为空时，再回退到 visualChildren().
    QList<QObject*> result;
    for (QObject* child : obj->children()) {
        if (!child) continue;
        if (!resolveTypeName(child).isEmpty()) result.append(child);
    }
    if (!result.isEmpty()) return result;

    // 回退到可视子节点的过滤列表（保证在 QObject::children 不可信时仍能工作）
    QList<QObject*> visualFiltered;
    const QList<QObject*> visual = visualChildren(obj);
    for (QObject* child : visual) {
        if (!child) continue;
        if (!resolveTypeName(child).isEmpty()) visualFiltered.append(child);
    }
    return visualFiltered;
 }

 // ════════════════════════════════════════════════════════════════
 //  siblings — 获取兄弟节点列表
 //
 //  onlyPreceding = true   返回 obj 之前的所有声明兄弟（用于 + / ~）
 //  onlyPreceding = false  返回 obj 之后的所有声明兄弟
 //
 //  内部调用 declaredChildren(parent) 取声明子列表，
 //  用 indexOf() 定位 obj，然后截取对应半段。
 //  若 obj 未出现在声明子列表中（理论上不应发生），返回空列表。
 // ════════════════════════════════════════════════════════════════
 QList<QObject*> QmlQuerySelector::siblings(QObject* obj, bool onlyPreceding) const
 {
     if (!obj) return {};
     // ── 使用 visualParent() 代替 obj->parent() ───────────────
     QObject* p = visualParent(obj);
     if (!p) return {};
    const QList<QObject*> all = declaredChildren(p);
    int selfIdx = all.indexOf(obj);

    if (selfIdx < 0) {
        // 如果 obj 未出现在 declaredChildren 列表中，说明它不是父节点
        // 在 QML 源码层面的直接声明子项（例如组件内部声明的子项），
        // 不应被视为同一声明域的兄弟节点。因此直接返回空列表，
        // 以避免跨组件边界进行 + / ~ / :nth-child 判定。
        return {};
    }

    return onlyPreceding ? all.mid(0, selfIdx) : all.mid(selfIdx + 1);
 }
 
 // ════════════════════════════════════════════════════════════════
 //  resolveTypeName — 将 C++ 元对象类名还原为 QML 组件名
 //
 //  Qt 5.15 中 QML 运行时会为每个组件类型生成一个 C++ 类，
 //  其 metaObject()->className() 带有两种动态后缀：
 //
 //    _QMLTYPE_\d+   内联或文件级自定义组件
 //                    例: "MyButton_QMLTYPE_42"
 //    _QML_\d+       匿名/动态创建组件（Component.createObject 等）
 //                    例: "QQuickRectangle_QML_7"
 //
 //  Qt Quick / Qt Quick Controls 2 的所有内建控件均以 "QQuick" 开头：
 //    QQuickTextField → TextField
 //    QQuickComboBox  → ComboBox
 //    QQuickColumnLayout → ColumnLayout
 //
 //  处理步骤：
 //    Step 1  用 QRegularExpression 去掉 _(QMLTYPE|QML)_\d+ 后缀
 //            正则锚定在字符串末尾（$），保证只截断真正的后缀，
 //            不误伤名称中间恰好含有 _QML_ 的第三方组件名。
 //    Step 2  若剩余名以 "QQuick" 开头则去掉该前缀（长度 6）
 //
 //  对照示例：
 //    "QQuickTextField_QML_3"   → "QQuickTextField" → "TextField"
 //    "QQuickComboBox"          → "QQuickComboBox"  → "ComboBox"
 //    "MyWidget_QMLTYPE_12"     → "MyWidget"        → "MyWidget"
 //    "QQuickColumnLayout"      → "QQuickColumnLayout" → "ColumnLayout"
 // ════════════════════════════════════════════════════════════════
 QString QmlQuerySelector::resolveTypeName(QObject* obj) const
 {
     if (!obj) return {};
 
     QString name = QString::fromLatin1(obj->metaObject()->className());
 
     // Step 1：用正则去掉动态后缀 _(QMLTYPE|QML)_\d+
     //   锚定 $ 保证只匹配真正的尾部后缀
     static const QRegularExpression suffixRe(
         QStringLiteral("_(QMLTYPE|QML)_\\d+$"));
     name.remove(suffixRe);
 
     // Step 2：去掉 Qt Quick 统一前缀 "QQuick"（长度固定为 6）
     if (name.startsWith(QLatin1String("QQuick")))
         name = name.mid(6);
 
     return name;
 }
 

 // ════════════════════════════════════════════════════════════════
 //  property — 读取节点属性值（三级回退）
 //
 //  Level 1 — QMetaProperty（Q_PROPERTY 静态属性）
 //    通过 metaObject()->indexOfProperty() + QMetaProperty::read()。
 //    速度最快，覆盖 C++ 侧显式声明的属性（text、visible 等）。
 //
 //  Level 2 — QObject::property()（动态属性）
 //    覆盖通过 setProperty() 在运行时动态添加的属性。
 //
 //  Level 3 — QQmlProperty（QML 绑定属性 / attached property）
 //    覆盖 placeholderText 等仅通过 QML 类型系统暴露、
 //    在 C++ 侧没有 Q_PROPERTY 宏的属性。
 //    无需私有头文件，QQmlProperty 是公开 API。
 //
 //  三级均不命中时返回无效的 QVariant，matchToken() 将视为不匹配。
 // ════════════════════════════════════════════════════════════════
 QVariant QmlQuerySelector::property(QObject* obj, const QString& name) const
 {
     if (!obj || name.isEmpty()) return {};
     const QByteArray ba = name.toLatin1();
 
     // Level 1: Q_PROPERTY
     const QMetaObject* mo = obj->metaObject();
     const int idx = mo->indexOfProperty(ba.constData());
     if (idx >= 0) {
         const QVariant v = mo->property(idx).read(obj);
         if (v.isValid()) return v;
     }
 
     // Level 2: 动态属性
     {
         const QVariant v = obj->property(ba.constData());
         if (v.isValid()) return v;
     }
 
     // Level 3: QQmlProperty
     {
         QQmlProperty qp(obj, name);
         if (qp.isValid()) return qp.read();
     }
 
     return {};
 }

 void QmlQuerySelector::debugTree(QObject* node, int depth) const
 {
    if (!node) return;
    // 过滤 visible = false 的节点（及其整棵子树）
    const QVariant vis = property(node, QStringLiteral("visible"));
    if (vis.isValid() && !vis.toBool()) return;

    QString indent(depth * 2, ' ');
    LOG("Tree", indent + QString("%1").arg(resolveTypeName(node)) +
             "| type:" + node->metaObject()->className() +
             "| parent:" + (node->parent() ? resolveTypeName(node->parent()) : "null"));
    for (QObject* child : visualChildren(node))
        debugTree(child, depth + 1);
 }

// ════════════════════════════════════════════════════════════════
//  buildParentMap — 以 node 为当前节点、parent 为其逻辑父节点，
//                   DFS 遍历整棵可视树，将映射关系写入 parentMap_。
//
//  为什么需要这张表：
//    Qt QML 的 QQuickItem 视觉树（parentItem/childItems）与
//    QObject 对象树（parent/children）相互独立。
//    当 Column、Row、ListView 等容器通过 QML 引擎动态实例化子项时，
//    子项的 QObject::parent() 往往为 null（取决于创建方式），
//    而 QQuickItem::parentItem() 才是正确的视觉父节点。
//
//    本函数使用与 collectAll / visualChildren 完全一致的 DFS 路径，
//    确保「遍历时发现的父子关系」100% 覆盖查询所用的节点集合。
//
//  用法：
//    在 querySelector / querySelectorAll 入口处，
//    执行任何匹配逻辑之前调用一次。
//    后续所有需要父节点的地方改为调用 visualParent(obj)。
// ════════════════════════════════════════════════════════════════
void QmlQuerySelector::buildParentMap(QObject* node, QObject* parent)
{
    if (!node) return;

    // 将当前节点的逻辑父节点写入映射表
    // （root 节点的 parent 传入 nullptr，表示无父节点）
    if (parent) {
        parentMap_.insert(node, parent);
    }

    // 使用与 collectAll 完全相同的 visualChildren 遍历路径，
    // 保证映射表与实际遍历集合完全一致
    for (QObject* child : visualChildren(node)) {
        buildParentMap(child, node);
    }

    // ── 同步覆盖 QWindow 子节点（与 collectAll 中的 check_window_children 对齐）──
    for (QObject* child : node->children()) {
        if (!qobject_cast<QWindow*>(child)) continue;
        if (parentMap_.contains(child))    continue; // 已由 visualChildren 覆盖，跳过
        buildParentMap(child, node);
    }
}

// ════════════════════════════════════════════════════════════════
//  visualParent — 获取节点的「逻辑父节点」
//
//  查询策略（优先级从高到低）：
//    1. parentMap_ 表（由 buildParentMap 在遍历时填充）
//       覆盖所有通过 QQuickItem::parentItem 管理的视觉子节点，
//       即使其 QObject::parent() 为 null 也能正确返回父节点。
//    2. QObject::parent()（回退）
//       覆盖不在可视树中、但通过标准 QObject 父子关系组织的节点。
//
//  所有原先直接调用 obj->parent() 的地方（matchToken 中的
//  :nth-child 计算、matchChainRTL 中的 Descendant/Child 回溯、
//  siblings() 中的兄弟查找）均改为调用本函数。
// ════════════════════════════════════════════════════════════════
QObject* QmlQuerySelector::visualParent(QObject* obj) const
{
    if (!obj) return nullptr;

    // 优先查映射表（视觉父节点，由 buildParentMap 填充）
    QObject* mapped = parentMap_.value(obj, nullptr);
    if (mapped) return mapped;

    // 回退到 QObject 父节点（兼容不在可视树中的节点）
    return obj->parent();
}

