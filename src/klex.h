//
// Created by Helix on 2026/1/10.
//

#ifndef KORELIN_KLEX_H
#define KORELIN_KLEX_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Korelin 语言的 Token 类型枚举
 * 定义了语言中所有的词法单元类型，包括特殊标记、标识符、字面量、运算符、分隔符和关键字。
 */
typedef enum {
    // --- 特殊 Token ---
    KORELIN_TOKEN_EOF,      // 文件结束标记 (End Of File)
    KORELIN_TOKEN_ERROR,    // 词法错误标记 (用于指示非法的字符或序列)

    // --- 标识符和字面量 ---
    KORELIN_TOKEN_IDENT,    // 标识符 (变量名、函数名等)
    KORELIN_TOKEN_STRING,   // 字符串字面量 (例如 "hello")
    KORELIN_TOKEN_KEYWORD_STRING, // 字符串类型关键字 (string)
    KORELIN_TOKEN_INT,      // 整型字面量 (例如 123)
    KORELIN_TOKEN_FLOAT,    // 浮点型字面量 (例如 3.14)
    KORELIN_TOKEN_BOOL,     // 布尔类型标识 (作为类型关键字使用)
    KORELIN_TOKEN_VOID,     // Void类型标识 (作为类型关键字使用)

    // --- 运算符 ---
    KORELIN_TOKEN_ASSIGN,   // 赋值运算符 (=)
    KORELIN_TOKEN_ADD,      // 加法运算符 (+)
    KORELIN_TOKEN_SUB,      // 减法运算符 (-)
    KORELIN_TOKEN_MUL,      // 乘法运算符 (*)
    KORELIN_TOKEN_DIV,      // 除法运算符 (/)
    KORELIN_TOKEN_MOD,      // 取模运算符 (%)

    // --- 一元自增/自减 ---
    KORELIN_TOKEN_INC,      // 自增运算符 (++)
    KORELIN_TOKEN_DEC,      // 自减运算符 (--)

    // --- 比较运算符 ---
    KORELIN_TOKEN_LT,        // 小于 (<)
    KORELIN_TOKEN_GT,        // 大于 (>)
    KORELIN_TOKEN_LE,       // 小于等于 (<=)
    KORELIN_TOKEN_GE,       // 大于等于 (>=)
    KORELIN_TOKEN_EQ,       // 等于 (==)
    KORELIN_TOKEN_NE,       // 不等于 (!=)

    // --- 复合赋值运算符 (对应基础运算符) ---
    KORELIN_TOKEN_ADD_ASSIGN,  // 加法赋值 (+=)
    KORELIN_TOKEN_SUB_ASSIGN,  // 减法赋值 (-=)
    KORELIN_TOKEN_MUL_ASSIGN,  // 乘法赋值 (*=)
    KORELIN_TOKEN_DIV_ASSIGN,  // 除法赋值 (/=)
    KORELIN_TOKEN_MOD_ASSIGN,  // 取模赋值 (%=)

    // --- 逻辑运算符 ---
    KORELIN_TOKEN_AND,      // 逻辑与 (&&)
    KORELIN_TOKEN_OR,       // 逻辑或 (||)
    KORELIN_TOKEN_NOT,      // 逻辑非 (!)

    // --- 分隔符 ---
    KORELIN_TOKEN_COMMA,     // 逗号 (,)
    KORELIN_TOKEN_SEMICOLON, // 分号 (;)
    KORELIN_TOKEN_COLON,     // 冒号 (:)
    KORELIN_TOKEN_SCOPE,     // 作用域解析符 (::)
    KORELIN_TOKEN_DOT,       // 点 (.) - 用于成员访问
    KORELIN_TOKEN_AT,        // At符号 (@) - 用于注解/装饰器

    KORELIN_TOKEN_LPAREN,    // 左圆括号 (()
    KORELIN_TOKEN_RPAREN,    // 右圆括号 ())
    KORELIN_TOKEN_LBRACKET,  // 左方括号 ([)
    KORELIN_TOKEN_RBRACKET,  // 右方括号 (])
    KORELIN_TOKEN_LBRACE,    // 左花括号 ({)
    KORELIN_TOKEN_RBRACE,    // 右花括号 (})
    KORELIN_TOKEN_LANGLE,    // 左尖括号 (<) - 作为泛型分隔符等
    KORELIN_TOKEN_RANGLE,    // 右尖括号 (>) - 作为泛型分隔符等

    // --- 关键字 ---
    KORELIN_TOKEN_LET,       // let - 变量声明 (通常不可变或局部推导，视语言设计而定)
    KORELIN_TOKEN_FUNCTION,  // function - 函数声明
    KORELIN_TOKEN_VAR,       // var - 变量声明 (可变)
    KORELIN_TOKEN_CONST,     // const - 常量声明
    KORELIN_TOKEN_IF,        // if - 条件语句
    KORELIN_TOKEN_ELSE,      // else - 条件分支
    KORELIN_TOKEN_FOR,       // for - 循环语句
    KORELIN_TOKEN_WHILE,     // while - 循环语句
    KORELIN_TOKEN_DO,        // do - do-while循环
    KORELIN_TOKEN_RETURN,    // return - 返回语句
    KORELIN_TOKEN_TRY,       // try - 异常处理
    KORELIN_TOKEN_CATCH,     // catch - 异常捕获
    KORELIN_TOKEN_IMPORT,    // import - 模块导入
    // KORELIN_TOKEN_STRUCT, // Removed duplicate
    // KORELIN_TOKEN_MAP,    // Removed duplicate
    KORELIN_TOKEN_TRUE,      // true - 布尔真字面量
    KORELIN_TOKEN_FALSE,     // false - 布尔假字面量
    KORELIN_TOKEN_NIL,       // nil - 空值/空指针字面量
    KORELIN_TOKEN_BREAK,     // break - 跳出循环
    KORELIN_TOKEN_CONTINUE,  // continue - 继续下一次循环
    KORELIN_TOKEN_SWITCH,    // switch - 多路分支语句
    KORELIN_TOKEN_CASE,      // case - 分支情况
    KORELIN_TOKEN_DEFAULT,   // default - 默认分支
    KORELIN_TOKEN_CLASS,     // class
    KORELIN_TOKEN_STRUCT,    // struct
    KORELIN_TOKEN_MAP,       // Map - 泛型Map
    KORELIN_TOKEN_PUBLIC,    // public - 访问修饰符
    KORELIN_TOKEN_PRIVATE,   // private - 访问修饰符
    KORELIN_TOKEN_PROTECTED, // protected - 访问修饰符
    KORELIN_TOKEN_EXTENDS,   // extends - 继承
    KORELIN_TOKEN_SUPER,     // super - 父类引用
    KORELIN_TOKEN_NEW,       // new - 创建对象
} KorelinToken;

