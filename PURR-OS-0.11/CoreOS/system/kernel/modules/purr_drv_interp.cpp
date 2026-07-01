// purr_drv_interp.cpp — PURR Driver Language (.drv) interpreter
// Lexer + recursive-descent parser + tree-walk evaluator.
//
// Language subset:
//   int globals, function defs (void/int), if/else, while, return
//   Built-in hardware functions: gpio_*, i2c_*, delay, millis, log, log_int,
//     arg, streq, lora_send, lora_rssi, wifi_connected, mem_free, adc_read
//   Constants: INPUT=0, OUTPUT=1, INPUT_PULLUP=2, HIGH=1, LOW=0

#include "purr_drv_interp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef PURR_HAS_LORA
#include "lora_manager.h"
#endif

static const char *TAG = "pdrv";

// ═══════════════════════════════════════════════════════════════════
// Values
// ═══════════════════════════════════════════════════════════════════

typedef struct { int32_t i; const char *s; } PVal;
#define VI(v)  ((PVal){(v),NULL})
#define VS(p)  ((PVal){0,(p)})
#define VNULL  VI(0)

// ═══════════════════════════════════════════════════════════════════
// Lexer
// ═══════════════════════════════════════════════════════════════════

typedef enum {
    TK_INT_LIT, TK_STR_LIT, TK_IDENT,
    TK_INT, TK_VOID, TK_IF, TK_ELSE, TK_WHILE, TK_RETURN, TK_DRIVER, TK_STATIC,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PCT,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LEQ, TK_GEQ,
    TK_LAND, TK_LOR, TK_BANG,
    TK_ASSIGN, TK_PLUSEQ, TK_MINUSEQ,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_SEMI, TK_COMMA,
    TK_EOF,
} TK;

typedef struct { TK t; int32_t i; char s[128]; int line; } Tok;
typedef struct { const char *src; int pos, line; Tok cur; } Lex;

static void lex_advance(Lex *L) {
    const char *p = L->src + L->pos;
    for (;;) {
        while (*p && isspace((uint8_t)*p)) { if (*p=='\n') L->line++; p++; }
        if (p[0]=='/' && p[1]=='/') { while (*p && *p!='\n') p++; continue; }
        break;
    }
    L->pos = (int)(p - L->src);
    Tok t = {TK_EOF,0,{},L->line};
    if (!*p) { L->cur=t; return; }

    if (isdigit((uint8_t)*p)) {
        char *e; t.i=(int32_t)strtol(p,&e,0); t.t=TK_INT_LIT;
        L->pos+=(int)(e-p); L->cur=t; return;
    }
    if (*p=='"') {
        p++; int n=0;
        while (*p && *p!='"' && n<126) {
            if (*p=='\\' && p[1]) { switch(p[1]){case 'n':t.s[n++]='\n';break;case 't':t.s[n++]='\t';break;default:t.s[n++]=p[1];}p+=2;}
            else { t.s[n++]=*p++; }
        }
        t.s[n]=0; if(*p=='"')p++; t.t=TK_STR_LIT;
        L->pos=(int)(p-L->src); L->cur=t; return;
    }
    if (isalpha((uint8_t)*p)||*p=='_') {
        int n=0; while((isalnum((uint8_t)*p)||*p=='_')&&n<63) t.s[n++]=*p++;
        t.s[n]=0; L->pos=(int)(p-L->src);
        static const struct{const char*w;TK t;} kw[]={
            {"int",TK_INT},{"void",TK_VOID},{"if",TK_IF},{"else",TK_ELSE},
            {"while",TK_WHILE},{"return",TK_RETURN},{"driver",TK_DRIVER},{"static",TK_STATIC},{NULL,TK_EOF}};
        t.t=TK_IDENT;
        for(int i=0;kw[i].w;i++) if(!strcmp(t.s,kw[i].w)){t.t=kw[i].t;break;}
        L->cur=t; return;
    }
#define O2(a,b,r) if(p[0]==(a)&&p[1]==(b)){t.t=(r);L->pos+=2;L->cur=t;return;}
#define O1(a,r)   if(p[0]==(a)){t.t=(r);L->pos+=1;L->cur=t;return;}
    O2('=','=',TK_EQ)O2('!','=',TK_NEQ)O2('<','=',TK_LEQ)O2('>','=',TK_GEQ)
    O2('&','&',TK_LAND)O2('|','|',TK_LOR)O2('+','=',TK_PLUSEQ)O2('-','=',TK_MINUSEQ)
    O1('<',TK_LT)O1('>',TK_GT)O1('=',TK_ASSIGN)O1('!',TK_BANG)
    O1('+',TK_PLUS)O1('-',TK_MINUS)O1('*',TK_STAR)O1('/',TK_SLASH)O1('%',TK_PCT)
    O1('(',TK_LPAREN)O1(')',TK_RPAREN)O1('{',TK_LBRACE)O1('}',TK_RBRACE)
    O1(';',TK_SEMI)O1(',',TK_COMMA)
    L->pos++; lex_advance(L);
}
static void     lex_init(Lex*L,const char*s){L->src=s;L->pos=0;L->line=1;lex_advance(L);}
static Tok      lpeek(Lex*L){return L->cur;}
static Tok      lnext(Lex*L){Tok t=L->cur;lex_advance(L);return t;}
static bool     lmatch(Lex*L,TK t){if(L->cur.t==t){lex_advance(L);return true;}return false;}

