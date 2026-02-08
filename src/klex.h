#ifndef KORELIN_KLEX_H
#define KORELIN_KLEX_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Korelin 語言的 Token 類型枚舉
 * 定義了語言中所有的詞法單元類型，包括特殊標記、標識符、字面量、運算符、分隔符和關鍵字。
 */
typedef enum {
    /* --- 特殊 Token --- */
    KORELIN_TOKEN_EOF,      /**< 文件結束標記 (End Of File) */
    KORELIN_TOKEN_ERROR,    /**< 詞法錯誤標記 (用於指示非法的字符或序列) */

    /* --- 標識符和字面量 --- */
    KORELIN_TOKEN_IDENT,    /**< 標識符 (變量名、函數名等) */
    KORELIN_TOKEN_STRING,   /**< 字符串字面量 (例如 "hello") */
    KORELIN_TOKEN_KEYWORD_STRING, /**< 字符串類型關鍵字 (string) */
    KORELIN_TOKEN_INT,      /**< 整型字面量 (例如 123) */
    KORELIN_TOKEN_FLOAT,    /**< 浮點型字面量 (例如 3.14) */
    KORELIN_TOKEN_BOOL,     /**< 布爾類型標識 (作為類型關鍵字使用) */
    KORELIN_TOKEN_VOID,     /**< Void類型標識 (作為類型關鍵字使用) */

    /* --- 運算符 --- */
    KORELIN_TOKEN_ASSIGN,   /**< 賦值運算符 (=) */
    KORELIN_TOKEN_ADD,      /**< 加法運算符 (+) */
    KORELIN_TOKEN_SUB,      /**< 減法運算符 (-) */
    KORELIN_TOKEN_MUL,      /**< 乘法運算符 (*) */
    KORELIN_TOKEN_DIV,      /**< 除法運算符 (/) */
    KORELIN_TOKEN_MOD,      /**< 取模運算符 (%) */

    /* --- 一元自增/自減 --- */
    KORELIN_TOKEN_INC,      /**< 自增運算符 (++) */
    KORELIN_TOKEN_DEC,      /**< 自減運算符 (--) */

    /* --- 比較運算符 --- */
    KORELIN_TOKEN_LT,       /**< 小於 (<) */
    KORELIN_TOKEN_GT,       /**< 大於 (>) */
    KORELIN_TOKEN_LE,       /**< 小於等於 (<=) */
    KORELIN_TOKEN_GE,       /**< 大於等於 (>=) */
    KORELIN_TOKEN_EQ,       /**< 等於 (==) */
    KORELIN_TOKEN_NE,       /**< 不等於 (!=) */

    /* --- 複合賦值運算符 (對應基礎運算符) --- */
    KORELIN_TOKEN_ADD_ASSIGN,  /**< 加法賦值 (+=) */
    KORELIN_TOKEN_SUB_ASSIGN,  /**< 減法賦值 (-=) */
    KORELIN_TOKEN_MUL_ASSIGN,  /**< 乘法賦值 (*=) */
    KORELIN_TOKEN_DIV_ASSIGN,  /**< 除法賦值 (/=) */
    KORELIN_TOKEN_MOD_ASSIGN,  /**< 取模賦值 (%=) */

    /* --- 邏輯運算符 --- */
    KORELIN_TOKEN_AND,      /**< 邏輯與 (&&) */
    KORELIN_TOKEN_OR,       /**< 邏輯或 (||) */
    KORELIN_TOKEN_NOT,      /**< 邏輯非 (!) */

    /* --- 分隔符 --- */
    KORELIN_TOKEN_COMMA,     /**< 逗號 (,) */
    KORELIN_TOKEN_SEMICOLON, /**< 分號 (;) */
    KORELIN_TOKEN_COLON,     /**< 冒號 (:) */
    KORELIN_TOKEN_SCOPE,     /**< 作用域解析符 (::) */
    KORELIN_TOKEN_DOT,       /**< 點 (.) - 用於成員訪問 */
    KORELIN_TOKEN_AT,        /**< At符號 (@) - 用於注解/裝飾器 */

    KORELIN_TOKEN_LPAREN,    /**< 左圓括号 (() */
    KORELIN_TOKEN_RPAREN,    /**< 右圓括号 ()) */
    KORELIN_TOKEN_LBRACKET,  /**< 左方括号 ([) */
    KORELIN_TOKEN_RBRACKET,  /**< 右方括号 (]) */
    KORELIN_TOKEN_LBRACE,    /**< 左花括号 ({) */
    KORELIN_TOKEN_RBRACE,    /**< 右花括号 (}) */
    KORELIN_TOKEN_LANGLE,    /**< 左尖括号 (<) - 作為泛型分隔符等 */
    KORELIN_TOKEN_RANGLE,    /**< 右尖括号 (>) - 作為泛型分隔符等 */

    /* --- 關鍵字 --- */
    KORELIN_TOKEN_LET,       /**< let - 變量聲明 (通常不可變或局部推導，視語言設計而定) */
    KORELIN_TOKEN_FUNCTION,  /**< function - 函數聲明 */
    KORELIN_TOKEN_VAR,       /**< var - 變量聲明 (可變) */
    KORELIN_TOKEN_CONST,     /**< const - 常量聲明 */
    KORELIN_TOKEN_IF,        /**< if - 條件語句 */
    KORELIN_TOKEN_ELSE,      /**< else - 條件分支 */
    KORELIN_TOKEN_FOR,       /**< for - 循環語句 */
    KORELIN_TOKEN_WHILE,     /**< while - 循環語句 */
    KORELIN_TOKEN_DO,        /**< do - do-while循環 */
    KORELIN_TOKEN_RETURN,    /**< return - 返回語句 */
    KORELIN_TOKEN_TRY,       /**< try - 異常處理 */
    KORELIN_TOKEN_CATCH,     /**< catch - 異常捕獲 */
    KORELIN_TOKEN_IMPORT,    /**< import - 模塊導入 */
    
    KORELIN_TOKEN_TRUE,      /**< true - 布爾真字面量 */
    KORELIN_TOKEN_FALSE,     /**< false - 布爾假字面量 */
    KORELIN_TOKEN_NIL,       /**< nil - 空值/空指針字面量 */
    KORELIN_TOKEN_BREAK,     /**< break - 跳出循環 */
    KORELIN_TOKEN_CONTINUE,  /**< continue - 繼續下一次循環 */
    KORELIN_TOKEN_SWITCH,    /**< switch - 多路分支語句 */
    KORELIN_TOKEN_CASE,      /**< case - 分支情況 */
    KORELIN_TOKEN_DEFAULT,   /**< default - 默認分支 */
    KORELIN_TOKEN_CLASS,     /**< class */
    KORELIN_TOKEN_STRUCT,    /**< struct */
    KORELIN_TOKEN_MAP,       /**< Map - 泛型Map */
    KORELIN_TOKEN_PUBLIC,    /**< public - 訪問修飾符 */
    KORELIN_TOKEN_PRIVATE,   /**< private - 訪問修飾符 */
    KORELIN_TOKEN_PROTECTED, /**< protected - 訪問修飾符 */
    KORELIN_TOKEN_EXTENDS,   /**< extends - 繼承 */
    KORELIN_TOKEN_SUPER,     /**< super - 父類引用 */
    KORELIN_TOKEN_NEW,       /**< new - 創建對象 */
    KORELIN_TOKEN_THROW,     /**< throw - 拋出異常 */
} KorelinToken;

