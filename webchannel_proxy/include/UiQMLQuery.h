#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QList>
#include <QHash>
#include <QSet>
#include <stdexcept>
#include <bitset>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

// ════════════════════════════════════════════════════════════════
//  解析错误异常
//  parseToken() 遇到语法错误时抛出，携带位置和原因信息。
// ════════════════════════════════════════════════════════════════
class SelectorParseError : public std::runtime_error {
public:
    explicit SelectorParseError(const QString& msg)
        : std::runtime_error(msg.toStdString()) {}
};

// ════════════════════════════════════════════════════════════════
//  1. 属性条件
// ════════════════════════════════════════════════════════════════
struct AttributeCondition {
    QString name;   // 属性名，e.g. "placeholderText"
    QString op;     // "=" | "~=" | "^=" | "$=" | "*=" | "|=" | ""(仅存在性)
    QString value;  // 期望值
};

// ════════════════════════════════════════════════════════════════
//  2. 伪类
// ════════════════════════════════════════════════════════════════
struct PseudoClass {
    enum Type {
        NthChild,       // :nth-child(n)
        NthLastChild    // :nth-last-child(n)
    };
    Type type;
    int  n;     // 位置参数（1-based）
};

// ════════════════════════════════════════════════════════════════
//  3. 选择器 Token
// ════════════════════════════════════════════════════════════════
struct SelectorToken {
    QString                   typeName;    // QML 类型名；空 = 通配符 *
    QList<AttributeCondition> attributes;
    QList<PseudoClass>        pseudos;
};

// ════════════════════════════════════════════════════════════════
//  4. 组合器类型
// ════════════════════════════════════════════════════════════════
enum class Combinator {
    Descendant, // ' '  后代
    Child,      // '>'  直接子
    Adjacent,   // '+'  紧邻前兄弟
    Sibling     // '~'  任意前兄弟
};

// ════════════════════════════════════════════════════════════════
//  5. 选择器段
// ════════════════════════════════════════════════════════════════
struct SelectorSegment {
    SelectorToken token;
    Combinator    combinator;
};

using SelectorChain = QList<SelectorSegment>;

// ════════════════════════════════════════════════════════════════
//  6. 布隆过滤器
// ════════════════════════════════════════════════════════════════
class BloomFilter {
    static constexpr int BITS = 1024;
    std::bitset<BITS> bits_;

    int hash1(const QString& s) const {
        uint h = 5381;
        for (QChar c : s) h = ((h << 5) + h) ^ c.unicode();
        return static_cast<int>(h % BITS);
    }
    int hash2(const QString& s) const {
        uint h = 0;
        for (QChar c : s) h = h * 31 + c.unicode();
        return static_cast<int>((h ^ (h >> 16)) % BITS);
    }

public:
    void add(const QString& s)             { bits_.set(hash1(s)); bits_.set(hash2(s)); }
    bool mayContain(const QString& s) const{ return bits_.test(hash1(s)) && bits_.test(hash2(s)); }
    void merge(const BloomFilter& o)       { bits_ |= o.bits_; }
};

// ════════════════════════════════════════════════════════════════
//  7. 主引擎类
//
//  支持的选择器规则：
//    [attr]           属性存在性
//    [attr=val]       精确匹配
//    [attr~=val]      空格分隔词匹配
//    [attr^=val]      前缀匹配
//    [attr$=val]      后缀匹配
//    [attr*=val]      子串匹配
//    [attr|=val]      连字符前缀匹配
//    A,B              并集
//    A B              后代
//    A > B            直接子
//    A + B            紧邻前兄弟
//    A ~ B            任意前兄弟
//    :nth-child(n)    父元素第 n 个子元素（1-based）
//    :nth-last-child(n) 父元素倒数第 n 个子元素
//
//  CMakeLists.txt 依赖：
//    find_package(Qt5 REQUIRED COMPONENTS Qml Quick)
//    target_link_libraries(... Qt5::Qml Qt5::Quick)
// ════════════════════════════════════════════════════════════════
class QmlQuerySelector : public QObject {
    Q_OBJECT

public:
    explicit QmlQuerySelector(QObject* parent = nullptr);