// peek 2 tokens ahead without consuming
static Tok lpeek2(Lex *L) {
    Lex tmp=*L; lex_advance(&tmp); return tmp.cur;
}

// ═══════════════════════════════════════════════════════════════════
// AST
// ═══════════════════════════════════════════════════════════════════

typedef enum {
    ND_BLOCK, ND_IF, ND_WHILE, ND_RETURN, ND_ESTMT,
    ND_ASSIGN, ND_ASSIGNOP,
    ND_BINOP, ND_UNOP, ND_CALL,
    ND_INT_LIT, ND_STR_LIT, ND_IDENT,
    ND_VAR_DECL, ND_STATIC_DECL, ND_FUNC,
} ND;

typedef struct Node Node;
struct Node {
    ND nd; Node *next;
    union {
        struct { Node *stmts; }                                     block;
        struct { Node *cond,*then_,*else_; }                       if_;
        struct { Node *cond,*body; }                                while_;
        struct { Node *val; }                                       ret;
        struct { Node *expr; }                                      estmt;
        struct { char tgt[32]; int op; Node *val; }                 asgn;
        struct { int op; Node *a,*b; }                              bin;
        struct { int op; Node *a; }                                 un;
        struct { char name[32]; Node *args; int nargs; }            call;
        int32_t ival;
        char sval[128];
        char ident[32];
        struct { char name[32]; bool is_st; Node *init; }          vd;
        struct { char name[32]; char p[8][32]; int np; Node *body;} fn;
    };
};

static Node *nn(ND t){Node*n=(Node*)calloc(1,sizeof(Node));if(n)n->nd=t;return n;}

static void nfree(Node *n) {
    if(!n)return;
    switch(n->nd){
    case ND_BLOCK: nfree(n->block.stmts); break;
    case ND_IF:    nfree(n->if_.cond);nfree(n->if_.then_);nfree(n->if_.else_); break;
    case ND_WHILE: nfree(n->while_.cond);nfree(n->while_.body); break;
    case ND_RETURN:nfree(n->ret.val); break;
    case ND_ESTMT: nfree(n->estmt.expr); break;
    case ND_ASSIGN:case ND_ASSIGNOP: nfree(n->asgn.val); break;
    case ND_BINOP: nfree(n->bin.a);nfree(n->bin.b); break;
    case ND_UNOP:  nfree(n->un.a); break;
    case ND_CALL:  nfree(n->call.args); break;
    case ND_VAR_DECL: case ND_STATIC_DECL: nfree(n->vd.init); break;
    case ND_FUNC:  nfree(n->fn.body); break;
    default: break;
    }
    nfree(n->next); free(n);
}

// ═══════════════════════════════════════════════════════════════════
// Parser
// ═══════════════════════════════════════════════════════════════════

typedef struct { Lex *lex; char *err; int elen; bool fail; } Par;

static void perr(Par*P,const char*fmt,...){
    if(!P->fail){char b[128];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);
        snprintf(P->err,P->elen,"line %d: %s",P->lex->cur.line,b);}P->fail=true;}