/**
 * @brief Token 結構體
 * 表示源代碼中的一個詞法單元。
 */
typedef struct {
    KorelinToken type;  /**< Token 的類型 */
    const char* value;  /**< Token 的字面值 (字符串) */
    size_t length;      /**< 值的長度 */
    int line;           /**< Token 所在的行號 */
    int column;         /**< Token 所在的列號 */
} Token;

/**
 * @brief 詞法分析器 (Lexer) 結構體
 * 維護詞法分析過程中的狀態。
 */
typedef struct {
    const char* input;    /**< 源代碼輸入字符串 */
    size_t position;      /**< 當前正在檢查的字符的索引 (指向 current_char) */
    size_t read_position; /**< 下一個要檢查的字符的索引 (用於前瞻) */
    char current_char;    /**< 當前正在檢查的字符 */
    int line;             /**< 當前行號 */
    int column;           /**< 當前列號 */
} Lexer;

// --- 函數聲明 ---

/**
 * @brief 初始化一個新的 Lexer 實例
 * 
 * @param lexer 指向需要初始化的 Lexer 結構體的指針
 * @param input 源代碼字符串 (必須以 null 結尾)
 */
void init_lexer(Lexer* lexer, const char* input);

/**
 * @brief 從 Lexer 中獲取下一個 Token
 * 
 * 此函數會推進 Lexer 的狀態，並返回解析出的下一個 Token。
 * 調用者負責在不再需要 Token 時調用 free_token 釋放相關內存。
 * 
 * @param lexer 指向 Lexer 實例的指針
 * @return 解析出的 Token
 */
Token next_token(Lexer* lexer);

/**
 * @brief 釋放 Token 中動態分配的內存
 * 
 * 某些 Token (如標識符、字符串字面量) 的 value 字段是動態分配的。
 * 此函數用於釋放這些內存，防止內存泄漏。
 * 
 * @param token 指向需要釋放的 Token 的指針
 */
void free_token(Token* token);

#endif //KORELIN_KLEX_H