    // 调试接口（测试用）：返回解析后的 SelectorChain 的可读文本
    QString debugParsePublic(const QString& selector);

    // error 非空时，失败原因写入 *error；成功时清空 *error。
    // 传 nullptr 时与旧调用代码完全兼容。
    QObject*        querySelector   (QObject* root, const QString& selector,
                                     QString* error = nullptr, bool debug = false);
    QList<QObject*> querySelectorAll(QObject* root, const QString& selector,
                                     QString* error = nullptr, bool debug = false);

    void clearCache() { parseCache_.clear(); }

private:
    // ── 解析 ──────────────────────────────────────────────────
    SelectorChain parse(const QString& selector);

    // Token 语法解析器（手写递归下降，解析失败抛 SelectorParseError）
    SelectorToken parseToken(const QString& src, int& pos);

    void          parseAttrSelector(const QString& src, int& pos, SelectorToken& token);
    void          parsePseudoClass (const QString& src, int& pos, SelectorToken& token);
    QString       parseIdent       (const QString& src, int& pos);
    QString       parseAttrValue   (const QString& src, int& pos);
    QString       parseAttrOp      (const QString& src, int& pos);
    int           parseInteger     (const QString& src, int& pos);
    void          skipSpaces       (const QString& src, int& pos);
    void          expect           (const QString& src, int& pos, QChar ch);

    // ── 匹配 ──────────────────────────────────────────────────
    bool matchToken   (QObject* obj, const SelectorToken& token) const;
    bool matchChainRTL(QObject* obj, const SelectorChain& chain, int idx) const;

    // ── 遍历 ──────────────────────────────────────────────────
    void collectAll(QObject* node,
                    const SelectorChain& chain,
                    QList<QObject*>& results,
                    bool stopAtFirst);
    void collectOrdered(QObject* node,
                        const QSet<QObject*>& matched,
                        QList<QObject*>& ordered);

    // ── 布隆过滤器 ────────────────────────────────────────────
    BloomFilter buildBloom(QObject* node) const;

    // ── 节点访问（双轨策略）──────────────────────────────────
    QList<QObject*> visualChildren  (QObject* obj) const;
    QList<QObject*> declaredChildren(QObject* obj) const;
    QList<QObject*> siblings        (QObject* obj, bool onlyPreceding) const;

    // ── 元对象工具 ────────────────────────────────────────────
    QString  resolveTypeName(QObject* obj) const;
    QVariant property       (QObject* obj, const QString& name) const;

    // ── 调试 ──────────────────────────────────────────────────
    void debugTree(QObject* node, int depth) const;

    // visualParent — 优先查 parentMap_，回退到 QObject::parent()
    QObject* visualParent(QObject* obj) const;
    // buildParentMap — 以 root 为根 DFS 遍历，填充 parentMap_
    void buildParentMap(QObject* node, QObject* parent);

    // 调试：返回解析后的 SelectorChain 的可读文本（仅供测试）
    QString debugParse(const QString& selector);

    // ── 解析缓存 ──────────────────────────────────────────────
    QHash<QString, SelectorChain> parseCache_;
    mutable QHash<QObject*, QObject*> parentMap_;
};


inline void log(const char *tag, const QString &msg)
{
    // 1. C++11 保证了 static 局部变量在多线程环境下只会被初始化一次。
    // 这在纯头文件中是替代 Q_GLOBAL_STATIC 的最安全做法。
    static QMutex mutex; 

    // 静态文件指针：只会在第一次执行时初始化
    static struct LogFile {
        QFile file;
        QTextStream stream;
        LogFile() {
            file.setFileName(QStringLiteral("debug.log"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                stream.setDevice(&file);
            }
        }
        ~LogFile() {
            if (file.isOpen()) file.close();
        }
    } logStorage;

    if (logStorage.file.isOpen()) {
        logStorage.stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                          << " [" << tag << "] " << msg << "\n";
        // 强制刷新缓冲区，确保实时写入磁盘
        logStorage.stream.flush();
    }
}

#define LOG(tag, msg) log(tag, msg)