static Tok expect_(Par*P,TK t,const char*w){
    Tok k=lpeek(P->lex);
    if(k.t!=t){perr(P,"expected %s",w);return k;}
    return lnext(P->lex);
}

// forward declarations
static Node *pexpr(Par*P);
static Node *pstmt(Par*P);

static Node *pprimary(Par*P){
    if(P->fail)return NULL;
    Tok t=lnext(P->lex);
    if(t.t==TK_INT_LIT){Node*n=nn(ND_INT_LIT);n->ival=t.i;return n;}
    if(t.t==TK_STR_LIT){Node*n=nn(ND_STR_LIT);strncpy(n->sval,t.s,127);return n;}
    if(t.t==TK_IDENT){
        if(lpeek(P->lex).t==TK_LPAREN){
            lnext(P->lex);
            Node*n=nn(ND_CALL);strncpy(n->call.name,t.s,31);
            Node*tail=NULL;
            while(!P->fail&&lpeek(P->lex).t!=TK_RPAREN&&lpeek(P->lex).t!=TK_EOF){
                Node*a=pexpr(P);if(!a)break;
                a->next=NULL;
                if(!n->call.args)n->call.args=a;else tail->next=a;
                tail=a;n->call.nargs++;
                if(!lmatch(P->lex,TK_COMMA))break;
            }
            expect_(P,TK_RPAREN,"')'");return n;
        }
        Node*n=nn(ND_IDENT);strncpy(n->ident,t.s,31);return n;
    }
    if(t.t==TK_LPAREN){Node*e=pexpr(P);expect_(P,TK_RPAREN,"')'");return e;}
    perr(P,"unexpected '%s'",t.s[0]?t.s:"?");return NULL;
}

static Node *punary(Par*P){
    if(P->fail)return NULL;
    Tok t=lpeek(P->lex);
    if(t.t==TK_BANG||t.t==TK_MINUS){lnext(P->lex);Node*n=nn(ND_UNOP);n->un.op=t.t;n->un.a=punary(P);return n;}
    return pprimary(P);
}
static Node *pmul(Par*P){
    Node*a=punary(P);
    while(!P->fail){Tok t=lpeek(P->lex);if(t.t!=TK_STAR&&t.t!=TK_SLASH&&t.t!=TK_PCT)break;
        lnext(P->lex);Node*n=nn(ND_BINOP);n->bin.op=t.t;n->bin.a=a;n->bin.b=punary(P);a=n;}return a;}
static Node *padd(Par*P){
    Node*a=pmul(P);
    while(!P->fail){Tok t=lpeek(P->lex);if(t.t!=TK_PLUS&&t.t!=TK_MINUS)break;
        lnext(P->lex);Node*n=nn(ND_BINOP);n->bin.op=t.t;n->bin.a=a;n->bin.b=pmul(P);a=n;}return a;}
static Node *pcmp(Par*P){
    Node*a=padd(P);
    while(!P->fail){Tok t=lpeek(P->lex);if(t.t!=TK_LT&&t.t!=TK_GT&&t.t!=TK_LEQ&&t.t!=TK_GEQ)break;
        lnext(P->lex);Node*n=nn(ND_BINOP);n->bin.op=t.t;n->bin.a=a;n->bin.b=padd(P);a=n;}return a;}
static Node *peq(Par*P){
    Node*a=pcmp(P);
    while(!P->fail){Tok t=lpeek(P->lex);if(t.t!=TK_EQ&&t.t!=TK_NEQ)break;
        lnext(P->lex);Node*n=nn(ND_BINOP);n->bin.op=t.t;n->bin.a=a;n->bin.b=pcmp(P);a=n;}return a;}
static Node *pand(Par*P){
    Node*a=peq(P);
    while(!P->fail&&lpeek(P->lex).t==TK_LAND){lnext(P->lex);Node*n=nn(ND_BINOP);n->bin.op=TK_LAND;n->bin.a=a;n->bin.b=peq(P);a=n;}return a;}
static Node *por(Par*P){
    Node*a=pand(P);
    while(!P->fail&&lpeek(P->lex).t==TK_LOR){lnext(P->lex);Node*n=nn(ND_BINOP);n->bin.op=TK_LOR;n->bin.a=a;n->bin.b=pand(P);a=n;}return a;}

