#ifndef KORELIN_KLEX_H
#define KORELIN_KLEX_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Korelin 語言的 Token 類型枚舉
 * 定義了語言中所有的詞法單元類型，包括特殊標記、標識符、字面量、運算符、分隔符和關鍵字。
 */
typedef enum {
    /** @brief --- 特殊 Token --- */
    KORELIN_TOKEN_EOF,      /** @brief 文件結束標記 (End Of File) */
    KORELIN_TOKEN_ERROR,    /** @brief 詞法錯誤標記 (用於指示非法的字符或序列) */

    /** @brief --- 標識符和字面量 --- */
    KORELIN_TOKEN_IDENT,    /** @brief 標識符 (變量名、函數名等) */
    KORELIN_TOKEN_STRING,   /** @brief 字符串字面量 (例如 "hello") */
    KORELIN_TOKEN_KEYWORD_STRING, /** @brief 字符串類型關鍵字 (string) */
    KORELIN_TOKEN_INT,      /** @brief 整型字面量 (例如 123) */
    KORELIN_TOKEN_FLOAT,    /** @brief 浮點型字面量 (例如 3.14) */
    KORELIN_TOKEN_BOOL,     /** @brief 布爾類型標識 (作為類型關鍵字使用) */
    KORELIN_TOKEN_VOID,     /** @brief Void類型標識 (作為類型關鍵字使用) */

    /** @brief --- 運算符 --- */
    KORELIN_TOKEN_ASSIGN,   /** @brief 賦值運算符 (=) */
    KORELIN_TOKEN_ADD,      /** @brief 加法運算符 (+) */
    KORELIN_TOKEN_SUB,      /** @brief 減法運算符 (-) */
    KORELIN_TOKEN_MUL,      /** @brief 乘法運算符 (*) */
    KORELIN_TOKEN_DIV,      /** @brief 除法運算符 (/) */
    KORELIN_TOKEN_MOD,      /** @brief 取模運算符 (%) */

    /** @brief --- 一元自增/自減 --- */
    KORELIN_TOKEN_INC,      /** @brief 自增運算符 (++) */
    KORELIN_TOKEN_DEC,      /** @brief 自減運算符 (--) */

    /** @brief --- 比較運算符 --- */
    KORELIN_TOKEN_LT,        /** @brief 小於 (<) */
    KORELIN_TOKEN_GT,        /** @brief 大於 (>) */
    KORELIN_TOKEN_LE,       /** @brief 小於等於 (<=) */
    KORELIN_TOKEN_GE,       /** @brief 大於等於 (>=) */
    KORELIN_TOKEN_EQ,       /** @brief 等於 (==) */
    KORELIN_TOKEN_NE,       /** @brief 不等於 (!=) */

    /** @brief --- 複合賦值運算符 (對應基礎運算符) --- */
    KORELIN_TOKEN_ADD_ASSIGN,  /** @brief 加法賦值 (+=) */
    KORELIN_TOKEN_SUB_ASSIGN,  /** @brief 減法賦值 (-=) */
    KORELIN_TOKEN_MUL_ASSIGN,  /** @brief 乘法賦值 (*=) */
    KORELIN_TOKEN_DIV_ASSIGN,  /** @brief 除法賦值 (/=) */
    KORELIN_TOKEN_MOD_ASSIGN,  /** @brief 取模賦值 (%=) */

    /** @brief --- 邏輯運算符 --- */
    KORELIN_TOKEN_AND,      /** @brief 邏輯與 (&&) */
    KORELIN_TOKEN_OR,       /** @brief 邏輯或 (||) */
    KORELIN_TOKEN_NOT,      /** @brief 邏輯非 (!) */

    /** @brief --- 分隔符 --- */
    KORELIN_TOKEN_COMMA,     /** @brief 逗號 (,) */
    KORELIN_TOKEN_SEMICOLON, /** @brief 分號 (;) */
    KORELIN_TOKEN_COLON,     /** @brief 冒號 (:) */
    KORELIN_TOKEN_SCOPE,     /** @brief 作用域解析符 (::) */
    KORELIN_TOKEN_DOT,       /** @brief 點 (.) - 用於成員訪問 */
    KORELIN_TOKEN_AT,        /** @brief At符號 (@) - 用於註解/裝飾器 */

    KORELIN_TOKEN_LPAREN,    /** @brief 左圓括號 (() */
    KORELIN_TOKEN_RPAREN,    /** @brief 右圓括號 ()) */
    KORELIN_TOKEN_LBRACKET,  /** @brief 左方括號 ([) */
    KORELIN_TOKEN_RBRACKET,  /** @brief 右方括號 (]) */
    KORELIN_TOKEN_LBRACE,    /** @brief 左花括號 ({) */
    KORELIN_TOKEN_RBRACE,    /** @brief 右花括號 (}) */
    KORELIN_TOKEN_LANGLE,    /** @brief 左尖括號 (<) - 作為泛型分隔符等 */
    KORELIN_TOKEN_RANGLE,    /** @brief 右尖括號 (>) - 作為泛型分隔符等 */

    /** @brief --- 關鍵字 --- */
    KORELIN_TOKEN_LET,       /** @brief let - 變量聲明 (通常不可變或局部推導，視語言設計而定) */
    KORELIN_TOKEN_FUNCTION,  /** @brief function - 函數聲明 */
    KORELIN_TOKEN_VAR,       /** @brief var - 變量聲明 (可變) */
    KORELIN_TOKEN_CONST,     /** @brief const - 常量聲明 */
    KORELIN_TOKEN_IF,        /** @brief if - 條件語句 */
    KORELIN_TOKEN_ELSE,      /** @brief else - 條件分支 */
    KORELIN_TOKEN_FOR,       /** @brief for - 循環語句 */
    KORELIN_TOKEN_WHILE,     /** @brief while - 循環語句 */
    KORELIN_TOKEN_DO,        /** @brief do - do-while循環 */
    KORELIN_TOKEN_RETURN,    /** @brief return - 返回語句 */
    KORELIN_TOKEN_TRY,       /** @brief try - 異常處理 */
    KORELIN_TOKEN_CATCH,     /** @brief catch - 異常捕獲 */
    KORELIN_TOKEN_THROW,     /** @brief throw - 拋出異常 */
    KORELIN_TOKEN_IMPORT,    /** @brief import - 模塊導入 */
    
    KORELIN_TOKEN_TRUE,      /** @brief true - 布爾真字面量 */
    KORELIN_TOKEN_FALSE,     /** @brief false - 布爾假字面量 */
    KORELIN_TOKEN_NIL,       /** @brief nil - 空值/空指針字面量 */
    KORELIN_TOKEN_BREAK,     /** @brief break - 跳出循環 */
    KORELIN_TOKEN_CONTINUE,  /** @brief continue - 繼續下一次循環 */
    KORELIN_TOKEN_SWITCH,    /** @brief switch - 多路分支語句 */
    KORELIN_TOKEN_CASE,      /** @brief case - 分支情況 */
    KORELIN_TOKEN_DEFAULT,   /** @brief default - 默認分支 */
    KORELIN_TOKEN_CLASS,     /** @brief class */
    KORELIN_TOKEN_MAP,       /** @brief Map - 泛型Map */
    KORELIN_TOKEN_PUBLIC,    /** @brief public - 訪問修飾符 */
    KORELIN_TOKEN_PRIVATE,   /** @brief private - 訪問修飾符 */
    KORELIN_TOKEN_PROTECTED, /** @brief protected - 訪問修飾符 */
    KORELIN_TOKEN_EXTENDS,   /** @brief extends - 繼承 */
    KORELIN_TOKEN_SUPER,     /** @brief super - 父類引用 */
    KORELIN_TOKEN_NEW,       /** @brief new - 創建對象 */
} KorelinToken;

