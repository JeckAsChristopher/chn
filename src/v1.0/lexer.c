

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "lexer.h"
#include "error.h"
#include <ctype.h>

static inline bool is_digit(char c)  { return c>='0'&&c<='9'; }
static inline bool is_alpha(char c)  { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static inline bool is_alnum(char c)    { return is_alpha(c)||is_digit(c); }
static inline bool is_hex_digit_(char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
static inline bool at_end(Lexer *L)  { return *L->current=='\0'; }
static inline char adv(Lexer *L){
    char c = *L->current++;
    if(c == '\n'){ L->col = 1; } else { L->col++; }
    return c;
}
static inline char pk(Lexer *L)      { return *L->current; }
static inline char pk2(Lexer *L)     { return at_end(L)?'\0':L->current[1]; }
static inline bool mat(Lexer *L,char e){
    if(at_end(L)||*L->current!=e) return false;
    L->current++; return true;
}

static Token mktok(Lexer *L, TokenKind k){
    Token t={0}; t.kind=k; t.start=L->start;
    t.length=(int)(L->current-L->start); t.line=L->line;
    t.col = L->col - t.length;
    if(t.col < 1) t.col = 1;
    return t;
}
static Token errtok(Lexer *L, const char *msg){
    error_lex(L->line, L->col, 1,"%s",msg);
    Token t={0}; t.kind=TK_ERROR; t.start=msg;
    t.length=(int)strlen(msg); t.line=L->line; return t;
}

static void skip_ws(Lexer *L){
    for(;;){
        char c=pk(L);
        switch(c){
            case ' ': case '\r': case '\t': adv(L); break;
            case '\n': return; 
            case '-':
                if(pk2(L)=='-'){
                    while(!at_end(L)&&pk(L)!='\n') adv(L);
                } else return;
                break;
            case '/':
                if(pk2(L)=='/'){ 
                    while(!at_end(L)&&pk(L)!='\n') adv(L);
                } else if(pk2(L)=='*'){ 
                    adv(L); adv(L); 
                    int comment_line = L->line;
                    while(!at_end(L)){
                        if(pk(L)=='*'&&pk2(L)=='/'){ adv(L); adv(L); goto block_done; }
                        if(pk(L)=='\n'){ L->line++; L->col=1; }
                        adv(L);
                    }
                    error_lex(comment_line, 1, 2,
                        "unterminated block comment: missing closing '*/'");
                    block_done:;
                } else return;
                break;
            default: return;
        }
    }
}

static int process_escape(Lexer *L, char *buf, int pos){
    char e=adv(L);
    switch(e){
        case 'n':  buf[pos++]='\n'; break;
        case 't':  buf[pos++]='\t'; break;
        case 'r':  buf[pos++]='\r'; break;
        case '"':  buf[pos++]='"';  break;
        case '\'': buf[pos++]='\''; break;
        case '\\': buf[pos++]='\\'; break;
        case 'e':  buf[pos++]='\x1b'; break;
        case '0':   break;
        case 'a':  buf[pos++]='\a'; break;
        case 'b':  buf[pos++]='\b'; break;
        case 'f':  buf[pos++]='\f'; break;
        case 'v':  buf[pos++]='\v'; break;
        case 'x':{ 
            if(at_end(L) || !is_hex_digit_(pk(L))){
                error_lex(L->line, L->col, 1,
                    "invalid \\x escape: expected two hex digits (e.g. \\x41)");
                buf[pos++]='?';
                break;
            }
            char h1=adv(L);
            if(at_end(L) || !is_hex_digit_(pk(L))){
                error_lex(L->line, L->col, 1,
                    "invalid \\x escape: expected two hex digits (e.g. \\x41)");
                buf[pos++]='?';
                break;
            }
            char h2=adv(L);
            char hex[3]={h1,h2,'\0'};
            buf[pos++]=(char)strtol(hex,NULL,16);
            break;
        }
        case 'u':{ 
            char h[5]; int ui;
            for(ui=0;ui<4;ui++){
                if(at_end(L) || !is_hex_digit_(pk(L))){
                    error_lex(L->line, L->col, 1,
                        "invalid \\u escape: expected four hex digits (e.g. \\u0041)");
                    buf[pos++]='?';
                    goto escape_done;
                }
                h[ui]=adv(L);
            }
            h[4]='\0';
            { unsigned int cp=(unsigned)strtol(h,NULL,16);
              if(cp<0x80)       buf[pos++]=(char)cp;
              else if(cp<0x800){ buf[pos++]=(char)(0xC0|(cp>>6)); buf[pos++]=(char)(0x80|(cp&0x3F)); }
              else              { buf[pos++]=(char)(0xE0|(cp>>12)); buf[pos++]=(char)(0x80|((cp>>6)&0x3F)); buf[pos++]=(char)(0x80|(cp&0x3F)); }
            }
            escape_done:;
            break;
        }
        default: buf[pos++]='\\'; buf[pos++]=e;
    }
    return pos;
}

static Token scan_string(Lexer *L){
    char buf[MAX_STRING_LEN]; int pos=0;
    while(!at_end(L)&&pk(L)!='"'){
        char c=pk(L);
        if(c=='\n') L->line++;
        if(c=='\\'){
            adv(L);
            pos=process_escape(L,buf,pos);
        } else {
            buf[pos++]=adv(L);
        }
        if(pos>=MAX_STRING_LEN-4) return errtok(L,"string literal too long");
    }
    if(at_end(L)) return errtok(L,"unterminated string - missing closing '\"'");
    adv(L); buf[pos]='\0';
    Token t=mktok(L,TK_STRING);
    if(pos<(int)sizeof(t.value.string)) memcpy(t.value.string,buf,pos+1);
    return t;
}

static Token scan_fstring(Lexer *L){
    char buf[MAX_STRING_LEN]; int pos=0;
    while(!at_end(L)&&pk(L)!='"'){
        char c=pk(L);
        if(c=='\n') L->line++;
        if(c=='\\'){
            adv(L);
            pos=process_escape(L,buf,pos);
        } else {
            buf[pos++]=adv(L);
        }
        if(pos>=MAX_STRING_LEN-4) return errtok(L,"f-string too long");
    }
    if(at_end(L)) return errtok(L,"unterminated f-string");
    adv(L); buf[pos]='\0';
    Token t=mktok(L,TK_FSTRING);
    if(pos<(int)sizeof(t.value.string)) memcpy(t.value.string,buf,pos+1);
    return t;
}

static bool is_hex_digit(char c){
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}

static Token scan_number(Lexer *L){
    
    if(*(L->current-1)=='0'&&(pk(L)=='x'||pk(L)=='X')){
        adv(L);
        if(!is_hex_digit(pk(L)))
            return errtok(L, "invalid hex literal: expected hex digits after '0x' (e.g. 0xFF)");
        while(is_hex_digit(pk(L))) adv(L);
        Token t=mktok(L,TK_NUMBER);
        char buf[64]; int len=t.length<63?t.length:63;
        memcpy(buf,t.start,len); buf[len]='\0';
        t.value.number=(double)strtoll(buf,NULL,16);
        return t;
    }
    
    if(*(L->current-1)=='0'&&(pk(L)=='b'||pk(L)=='B')){
        adv(L);
        if(pk(L)!='0'&&pk(L)!='1')
            return errtok(L, "invalid binary literal: expected '0' or '1' after '0b' (e.g. 0b1010)");
        while(pk(L)=='0'||pk(L)=='1') adv(L);
        Token t=mktok(L,TK_NUMBER);
        char buf[128]; int len=t.length<127?t.length:127;
        memcpy(buf,t.start,len); buf[len]='\0';
        t.value.number=(double)strtoll(buf+2,NULL,2);
        return t;
    }
    
    if(*(L->current-1)=='0'&&(pk(L)=='o'||pk(L)=='O')){
        adv(L);
        if(pk(L)<'0'||pk(L)>'7')
            return errtok(L, "invalid octal literal: expected octal digit (0-7) after '0o' (e.g. 0o77)");
        while(pk(L)>='0'&&pk(L)<='7') adv(L);
        Token t=mktok(L,TK_NUMBER);
        char buf[64]; int len=t.length<63?t.length:63;
        memcpy(buf,t.start,len); buf[len]='\0';
        t.value.number=(double)strtoll(buf+2,NULL,8);
        return t;
    }
    while(is_digit(pk(L))||pk(L)=='_') adv(L); 
    if(pk(L)=='.'&&is_digit(pk2(L))){ adv(L); while(is_digit(pk(L))) adv(L); }
    if(pk(L)=='e'||pk(L)=='E'){
        adv(L);
        if(pk(L)=='+'||pk(L)=='-') adv(L);
        if(!is_digit(pk(L))) return errtok(L,"invalid number exponent");
        while(is_digit(pk(L))) adv(L);
    }
    Token t=mktok(L,TK_NUMBER);
    char buf[128]; int len=t.length<127?t.length:127;
    memcpy(buf,t.start,len); buf[len]='\0';
    
    char clean[128]; int ci=0;
    for(int i=0;i<len;i++) if(buf[i]!='_') clean[ci++]=buf[i];
    clean[ci]='\0';
    t.value.number=strtod(clean,NULL);
    return t;
}

static Token scan_ident(Lexer *L){
    while(is_alnum(pk(L))) adv(L);
    Token t=mktok(L,TK_IDENT);
    
    int len=t.length;
    const char *s=t.start;
#define KW(str,_kk) if(len==(int)strlen(str)&&memcmp(s,str,len)==0){t.kind=(_kk);return t;}
    KW("true",      TK_TRUE)    KW("false",     TK_FALSE)
    KW("nil",       TK_NIL)     KW("null",      TK_NULL)
    KW("async",     TK_ASYNC)   KW("await",     TK_AWAIT)
    KW("struct",    TK_STRUCT)  KW("do",        TK_DO)
    KW("let",       TK_LET)
    KW("var",       TK_VAR)     KW("array",     TK_ARRAY)
    KW("dict",      TK_DICT)    KW("if",        TK_IF)
    KW("else",      TK_ELSE)    KW("while",     TK_WHILE)
    KW("for",       TK_FOR)     KW("in",        TK_IN)
    KW("switch",    TK_SWITCH)  KW("case",      TK_CASE)
    KW("default",   TK_DEFAULT) KW("break",     TK_BREAK)
    KW("continue",  TK_CONTINUE)KW("return",    TK_RETURN)
    KW("func",      TK_FUNC)    KW("public",    TK_PUBLIC)
    KW("private",   TK_PRIVATE) KW("protected", TK_PROTECTED)
    KW("entry",     TK_ENTRY)
    KW("imp",       TK_IMP)     KW("export",    TK_EXPORT)
    KW("stdo",      TK_STDO)    KW("stdi",      TK_STDI)
    KW("try",       TK_TRY)     KW("catch",     TK_CATCH)
    KW("throw",     TK_THROW)   KW("typeof",    TK_TYPEOF)
    KW("and",       TK_AND)     KW("or",        TK_OR)
    KW("not",       TK_BANG)    KW("use",       TK_IMP)
    
    if(len==2&&memcmp(s,"in",2)==0){ t.kind=TK_IN; return t; }
    
    if(len==10&&memcmp(s,"__native__",10)==0){t.kind=TK_NATIVE_CALL;return t;}
#undef KW
    return t;
}

void lexer_init(Lexer *L, const char *source){
    L->source=source; L->start=source; L->current=source; L->line=1;
}

Token lexer_next(Lexer *L){
    skip_ws(L);
    L->start=L->current;
    if(at_end(L)) return mktok(L,TK_EOF);

    char c=adv(L);

    if(c=='\n'){ L->line++; return mktok(L,TK_NEWLINE); }

    if(c=='f'&&pk(L)=='"'){ adv(L); return scan_fstring(L); }
    if(is_alpha(c)) return scan_ident(L);
    if(is_digit(c)) return scan_number(L);
    if(c=='"') return scan_string(L);

    switch(c){
        case '+':
            if(mat(L,'+')) return mktok(L,TK_PLUSPLUS);
            if(mat(L,'=')) return mktok(L,TK_PLUS_ASSIGN);
            return mktok(L,TK_PLUS);
        case '-':
            if(mat(L,'>')) return mktok(L,TK_ARROW);
            

            if(mat(L,'=')) return mktok(L,TK_MINUS_ASSIGN);
            return mktok(L,TK_MINUS);
        case '*':
            if(mat(L,'*')){
                if(mat(L,'=')) return mktok(L,TK_STARSTAR_ASSIGN);
                return mktok(L,TK_STARSTAR);
            }
            if(mat(L,'=')) return mktok(L,TK_STAR_ASSIGN);
            return mktok(L,TK_STAR);
        case '/':
            if(mat(L,'=')) return mktok(L,TK_SLASH_ASSIGN);
            return mktok(L,TK_SLASH);
        case '%':
            if(mat(L,'=')) return mktok(L,TK_PERCENT_ASSIGN);
            return mktok(L,TK_PERCENT);
        case '=':
            if(mat(L,'=')) return mktok(L,TK_EQ);
            return mktok(L,TK_ASSIGN);
        case '!':
            if(mat(L,'=')) return mktok(L,TK_NEQ);
            return mktok(L,TK_BANG);
        case '<':
            if(mat(L,'<')) return mktok(L,TK_LSHIFT);
            if(mat(L,'=')) return mktok(L,TK_LE);
            return mktok(L,TK_LT);
        case '>':
            if(mat(L,'>')) return mktok(L,TK_RSHIFT);
            if(mat(L,'=')) return mktok(L,TK_GE);
            return mktok(L,TK_GT);
        case '&':
            if(mat(L,'&')) return mktok(L,TK_AND);
            return mktok(L,TK_AMP);
        case '|':
            if(mat(L,'|')) return mktok(L,TK_OR);
            return mktok(L,TK_PIPE);
        case '^':  return mktok(L,TK_CARET);
        case '~':  return mktok(L,TK_TILDE);
        case '?':
            if(mat(L,'?')) return mktok(L,TK_NULL_COAL);
            return mktok(L,TK_QUESTION);
        case '.':  return mktok(L,TK_DOT);
        case '(':  return mktok(L,TK_LPAREN);
        case ')':  return mktok(L,TK_RPAREN);
        case '{':  return mktok(L,TK_LBRACE);
        case '}':  return mktok(L,TK_RBRACE);
        case '[':  return mktok(L,TK_LBRACKET);
        case ']':  return mktok(L,TK_RBRACKET);
        case ',':  return mktok(L,TK_COMMA);
        case ';':  return mktok(L,TK_SEMICOLON);
        case ':':
            if(*L->current == ':'){
                L->current++;
                return mktok(L, TK_COLONCOLON);
            }
            return mktok(L,TK_COLON);
        case '@':  return mktok(L,TK_NATIVE_CALL); 
        default:{
            char msg[64];
            snprintf(msg,sizeof(msg),"unexpected character '%c' (0x%02X)",c,(unsigned char)c);
            return errtok(L,msg);
        }
    }
}

Token lexer_peek(Lexer *L){
    Lexer save=*L;
    Token t=lexer_next(L);
    *L=save;
    return t;
}

const char *token_kind_name(TokenKind k){
    switch(k){
        case TK_NUMBER:         return "number";
        case TK_STRING:         return "string";
        case TK_FSTRING:        return "f-string";
        case TK_IDENT:          return "identifier";
        case TK_TRUE:           return "true";
        case TK_FALSE:          return "false";
        case TK_NIL:            return "nil";
        case TK_NULL:           return "null";
        case TK_ASYNC:          return "async";
        case TK_AWAIT:          return "await";
        case TK_STRUCT:         return "struct";
        case TK_DO:             return "do";
        case TK_IN_OP:          return "in";
        case TK_NOT_IN:         return "not in";
        case TK_PLUSPLUS:       return "++";
        case TK_STARSTAR_ASSIGN:return "**=";
        case TK_LET:            return "let";
        case TK_VAR:            return "var";
        case TK_ARRAY:          return "array";
        case TK_DICT:           return "dict";
        case TK_IF:             return "if";
        case TK_ELSE:           return "else";
        case TK_WHILE:          return "while";
        case TK_FOR:            return "for";
        case TK_IN:             return "in";
        case TK_SWITCH:         return "switch";
        case TK_CASE:           return "case";
        case TK_DEFAULT:        return "default";
        case TK_BREAK:          return "break";
        case TK_CONTINUE:       return "continue";
        case TK_RETURN:         return "return";
        case TK_FUNC:           return "func";
        case TK_PUBLIC:         return "public";
        case TK_PRIVATE:        return "private";
        case TK_PROTECTED:      return "protected";
        case TK_ENTRY:          return "entry";
        case TK_IMP:            return "imp";
        case TK_EXPORT:         return "export";
        case TK_STDO:           return "stdo";
        case TK_STDI:           return "stdi";
        case TK_NATIVE_CALL:    return "@native";
        case TK_TRY:            return "try";
        case TK_CATCH:          return "catch";
        case TK_THROW:          return "throw";
        case TK_TYPEOF:         return "typeof";
        case TK_PLUS:           return "+";
        case TK_MINUS:          return "-";
        case TK_STAR:           return "*";
        case TK_SLASH:          return "/";
        case TK_PERCENT:        return "%";
        case TK_STARSTAR:       return "**";
        case TK_PLUS_ASSIGN:    return "+=";
        case TK_MINUS_ASSIGN:   return "-=";
        case TK_STAR_ASSIGN:    return "*=";
        case TK_SLASH_ASSIGN:   return "/=";
        case TK_PERCENT_ASSIGN: return "%=";
        case TK_AMP:            return "&";
        case TK_PIPE:           return "|";
        case TK_CARET:          return "^";
        case TK_TILDE:          return "~";
        case TK_LSHIFT:         return "<<";
        case TK_RSHIFT:         return ">>";
        case TK_EQ:             return "==";
        case TK_NEQ:            return "!=";
        case TK_LT:             return "<";
        case TK_GT:             return ">";
        case TK_LE:             return "<=";
        case TK_GE:             return ">=";
        case TK_ASSIGN:         return "=";
        case TK_AND:            return "&&";
        case TK_OR:             return "||";
        case TK_BANG:           return "!";
        case TK_QUESTION:       return "?";
        case TK_NULL_COAL:      return "??";
        case TK_ARROW:          return "->";
        case TK_DOT:            return ".";
        case TK_LPAREN:         return "(";
        case TK_RPAREN:         return ")";
        case TK_LBRACE:         return "{";
        case TK_RBRACE:         return "}";
        case TK_LBRACKET:       return "[";
        case TK_RBRACKET:       return "]";
        case TK_COMMA:          return ",";
        case TK_SEMICOLON:      return ";";
        case TK_COLON:          return ":";
        case TK_COLONCOLON:     return "::";
        case TK_NEWLINE:        return "newline";
        case TK_EOF:            return "EOF";
        case TK_ERROR:          return "error";
        default:                return "?";
    }
}