static Node *pexpr(Par*P){
    if(P->fail)return NULL;
    // assignment: IDENT (=/+=-=) expr
    if(lpeek(P->lex).t==TK_IDENT){
        Tok t2=lpeek2(P->lex);
        if(t2.t==TK_ASSIGN||t2.t==TK_PLUSEQ||t2.t==TK_MINUSEQ){
            Tok nm=lnext(P->lex); lnext(P->lex);
            Node*n=nn(t2.t==TK_ASSIGN?ND_ASSIGN:ND_ASSIGNOP);
            strncpy(n->asgn.tgt,nm.s,31);n->asgn.op=t2.t;n->asgn.val=pexpr(P);return n;
        }
    }
    return por(P);
}

static Node *pblock(Par*P){
    expect_(P,TK_LBRACE,"'{'");if(P->fail)return NULL;
    Node*blk=nn(ND_BLOCK);Node*tail=NULL;
    while(!P->fail&&lpeek(P->lex).t!=TK_RBRACE&&lpeek(P->lex).t!=TK_EOF){
        Node*s=pstmt(P);if(!s)break;
        if(!blk->block.stmts)blk->block.stmts=s;else tail->next=s;
        tail=s;
    }
    expect_(P,TK_RBRACE,"'}'");return blk;
}

static Node *pstmt(Par*P){
    if(P->fail)return NULL;
    Tok t=lpeek(P->lex);
    if(t.t==TK_IF){
        lnext(P->lex);expect_(P,TK_LPAREN,"'('");
        Node*c=pexpr(P);expect_(P,TK_RPAREN,"')'");
        Node*n=nn(ND_IF);n->if_.cond=c;n->if_.then_=pblock(P);
        if(!P->fail&&lpeek(P->lex).t==TK_ELSE){lnext(P->lex);
            n->if_.else_=(lpeek(P->lex).t==TK_IF)?pstmt(P):pblock(P);}
        return n;
    }
    if(t.t==TK_WHILE){
        lnext(P->lex);expect_(P,TK_LPAREN,"'('");
        Node*c=pexpr(P);expect_(P,TK_RPAREN,"')'");
        Node*n=nn(ND_WHILE);n->while_.cond=c;n->while_.body=pblock(P);return n;
    }
    if(t.t==TK_RETURN){
        lnext(P->lex);Node*n=nn(ND_RETURN);
        if(lpeek(P->lex).t!=TK_SEMI)n->ret.val=pexpr(P);
        expect_(P,TK_SEMI,"';'");return n;
    }
    if(t.t==TK_STATIC){
        lnext(P->lex);expect_(P,TK_INT,"'int'");
        Tok nm=expect_(P,TK_IDENT,"variable name");
        Node*n=nn(ND_STATIC_DECL);strncpy(n->vd.name,nm.s,31);n->vd.is_st=true;
        if(lmatch(P->lex,TK_ASSIGN))n->vd.init=pexpr(P);
        expect_(P,TK_SEMI,"';'");return n;
    }
    if(t.t==TK_INT){
        lnext(P->lex);Tok nm=expect_(P,TK_IDENT,"variable name");
        Node*n=nn(ND_VAR_DECL);strncpy(n->vd.name,nm.s,31);
        if(lmatch(P->lex,TK_ASSIGN))n->vd.init=pexpr(P);
        expect_(P,TK_SEMI,"';'");return n;
    }
    Node*e=pexpr(P);if(!e)return NULL;
    expect_(P,TK_SEMI,"';'");
    Node*n=nn(ND_ESTMT);n->estmt.expr=e;return n;
}

// ═══════════════════════════════════════════════════════════════════
// Environment
// ═══════════════════════════════════════════════════════════════════

#define ENV_MAX 32
typedef struct Env Env;
struct Env { char k[ENV_MAX][32]; PVal v[ENV_MAX]; int n; Env *up; };

