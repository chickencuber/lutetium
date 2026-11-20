if exists("b:current_syntax")
    unlet b:current_syntax
endif

" Lutetium syntax highlighting

setlocal commentstring=//\ %s

" Identifiers
syn match lutIdentifier /\<[a-zA-Z_][a-zA-Z0-9_]*\>/

" Keywords
syn keyword lutKeyword fn nextgroup=lutFunctionDecl,lutCallingConvention skipwhite
syn keyword lutKeyword return let else if while struct

" Extern declarations
syn match lutKeyword /\<extern\>/ nextgroup=lutCallingConvention skipwhite

" Function declarations
syn match lutFunctionDecl /\<[a-zA-Z_][a-zA-Z0-9_]*\>/ contained
" Attributes
syn match lutAttribute /@[a-zA-Z_][a-zA-Z0-9_]*/

" Function calls 
syn match lutFunctionCall /\<[a-zA-Z_][a-zA-Z0-9_]*\>\ze\s*(/

" Calling conventions
syn match lutCallingConvention /\<C\>/ contained

" Booleans
syn keyword lutBoolean true false

" Integer literals
syn match lutInteger /\<0[bB][01]\%(_\?[01]\)*\>/
syn match lutInteger /\<0[oO][0-7]\%(_\?[0-7]\)*\>/
syn match lutInteger /\<0[xX][0-9a-fA-F]\%(_\?[0-9a-fA-F]\)*\>/
syn match lutInteger /\<[0-9]\%(_\?[0-9]\)*\>/

" Types
syn match lutType /\<\(i8\|i16\|i32\|i64\|u8\|u16\|u32\|u64\|bool\|void\|cstr\)\>/

" Operators
syn match lutOperator /->\|\.\.\./
syn match lutOperator /[+*\/%=!<>|&-]/

" Delimiters
syn match lutDelimiter /[(){};:,]/

" Strings
syn region lutString start=/"/ skip=/\\\\\|\\"/ end=/"/ contains=lutEscape,lutInvalidEscape

" String escapes
syn match lutInvalidEscape /\\./ contained
syn match lutEscape /\\[ntr0\\"']/ contained

" Comments
syn match lutComment /\/\/.*/

" Highlight links
hi link lutBoolean Boolean
hi link lutKeyword Keyword
hi link lutInteger Number
hi link lutCallingConvention SpecialChar
hi link lutType Type
hi link lutIdentifier Identifier
hi link lutOperator Operator
hi link lutDelimiter Delimiter
hi link lutComment Comment
hi link lutString String
hi link lutInvalidEscape Error
hi link lutEscape SpecialChar
hi link lutFunctionDecl Function
hi link lutFunctionCall Function
hi link lutAttribute PreProc

let b:current_syntax = "lutetium"
