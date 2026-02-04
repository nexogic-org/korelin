//
// Created by Helix on 2026/1/10.
//

#ifndef KORELIN_KPARSER_H
#define KORELIN_KPARSER_H

#include "klex.h"
#include <stdbool.h>

// 错误类型定义 (参考 e:\Korelin\korelin_v100\doc\错误异常处理.md)
typedef enum {
    KORELIN_NO_ERROR = 0,
    KORELIN_ERROR_NAME_DEFINE,             // NameDefineError
    KORELIN_ERROR_BRACKET_NOT_CLOSED,      // BracketNotClosedError
    KORELIN_ERROR_MISSING_SEMICOLON,       // MissingSemicolonError
    KORELIN_ERROR_DIVISION_BY_ZERO,        // DivisionByZeroError
    KORELIN_ERROR_UNKNOWN_CHARACTER,       // UnknownCharacterError
    KORELIN_ERROR_MISSING_KEYWORD_OR_SYMBOL, // MissingKeywordOrSymbolError
    KORELIN_ERROR_ILLEGAL_ARGUMENT,        // IllegalArgumentError
    KORELIN_ERROR_INDEX_OUT_OF_BOUNDS,     // IndexOutOfBoundsError
    KORELIN_ERROR_NIL_REFERENCE,           // NilReferenceError
    KORELIN_ERROR_TYPE_MISMATCH,           // TypeMismatchError
    KORELIN_ERROR_FILE_NOT_FOUND,          // FileNotFoundError
    KORELIN_ERROR_ILLEGAL_SYNTAX,          // IllegalSyntaxError
    KORELIN_ERROR_KEYWORD_AS_IDENTIFIER,   // KeywordAsIdentifierError
    KORELIN_ERROR_INVALID_TYPE_POSITION    // InvalidTypePositionError
} KorelinErrorType;

// --- AST 节点类型定义 ---

typedef enum {
    // 表达式
    KAST_NODE_LITERAL,      // 字面量 (整数, 浮点数, 字符串, bool, nil)
    KAST_NODE_IDENTIFIER,   // 标识符 (变量名)
    
    // 语句
    KAST_NODE_VAR_DECL,     // 变量/常量声明 (var/let + const)
    KAST_NODE_ASSIGNMENT,   // 赋值语句 (ident = expr)
    KAST_NODE_BLOCK,        // 代码块 ({ stmt; ... })
    KAST_NODE_TRY_CATCH,    // try-catch 语句
    KAST_NODE_THROW,        // throw 语句
    KAST_NODE_IF,           // if-else 语句
    KAST_NODE_SWITCH,       // switch-case 语句
    KAST_NODE_FOR,          // for 循环
    KAST_NODE_WHILE,        // while 循环
    KAST_NODE_DO_WHILE,     // do-while 循环
    KAST_NODE_RETURN,       // return 语句 (新增)
    KAST_NODE_BREAK,        // break 语句
    KAST_NODE_CONTINUE,     // continue 语句
    KAST_NODE_IMPORT,       // import 语句 (包管理)
    KAST_NODE_FUNCTION_DECL,// 函数声明 (新语法)
    
    // OOP 相关
    KAST_NODE_CLASS_DECL,   // 类声明
    KAST_NODE_STRUCT_DECL,  // 结构体声明
    KAST_NODE_MEMBER_DECL,  // 类/结构体成员声明成员声明 (属性或方法)
    KAST_NODE_NEW,          // new 表达式
    KAST_NODE_MEMBER_ACCESS,// 成员访问 (.)
    KAST_NODE_SCOPE_ACCESS, // 作用域访问 (::)
    KAST_NODE_CALL,         // 函数/方法调用
    KAST_NODE_ARRAY_ACCESS, // 数组访问 (arr[index])

    // 表达式扩展
    KAST_NODE_BINARY_OP,    // 二元操作符 (&&, ||, ==, <, +, -, etc.)
    KAST_NODE_UNARY_OP,     // 一元操作符 (!, -)
    KAST_NODE_POSTFIX_OP,   // 后缀操作符 (++, --)
    
    // 程序
    KAST_NODE_PROGRAM       // 根节点
} KastNodeType;

// --- AST 节点结构体前置声明 ---

typedef struct KastNode KastNode;
typedef struct KastExpression KastExpression;
typedef struct KastStatement KastStatement;

// --- 基础节点 ---

struct KastNode {
    KastNodeType type;
};

// --- 表达式节点 ---

struct KastExpression {
    KastNode base;
};

// 二元操作符节点
// 对应语法: left op right
typedef struct {
    KastNode base;
    KorelinToken operator; // 操作符 Token (AND, OR, EQ, PLUS, etc.)
    KastNode* left;
    KastNode* right;
} KastBinaryOp;

// 一元操作符节点
// 对应语法: op right
typedef struct {
    KastNode base;
    KorelinToken operator; // 操作符 Token (!, -)
    KastNode* operand;
} KastUnaryOp;