static bool eget(Env*e,const char*k,PVal*out){
    for(Env*f=e;f;f=f->up)
        for(int i=0;i<f->n;i++)
            if(!strcmp(f->k[i],k)){if(out)*out=f->v[i];return true;}
    return false;
}
static bool eset(Env*e,const char*k,PVal v){
    for(Env*f=e;f;f=f->up)
        for(int i=0;i<f->n;i++)
            if(!strcmp(f->k[i],k)){f->v[i]=v;return true;}
    return false;
}
static void edef(Env*e,const char*k,PVal v){
    if(eset(e,k,v))return;
    if(e->n<ENV_MAX){strncpy(e->k[e->n],k,31);e->v[e->n++]=v;}
}

// ═══════════════════════════════════════════════════════════════════
// Script struct
// ═══════════════════════════════════════════════════════════════════

#define FUNCS_MAX 16
typedef struct { char name[32]; char p[8][32]; int np; Node *body; } FDef;

struct pdrv_script_t {
    char name[32];
    FDef fns[FUNCS_MAX]; int nfns;
    Env globals;
    Env statics;   // persistent between calls; parent = globals
    char cur_arg[128];
};

// ═══════════════════════════════════════════════════════════════════
// Evaluator  (eval_stmt forward-declared so eval_expr can call it)
// ═══════════════════════════════════════════════════════════════════

typedef struct { pdrv_script_t *s; char *err; int elen; bool error; PVal ret; bool returned; } EC;

static void eval_stmt(EC*E, Node*n, Env*env);

