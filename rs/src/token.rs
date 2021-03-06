#[derive(PartialEq, Eq, Clone, Debug)]
pub enum TokenType {
    Root,
    Prog,
    ModDecl,
    Import,
    Line,
    Expr,
    Subexpr,
    ChrLit,
    StrLit,
    FnDecl,
    Parened,
    Return,
    Case,
    IfElse,
    Try,
    While,
    For,
    Lambda,
    TupleLit,
    ListLit,
    ListComp,
    DictLit,
    DictComp,
    SetLit,
    SetComp,
    QualIdent,
    NamespacedIdent,
    Ident,
    MemberIdent,
    ScopedIdent,
    TypeIdent,
    NumLit,
    Op,
    Infixed,
    Var,
    Assign,
    Pattern,
    StrChr,
    Param,
    Generator,
    RealLit,
    IntLit,
    AbsInt,
    AbsReal,
    ChrChr,
    DictEntry,
    CaseBranch,
    Equals,
    SingleQuote,
    DoubleQuote,
    ModuleKeyword,
    ExposingKeyword,
    HidingKeyword,
    ImportKeyword,
    AsKeyword,
    FnKeyword,
    CaseKeyword,
    IfKeyword,
    ElseKeyword,
    TryKeyword,
    CatchKeyword,
    WhileKeyword,
    ForKeyword,
    InKeyword,
    VarKeyword,
    NanKeyword,
    InfinityKeyword,
    ReturnKeyword,
    Dot,
    Comma,
    Colon,
    Underscore,
    LArrow,
    RArrow,
    FatRArrow,
    LParen,
    RParen,
    LSqBracket,
    RSqBracket,
    LCurlyBracket,
    RCurlyBracket,
    Backslash,
    DoubleColon,
    Minus,
    Bar,
    Backtick,
}

#[derive(Clone, Debug)]
pub struct Token {
    pub type_:  TokenType,
    pub lexeme: String,
}


impl Token {
    pub fn new(type_: TokenType, lexeme: String) -> Self {
        Token {
            type_:  type_,
            lexeme: lexeme,
        }
    }
}