/**
 * @brief Token 结构体
 * 表示源代码中的一个词法单元。
 */
typedef struct {
    KorelinToken type;  // Token 的类型
    const char* value;  // Token 的字面值 (字符串)
    size_t length;      // 值的长度
    int line;           // Token 所在的行号
    int column;         // Token 所在的列号
} Token;

/**
 * @brief 词法分析器 (Lexer) 结构体
 * 维护词法分析过程中的状态。
 */
typedef struct {
    const char* input;    // 源代码输入字符串
    size_t position;      // 当前正在检查的字符的索引 (指向 current_char)
    size_t read_position; // 下一个要检查的字符的索引 (用于前瞻)
    char current_char;    // 当前正在检查的字符
    int line;             // 当前行号
    int column;           // 当前列号
} Lexer;

// --- 函数声明 ---

/**
 * @brief 初始化一个新的 Lexer 实例
 * 
 * @param lexer 指向需要初始化的 Lexer 结构体的指针
 * @param input 源代码字符串 (必须以 null 结尾)
 */
void init_lexer(Lexer* lexer, const char* input);

/**
 * @brief 从 Lexer 中获取下一个 Token
 * 
 * 此函数会推进 Lexer 的状态，并返回解析出的下一个 Token。
 * 调用者负责在不再需要 Token 时调用 free_token 释放相关内存。
 * 
 * @param lexer 指向 Lexer 实例的指针
 * @return Token 解析出的 Token
 */
Token next_token(Lexer* lexer);

/**
 * @brief 释放 Token 中动态分配的内存
 * 
 * 某些 Token (如标识符、字符串字面量) 的 value 字段是动态分配的。
 * 此函数用于释放这些内存，防止内存泄漏。
 * 
 * @param token 指向需要释放的 Token 的指针
 */
void free_token(Token* token);

#endif //KORELIN_KLEX_H