static PVal eval_expr(EC*E, Node*n, Env*env) {
    if(!n||E->error)return VNULL;
    switch(n->nd){
    case ND_INT_LIT: return VI(n->ival);
    case ND_STR_LIT: return VS(n->sval);
    case ND_IDENT: {
        PVal v; if(eget(env,n->ident,&v))return v;
        // built-in constants
        if(!strcmp(n->ident,"INPUT"))        return VI(0);
        if(!strcmp(n->ident,"OUTPUT"))       return VI(1);
        if(!strcmp(n->ident,"INPUT_PULLUP")) return VI(2);
        if(!strcmp(n->ident,"HIGH"))         return VI(1);
        if(!strcmp(n->ident,"LOW"))          return VI(0);
        snprintf(E->err,E->elen,"undefined '%s'",n->ident);E->error=true;return VNULL;
    }
    case ND_ASSIGN: {
        PVal v=eval_expr(E,n->asgn.val,env);
        if(!eset(env,n->asgn.tgt,v))edef(env,n->asgn.tgt,v);
        return v;
    }
    case ND_ASSIGNOP: {
        PVal cur=VNULL; eget(env,n->asgn.tgt,&cur);
        PVal r=eval_expr(E,n->asgn.val,env);
        PVal v=VI(n->asgn.op==TK_PLUSEQ?cur.i+r.i:cur.i-r.i);
        if(!eset(env,n->asgn.tgt,v))edef(env,n->asgn.tgt,v);
        return v;
    }
    case ND_BINOP: {
        PVal a=eval_expr(E,n->bin.a,env); if(E->error)return VNULL;
        if(n->bin.op==TK_LAND) return VI(a.i?eval_expr(E,n->bin.b,env).i:0);
        if(n->bin.op==TK_LOR)  return VI(a.i?1:(eval_expr(E,n->bin.b,env).i?1:0));
        PVal b=eval_expr(E,n->bin.b,env);
        // string comparison
        if((n->bin.op==TK_EQ||n->bin.op==TK_NEQ)&&(a.s||b.s)){
            int eq=!strcmp(a.s?a.s:"",b.s?b.s:"");
            return VI(n->bin.op==TK_EQ?eq:!eq);
        }
        switch(n->bin.op){
        case TK_PLUS:  return VI(a.i+b.i);
        case TK_MINUS: return VI(a.i-b.i);
        case TK_STAR:  return VI(a.i*b.i);
        case TK_SLASH: return VI(b.i?a.i/b.i:0);
        case TK_PCT:   return VI(b.i?a.i%b.i:0);
        case TK_EQ:    return VI(a.i==b.i);
        case TK_NEQ:   return VI(a.i!=b.i);
        case TK_LT:    return VI(a.i<b.i);
        case TK_GT:    return VI(a.i>b.i);
        case TK_LEQ:   return VI(a.i<=b.i);
        case TK_GEQ:   return VI(a.i>=b.i);
        default: return VNULL;
        }
    }
    case ND_UNOP: {
        PVal a=eval_expr(E,n->un.a,env);
        return n->un.op==TK_BANG?VI(!a.i):VI(-a.i);
    }
    case ND_CALL: {
        // evaluate args
        PVal av[8]={};int na=0;
        for(Node*a=n->call.args;a&&na<8;a=a->next) av[na++]=eval_expr(E,a,env);
        if(E->error)return VNULL;
        #define AI(x) (na>(x)?av[x].i:0)
        #define AS(x) (na>(x)?av[x].s:NULL)

        // user-defined function?
        for(int i=0;i<E->s->nfns;i++){
            FDef*fd=&E->s->fns[i];
            if(strcmp(fd->name,n->call.name))continue;
            Env frame={};
            // statics -> globals chain
            frame.up=&E->s->statics;
            for(int p=0;p<fd->np&&p<na;p++) edef(&frame,fd->p[p],av[p]);
            bool pr=E->returned; PVal pv=E->ret;
            E->returned=false; E->ret=VNULL;
            for(Node*st=fd->body->block.stmts;st&&!E->returned&&!E->error;st=st->next)
                eval_stmt(E,st,&frame);
            PVal rv=E->ret; E->returned=pr; E->ret=pv;
            return rv;
        }

        // built-ins
        const char *nm=n->call.name;

        if(!strcmp(nm,"gpio_mode")){
            gpio_config_t c={};c.pin_bit_mask=1ULL<<AI(0);
            c.mode=(AI(1)==1)?GPIO_MODE_OUTPUT:GPIO_MODE_INPUT;
            c.pull_up_en=(AI(1)==2)?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE;
            c.pull_down_en=GPIO_PULLDOWN_DISABLE;c.intr_type=GPIO_INTR_DISABLE;
            gpio_config(&c);return VI(0);
        }
        if(!strcmp(nm,"gpio_write")){gpio_set_level((gpio_num_t)AI(0),(uint32_t)AI(1));return VI(0);}
        if(!strcmp(nm,"gpio_read")) {return VI(gpio_get_level((gpio_num_t)AI(0)));}
        if(!strcmp(nm,"adc_read"))  {return VI(0);} // stub — adc_oneshot per-pin setup needed

        if(!strcmp(nm,"i2c_init")){
            // PDL i2c_init(sda, scl) — new i2c_master API, one bus per script slot
            static i2c_master_bus_handle_t s_pdl_bus = NULL;
            if(s_pdl_bus) { i2c_del_master_bus(s_pdl_bus); s_pdl_bus=NULL; }
            i2c_master_bus_config_t bc={};
            bc.i2c_port=I2C_NUM_0;
            bc.sda_io_num=(gpio_num_t)AI(0);
            bc.scl_io_num=(gpio_num_t)AI(1);
            bc.clk_source=I2C_CLK_SRC_DEFAULT;
            bc.glitch_ignore_cnt=7;
            bc.flags.enable_internal_pullup=true;
            i2c_new_master_bus(&bc,&s_pdl_bus);
            return VI(s_pdl_bus?0:-1);
        }
        if(!strcmp(nm,"i2c_write")){
            static i2c_master_bus_handle_t s_pdl_bus = NULL;
            if(!s_pdl_bus) return VI(-1);
            i2c_device_config_t dc={};
            dc.device_address=(uint16_t)AI(0);
            dc.scl_speed_hz=400000;
            i2c_master_dev_handle_t dev;
            if(i2c_master_bus_add_device(s_pdl_bus,&dc,&dev)!=ESP_OK) return VI(-1);
            uint8_t buf[2]={(uint8_t)AI(1),(uint8_t)AI(2)};
            esp_err_t r=i2c_master_transmit(dev,buf,2,10);
            i2c_master_bus_rm_device(dev);
            return VI(r==ESP_OK?0:-1);
        }
        if(!strcmp(nm,"i2c_read")){
            static i2c_master_bus_handle_t s_pdl_bus = NULL;
            if(!s_pdl_bus) return VI(-1);
            i2c_device_config_t dc={};
            dc.device_address=(uint16_t)AI(0);
            dc.scl_speed_hz=400000;
            i2c_master_dev_handle_t dev;
            if(i2c_master_bus_add_device(s_pdl_bus,&dc,&dev)!=ESP_OK) return VI(-1);
            uint8_t reg=(uint8_t)AI(1),out=0;
            i2c_master_transmit_receive(dev,&reg,1,&out,1,10);
            i2c_master_bus_rm_device(dev);
            return VI((int32_t)out);
        }

        if(!strcmp(nm,"delay"))    {vTaskDelay(pdMS_TO_TICKS(AI(0)));return VI(0);}
        if(!strcmp(nm,"millis"))   {return VI((int32_t)(esp_timer_get_time()/1000LL));}
        if(!strcmp(nm,"mem_free")) {return VI((int32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));}

        if(!strcmp(nm,"log")){
            const char*s=AS(0);
            if(s) ESP_LOGI(TAG,"[%s] %s",E->s->name,s);
            else  ESP_LOGI(TAG,"[%s] %d",E->s->name,(int)AI(0));
            return VI(0);
        }
        if(!strcmp(nm,"log_int")){ESP_LOGI(TAG,"[%s] %d",E->s->name,(int)AI(0));return VI(0);}

        if(!strcmp(nm,"arg"))   {return VS(E->s->cur_arg);}
        if(!strcmp(nm,"streq")){
            const char*a=AS(0),*b=AS(1);
            return VI((a&&b)?!strcmp(a,b):0);
        }

#ifdef PURR_HAS_LORA
        if(!strcmp(nm,"lora_send")){
            const char*d=AS(0);if(!d)return VI(-1);
            return VI(lora_manager_send((const uint8_t*)d,strlen(d))?0:-1);
        }
        if(!strcmp(nm,"lora_rssi")){return VI(lora_manager_rssi());}
#endif
        if(!strcmp(nm,"wifi_connected")){return VI(0);} // stub — kitt.wifi_connected() via KITT
        if(!strcmp(nm,"mem_free"))      {return VI((int32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));}

        snprintf(E->err,E->elen,"unknown function '%s'",nm);
        E->error=true;
        #undef AI
        #undef AS
        return VNULL;
    }
    default: return VNULL;
    }
}