/**
 * @brief Token 結構體
 * 表示源代碼中的一個詞法單元。
 */
typedef struct {
    KorelinToken type;  /** @brief Token 的類型 */
    const char* value;  /** @brief Token 的字面值 (字符串) */
    size_t length;      /** @brief 值的長度 */
} Token;

/**
 * @brief 詞法分析器 (Lexer) 結構體
 * 維護詞法分析過程中的狀態。
 */
typedef struct {
    const char* input;    /** @brief 源代碼輸入字符串 */
    size_t position;      /** @brief 當前正在檢查的字符的索引 (指向 current_char) */
    size_t read_position; /** @brief 下一個要檢查的字符的索引 (用於前瞻) */
    char current_char;    /** @brief 當前正在檢查的字符 */
} Lexer;

/** @brief --- 函數聲明 --- */

/**
 * @brief 初始化一個新的 Lexer 實例
 * 
 * @param lexer 指向需要初始化的 Lexer 結構體的指標
 * @param input 源代碼字符串 (必須以 null 結尾)
 */
void init_lexer(Lexer* lexer, const char* input);

/**
 * @brief 從 Lexer 中獲取下一個 Token
 * 
 * 此函數會推進 Lexer 的狀態，並返回解析出的下一個 Token。
 * 調用者負責在不再需要 Token 時調用 free_token 釋放相關內存。
 * 
 * @param lexer 指向 Lexer 實例的指標
 * @return Token 解析出的 Token
 */
Token next_token(Lexer* lexer);

/**
 * @brief 釋放 Token 中動態分配的內存
 * 
 * 某些 Token (如標識符、字符串字面量) 的 value 字段是動態分配的。
 * 此函數用於釋放這些內存，防止內存洩漏。
 * 
 * @param token 指向需要釋放的 Token 的指標
 */
void free_token(Token* token);

#endif //KORELIN_KLEX_H
