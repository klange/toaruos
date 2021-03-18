#pragma once
/**
 * @file scanner.h
 * @brief Definitions used by the token scanner.
 */

typedef enum {
	TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
	TOKEN_LEFT_SQUARE, TOKEN_RIGHT_SQUARE,
	TOKEN_COLON,
	TOKEN_COMMA,
	TOKEN_DOT,
	TOKEN_MINUS,
	TOKEN_PLUS,
	TOKEN_SEMICOLON,
	TOKEN_SOLIDUS,
	TOKEN_ASTERISK,
	TOKEN_POW,
	TOKEN_MODULO,
	TOKEN_AT,
	TOKEN_CARET,      /* ^ (xor) */
	TOKEN_AMPERSAND,  /* & (and) */
	TOKEN_PIPE,       /* | (or) */
	TOKEN_TILDE,      /* ~ (negate) */
	TOKEN_LEFT_SHIFT, /* << */
	TOKEN_RIGHT_SHIFT,/* >> */
	TOKEN_BANG,
	TOKEN_GREATER,
	TOKEN_LESS,
	TOKEN_ARROW, /* -> */
	TOKEN_WALRUS, /* := */

	/* Comparisons */
	TOKEN_GREATER_EQUAL,
	TOKEN_LESS_EQUAL,
	TOKEN_BANG_EQUAL,
	TOKEN_EQUAL_EQUAL,

	/* Assignments */
	TOKEN_EQUAL,
	TOKEN_LSHIFT_EQUAL, /* <<= */
	TOKEN_RSHIFT_EQUAL, /* >>= */
	TOKEN_PLUS_EQUAL,   /* += */
	TOKEN_MINUS_EQUAL,  /* -= */
	TOKEN_PLUS_PLUS,    /* ++ */
	TOKEN_MINUS_MINUS,  /* -- */
	TOKEN_CARET_EQUAL,
	TOKEN_PIPE_EQUAL,
	TOKEN_AMP_EQUAL,
	TOKEN_SOLIDUS_EQUAL,
	TOKEN_ASTERISK_EQUAL,
	TOKEN_POW_EQUAL,
	TOKEN_MODULO_EQUAL,

	TOKEN_STRING,
	TOKEN_BIG_STRING,
	TOKEN_NUMBER,

	/*
	 * Everything after this, up to indentation,
	 * consists of alphanumerics.
	 */
	TOKEN_IDENTIFIER,
	TOKEN_AND,
	TOKEN_CLASS,
	TOKEN_DEF,
	TOKEN_DEL,
	TOKEN_ELSE,
	TOKEN_FALSE,
	TOKEN_FOR,
	TOKEN_IF,
	TOKEN_IMPORT,
	TOKEN_IN,
	TOKEN_IS,
	TOKEN_LET,
	TOKEN_NONE,
	TOKEN_NOT,
	TOKEN_OR,
	TOKEN_ELIF,
	TOKEN_PASS,
	TOKEN_RETURN,
	TOKEN_SELF,
	TOKEN_SUPER,
	TOKEN_TRUE,
	TOKEN_WHILE,
	TOKEN_TRY,
	TOKEN_EXCEPT,
	TOKEN_RAISE,
	TOKEN_BREAK,
	TOKEN_CONTINUE,
	TOKEN_AS,
	TOKEN_FROM,
	TOKEN_LAMBDA,
	TOKEN_ASSERT,
	TOKEN_YIELD,
	TOKEN_WITH,

	TOKEN_PREFIX_B,
	TOKEN_PREFIX_F,

	TOKEN_INDENTATION,

	TOKEN_EOL,
	TOKEN_RETRY,
	TOKEN_ERROR,
	TOKEN_EOF,
} KrkTokenType;

/**
 * @brief A token from the scanner.
 *
 * Represents a single scanned item from the scanner, such as a keyword,
 * string literal, numeric literal, identifier, etc.
 */
typedef struct {
	KrkTokenType type;
	const char * start;
	size_t length;
	size_t line;
	const char * linePtr;
	size_t col;
	size_t literalWidth;
} KrkToken;

/**
 * @brief Token scanner state.
 *
 * Stores the state of the compiler's scanner, reading from a source
 * character string and tracking the current line.
 */
typedef struct {
	const char * start;
	const char * cur;
	const char * linePtr;
	size_t line;
	int startOfLine;
	int hasUnget;
	KrkToken unget;
} KrkScanner;

/**
 * @brief Initialize the compiler to scan tokens from 'src'.
 *
 * FIXME: There is currently only a single static scanner state;
 *        along with making the compiler re-entrant, the scanner
 *        needs to also be re-entrant; there's really no reason
 *        these can't all just take a KrkScanner* argument.
 */
extern void krk_initScanner(const char * src);

/**
 * @brief Read the next token from the scanner.
 *
 * FIXME: Or, maybe the scanner shouldn't even be available outside
 *        of the compiler, that would make some sense as well, as it's
 *        a low-level detail, but we use it for tab completion in the
 *        main repl, so I'm not sure that's feasible right now.
 */
extern KrkToken krk_scanToken(void);

/**
 * @brief Push a token back to the scanner to be reprocessed.
 *
 * Pushes a previously-scanned token back to the scanner.
 * Used to implement small backtracking operations at the
 * end of block constructs like 'if' and 'try'.
 */
extern void krk_ungetToken(KrkToken token);

/**
 * @brief Rewind the scanner to a previous state.
 *
 * Resets the current scanner to the state in 'to'. Used by
 * the compiler to implement comprehensions, which would otherwise
 * not be possible in a single-pass compiler.
 */
extern void krk_rewindScanner(KrkScanner to);

/**
 * @brief Retreive a copy of the current scanner state.
 *
 * Used with krk_rewindScanner() to implement rescanning
 * for comprehensions.
 */
extern KrkScanner krk_tellScanner(void);