static void eval_stmt(EC*E, Node*n, Env*env) {
    if(!n||E->error||E->returned)return;
    switch(n->nd){
    case ND_VAR_DECL: {
        PVal v=n->vd.init?eval_expr(E,n->vd.init,env):VI(0);
        edef(env,n->vd.name,v); break;
    }
    case ND_STATIC_DECL: {
        // stored in E->s->statics (parent of local frame) — init only once
        char mk[40]; snprintf(mk,sizeof(mk),"_s_%s",n->vd.name);
        if(!eget(&E->s->statics,mk,NULL)){
            PVal v=n->vd.init?eval_expr(E,n->vd.init,env):VI(0);
            edef(&E->s->statics,mk,v);
        }
        // alias into local env so code can use the bare name
        PVal sv; eget(&E->s->statics,mk,&sv);
        edef(env,n->vd.name,sv);
        break;
    }
    case ND_ESTMT: eval_expr(E,n->estmt.expr,env); break;
    case ND_IF:
        if(eval_expr(E,n->if_.cond,env).i) eval_stmt(E,n->if_.then_,env);
        else if(n->if_.else_)               eval_stmt(E,n->if_.else_,env);
        break;
    case ND_WHILE: {
        int g=100000;
        while(!E->error&&!E->returned&&eval_expr(E,n->while_.cond,env).i&&g-->0)
            eval_stmt(E,n->while_.body,env);
        break;
    }
    case ND_BLOCK:
        for(Node*s=n->block.stmts;s&&!E->error&&!E->returned;s=s->next)
            eval_stmt(E,s,env);
        break;
    case ND_RETURN:
        E->ret=n->ret.val?eval_expr(E,n->ret.val,env):VNULL;
        E->returned=true; break;
    default: eval_expr(E,n,env); break;
    }

    // write-back: if this was a static-decl alias, sync the value back
    if(n->nd==ND_STATIC_DECL){
        char mk[40]; snprintf(mk,sizeof(mk),"_s_%s",n->vd.name);
        PVal lv; if(eget(env,n->vd.name,&lv)) eset(&E->s->statics,mk,lv);
    }
}