// 后缀操作符节点
// 对应语法: left op
typedef struct {
    KastNode base;
    KorelinToken operator; // 操作符 Token (++, --)
    KastNode* operand;
} KastPostfixOp;

// 字面量节点
typedef struct {
    KastNode base;
    Token token; // 存储字面量的 Token (包含类型和值)
} KastLiteral;

// 标识符节点
typedef struct {
    KastNode base;
    char* name; // 变量名
} KastIdentifier;

// --- 语句节点 ---

struct KastStatement {
    KastNode base;
};

// If 语句节点
// 对应语法: if (cond) { ... } [else { ... }]
typedef struct {
    KastNode base;
    KastNode* condition;      // 条件表达式
    KastStatement* then_branch; // if 分支 (通常是 Block)
    KastStatement* else_branch; // else 分支 (Block 或 If 用于 else-if)，可为 NULL
} KastIf;

// Switch Case 结构 (辅助)
typedef struct {
    KastNode* value;    // case 值 (字面量)
    KastStatement* body; // case 执行体 (Block 或 Statement list，通常以 break 结束)
    // 注意：这里简化处理，body 包含直到下一个 case/default/end 的所有语句
    // 或者我们遵循标准 AST，case 是一个标签，statement list 属于 switch block。
    // 为了简化实现，这里假设 body 是一个 Block 或者语句序列。
    // 按照 C 风格，case 只是标签。但为了 AST 结构化，我们将 case 视为包含语句块的结构。
    // 如果 body 是 NULL，表示 fallthrough (虽然 Korelin 文档建议 break，但语法上可能允许)
} KastCase;

// Switch 语句节点
// 对应语法: switch (expr) { case v: ... default: ... }
typedef struct {
    KastNode base;
    KastNode* condition;       // switch 表达式
    KastCase* cases;           // case 数组
    size_t case_count;
    KastStatement* default_branch; // default 分支，可为 NULL
} KastSwitch;

// For 循环节点
// 对应语法: for (init; condition; increment) { ... }
typedef struct {
    KastNode base;
    KastStatement* init;      // 初始化语句 (var decl or expr stmt)，可能为 NULL
    KastNode* condition;      // 条件表达式，可能为 NULL (无限循环)
    KastNode* increment;      // 增量表达式，可能为 NULL
    KastStatement* body;      // 循环体
} KastFor;

// While 循环节点
// 对应语法: while (condition) { ... }
typedef struct {
    KastNode base;
    KastNode* condition;      // 条件表达式
    KastStatement* body;      // 循环体
} KastWhile;

// Do-While 循环节点
// 对应语法: do { ... } while (condition);
typedef struct {
    KastNode base;
    KastStatement* body;      // 循环体
    KastNode* condition;      // 条件表达式
} KastDoWhile;

// Return 语句节点
// 对应语法: return [expr] ;
typedef struct {
    KastNode base;
    KastNode* value; // 返回值，可能为 NULL
} KastReturn;

// Break 语句节点
typedef struct {
    KastNode base;
} KastBreak;

// Continue 语句节点
typedef struct {
    KastNode base;
} KastContinue;

// Import 语句节点
// 对应语法: import path.to.package;
typedef struct {
    KastNode base;
    char** path_parts;     // 路径部分数组 (e.g. ["std", "io", "file"])
    size_t part_count;     // 路径部分数量
    char* alias;           // 别名/最终引入名 (e.g. "file")
    bool is_wildcard;      // 是否是 .* (暂未实现，预留)
} KastImport;

// 前置声明
typedef struct KastVarDecl KastVarDecl;
typedef struct KastBlock KastBlock;

// 访问修饰符
typedef enum {
    KAST_ACCESS_PUBLIC,
    KAST_ACCESS_PRIVATE,
    KAST_ACCESS_PROTECTED,
    KAST_ACCESS_DEFAULT // 默认 (通常是 public 或 internal，视设计而定)
} KastAccessModifier;

// 类成员类型
typedef enum {
    KAST_MEMBER_PROPERTY,
    KAST_MEMBER_METHOD
} KastMemberType;

// 类成员节点 (属性或方法)
typedef struct {
    KastNode base;
    KastMemberType member_type;
    KastAccessModifier access;
    bool is_static;
    bool is_constant; // const 修饰符
    char* name;
    
    // 属性特有
    char* type_name;      // 属性类型
    KastNode* init_value; // 初始值
    
    // 方法特有
    char* return_type;    // 返回类型 (构造函数为 NULL 或 void)
    KastNode** args;      // 参数列表 (更改为 KastNode** 以匹配 parse_parameter_list 返回类型，虽然实际存储的是 KastVarDecl*)
    size_t arg_count;     // 参数数量 (原名为 param_count，统一为 arg_count)
    KastBlock* body;      // 方法体
    
    // 泛型支持
    char** generic_params; // 泛型参数列表
    size_t generic_count;
} KastClassMember;