// After any function call, sync static var aliases back to the statics frame
static void sync_statics(EC*E, Env*frame){
    for(int i=0;i<frame->n;i++){
        char mk[40]; snprintf(mk,sizeof(mk),"_s_%s",frame->k[i]);
        if(eget(&E->s->statics,mk,NULL)) eset(&E->s->statics,mk,frame->v[i]);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════

pdrv_script_t *pdrv_parse(const char *src, char *err_buf, int err_len) {
    pdrv_script_t *s=(pdrv_script_t*)calloc(1,sizeof(pdrv_script_t));
    if(!s){snprintf(err_buf,err_len,"oom");return NULL;}
    strncpy(s->name,"unnamed",31);
    // globals -> no parent; statics -> globals
    s->statics.up=&s->globals;

    Lex lex; lex_init(&lex,src);
    Par par={&lex,err_buf,err_len,false};

    while(!par.fail&&lpeek(&lex).t!=TK_EOF){
        Tok t=lpeek(&lex);

        if(t.t==TK_DRIVER){
            lnext(&lex);
            Tok nm=expect_(&par,TK_STR_LIT,"driver name");
            if(!par.fail)strncpy(s->name,nm.s,31);
            expect_(&par,TK_SEMI,"';'"); continue;
        }

        // void/int IDENT ( ... ) { ... }   — function
        // int IDENT [= lit] ;              — global var
        if(t.t==TK_VOID||(t.t==TK_INT&&lpeek2(&lex).t==TK_LPAREN)){
            // actually need 3-token lookahead for int fn()
            // approach: consume type, then check what follows IDENT
        }
        if(t.t==TK_VOID||t.t==TK_INT){
            lnext(&lex); // consume type
            Tok nm=expect_(&par,TK_IDENT,"name");
            if(par.fail)break;

            if(lpeek(&lex).t==TK_LPAREN){
                // function
                lnext(&lex);
                FDef fd={}; strncpy(fd.name,nm.s,31);
                while(!par.fail&&lpeek(&lex).t!=TK_RPAREN&&lpeek(&lex).t!=TK_EOF){
                    if(lpeek(&lex).t==TK_INT||lpeek(&lex).t==TK_VOID) lnext(&lex);
                    Tok pn=expect_(&par,TK_IDENT,"param name");
                    if(!par.fail&&fd.np<8) strncpy(fd.p[fd.np++],pn.s,31);
                    if(!lmatch(&lex,TK_COMMA))break;
                }
                expect_(&par,TK_RPAREN,"')'");
                fd.body=pblock(&par);
                if(!par.fail&&s->nfns<FUNCS_MAX) s->fns[s->nfns++]=fd;
            } else {
                // global var
                PVal v=VI(0);
                if(lmatch(&lex,TK_ASSIGN)){
                    Tok vt=lnext(&lex); v=VI(vt.i); // literal only
                }
                expect_(&par,TK_SEMI,"';'");
                if(!par.fail) edef(&s->globals,nm.s,v);
            }
            continue;
        }

        perr(&par,"unexpected token at top level: '%s'",t.s[0]?t.s:"?"); break;
    }

    if(par.fail){pdrv_free(s);return NULL;}
    return s;
}

void pdrv_free(pdrv_script_t *s){
    if(!s)return;
    for(int i=0;i<s->nfns;i++) nfree(s->fns[i].body);
    free(s);
}

const char *pdrv_name(pdrv_script_t *s){ return s?s->name:NULL; }

bool pdrv_call(pdrv_script_t *s, const char *func, const char *arg_str,
               char *err_buf, int err_len) {
    if(!s)return false;
    strncpy(s->cur_arg, arg_str?arg_str:"", 127);

    FDef *fd=NULL;
    for(int i=0;i<s->nfns;i++) if(!strcmp(s->fns[i].name,func)){fd=&s->fns[i];break;}
    if(!fd)return true; // not defined — not an error

    EC E={s,err_buf,err_len,false,VNULL,false};
    Env frame={}; frame.up=&s->statics; // statics -> globals chain
    for(int p=0;p<fd->np;p++) edef(&frame,fd->p[p],VNULL);

    for(Node*st=fd->body->block.stmts;st&&!E.returned&&!E.error;st=st->next)
        eval_stmt(&E,st,&frame);

    sync_statics(&E,&frame);
    return !E.error;
}