// 函数声明节点
// 对应语法: [Type] Name [Generics] (Params) Body
typedef struct {
    KastNode base;
    char* name;
    char* return_type;
    KastNode** args;
    size_t arg_count;
    KastBlock* body;
    // 泛型支持
    char** generic_params;
    size_t generic_count;
    char* parent_class_name; // For method definition outside class
    KastAccessModifier access; // Access modifier
} KastFunctionDecl;

// 结构体声明节点
// 对应语法: struct Name { ... } [varName];
typedef struct {
    KastNode base;
    char* name;
    KastClassMember** members; // 复用 KastClassMember，但 struct 成员默认 public 且通常只有属性
    size_t member_count;
    KastStatement* init_var; // 可选的内联变量声明 (例如: struct S { ... } s;)
} KastStructDecl;

// 类声明节点
// 对应语法: class ClassName [extends ParentName] { ... }
typedef struct {
    KastNode base;
    char* name;
    char* parent_name; // 父类名称 (NULL 表示无继承)
    KastClassMember** members;
    size_t member_count;
    // Generics
    char** generic_params;
    size_t generic_count;
} KastClassDecl;

// New 表达式节点
// 对应语法: new ClassName(args)
typedef struct {
    KastNode base;
    char* class_name;
    bool is_array; // 新增
    KastNode** args;
    size_t arg_count;
} KastNew;

// 成员访问节点
// 对应语法: obj.member
typedef struct {
    KastNode base;
    KastNode* object;
    char* member_name;
} KastMemberAccess;

// 作用域访问节点 (静态访问)
// 对应语法: Class::member
typedef struct {
    KastNode base;
    char* class_name;
    char* member_name;
} KastScopeAccess;

// 函数/方法调用节点
// 对应语法: expr(args)
typedef struct {
    KastNode base;
    KastNode* callee; // 被调用者 (Identifier, MemberAccess, etc.)
    KastNode** args;
    size_t arg_count;
} KastCall;

// 数组访问节点
// 对应语法: array[index]
typedef struct {
    KastNode base;
    KastNode* array; // 数组表达式
    KastNode* index; // 索引表达式
} KastArrayAccess;

// 变量/常量声明节点
// 对应语法: (var|let) [const] [type] ident [= expr]
struct KastVarDecl {
    KastNode base;
    bool is_global;     // true: let, false: var
    bool is_constant;   // true: const
    char* type_name;    // 显式类型，可为 NULL
    bool is_array;      // 是否为数组类型 (例如 var int a[])
    char* name;
    KastNode* init_value; // 可为 NULL
};

// 赋值语句节点
// 对应语法: lvalue = expr ;
typedef struct {
    KastNode base;
    KastNode* lvalue; // 左值 (Identifier, MemberAccess)
    KastNode* value;    // 表达式
} KastAssignment;

// 代码块节点
struct KastBlock {
    KastNode base;
    KastStatement** statements;
    size_t statement_count;
};

// Catch 块结构 (辅助结构，非 AST 节点)
typedef struct {
    char* error_type;      // 捕获的错误类型名
    char* variable_name;   // 捕获的变量名 (可选)
    KastBlock* body;       // 处理代码块
} KorelinCatchBlock;

// Try-Catch 语句节点
// 对应语法: try { ... } catch (Error) { ... }
typedef struct {
    KastNode base;
    KastBlock* try_block;          // try 代码块
    KorelinCatchBlock* catch_blocks; // catch 块数组
    size_t catch_count;
} KastTryCatch;

// Throw 语句节点
typedef struct {
    KastNode base;
    KastNode* value; // 抛出的表达式
} KastThrow;

// 程序根节点 (包含一系列语句)
typedef struct {
    KastNode base;
    KastStatement** statements; // 语句数组
    size_t statement_count;
} KastProgram;

// --- Parser 结构体定义 ---

typedef struct {
    Lexer* lexer;
    Token previous_token; // Previous token for better error reporting
    Token current_token;
    Token peek_token;
    Token peek_token_2; // Lookahead 2
    Token peek_token_3; // Lookahead 3
    Token peek_token_4; // Lookahead 4
    Token peek_token_5; // Lookahead 5
    bool has_error;               // 是否发生错误
    bool panic_mode;              // 是否处于恐慌模式 (用于错误恢复)
    KorelinErrorType error_type;  // 错误类型
    char error_message[256];      // 错误信息
    bool has_main_function;       // 是否已解析主函数
} Parser;

// --- 函数声明 ---

// 初始化 Parser
void init_parser(Parser* parser, Lexer* lexer);

// 解析程序 (入口)
KastProgram* parse_program(Parser* parser);

// 获取错误名称字符串
const char* get_error_name(KorelinErrorType type);

// 释放 AST 内存
void free_ast_node(KastNode* node);

#endif //KORELIN_KPARSER_H