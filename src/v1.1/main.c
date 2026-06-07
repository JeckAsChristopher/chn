

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "error.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "func.h"
#include "compiler.h"
#include "vm.h"
#include "gc.h"
#include "bytecode.h"
#include "native.h"
#include "../memory/include/MemoryTypes.h"  

#define MAX_IMPORTED 256
static char imported_files[MAX_IMPORTED][1024];
static int  imported_count = 0;
static bool already_imported(const char *p){
    for(int i=0;i<imported_count;i++) if(!strcmp(imported_files[i],p)) return true;
    return false;
}
static void mark_imported(const char *p){
    if(imported_count<MAX_IMPORTED) strncpy(imported_files[imported_count++],p,1023);
}

static char *read_file(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){fprintf(stderr,"error: cannot open '%s'\n\n",path);return NULL;}
    fseek(f,0,SEEK_END);long sz=ftell(f);rewind(f);
    char *buf=(char*)malloc((size_t)sz+1);
    if(!buf){fclose(f);return NULL;}
    size_t n=fread(buf,1,(size_t)sz,f);buf[n]='\0';fclose(f);return buf;
}
static bool file_exists(const char *p){struct stat st;return stat(p,&st)==0;}

static char chn_bin_dir[1024]="";

bool (*chn_import_handler)(const char *path, Compiler *C)=NULL;
static bool compile_and_import(const char *src, Compiler *parent_C);

static bool load_cco_deps(const char *cco_path); 

static bool try_import_cco(const char *path, Compiler *parent_C){
    bool was_imported = already_imported(path);
    if(!was_imported) mark_imported(path);

    

    if(!was_imported) load_cco_deps(path);

    FunctionObject **fns; int nf;
    BCResult r = bc_read_cco(path, &fns, &nf);
    if(r != BC_OK){
        fprintf(stderr,"error: load CCO '%s': %s\n\n", path, bc_result_str(r));
        return false;
    }
    for(int i = 0; i < nf; i++)
        if(parent_C->import_count < MAX_FUNCS)
            parent_C->imports[parent_C->import_count++] = fns[i];
    free(fns);
    return true;
}

static bool try_import_from(const char *dir, const char *name, Compiler *parent_C);

static bool search_chn_libs(const char *rel_path, Compiler *parent_C){
    const char *slash=strrchr(rel_path,'/');
    const char *name=slash?slash+1:rel_path;
    char bare[256]; strncpy(bare,name,sizeof(bare)-1); bare[sizeof(bare)-1]='\0';
    char *dot=strrchr(bare,'.'); if(dot) *dot='\0';
    char local_dir[1024]="";
    if(parent_C->source_file[0]){
        const char *sl=strrchr(parent_C->source_file,'/');
        if(sl){ size_t dl=(size_t)(sl-parent_C->source_file)+1; memcpy(local_dir,parent_C->source_file,dl); local_dir[dl]='\0'; }
        else strncpy(local_dir,"./",sizeof(local_dir)-1);
    } else strncpy(local_dir,"./",sizeof(local_dir)-1);
    if(try_import_from(local_dir,bare,parent_C)) return true;
    char walk[1024]; strncpy(walk,local_dir,sizeof(walk)-1);
    for(int depth=0;depth<16;depth++){
        char libdir[1100]; snprintf(libdir,sizeof(libdir),"%schn-libs/",walk);
        if(file_exists(libdir)&&try_import_from(libdir,bare,parent_C)) return true;
        int len=(int)strlen(walk); if(len<=1) break;
        if(walk[len-1]=='/') walk[len-1]='\0';
        char *up=strrchr(walk,'/'); if(!up) break;
        *(up+1)='\0'; if(!strcmp(walk,local_dir)) break;
    }
    if(chn_bin_dir[0]){
        char instdir[1100]; snprintf(instdir,sizeof(instdir),"%s/chn-libs/",chn_bin_dir);
        if(try_import_from(instdir,bare,parent_C)) return true;
    }
    return false;
}

static void path_dir(const char *path, char *out, size_t n){
    const char *sl=strrchr(path,'/');
    if(sl&&(size_t)(sl-path+2)<n){
        size_t dl=(size_t)(sl-path)+1; memcpy(out,path,dl); out[dl]='\0';
    } else { out[0]='.'; out[1]='/'; out[2]='\0'; }
}
static void resolve_path(const char *from, const char *rel, char *out, size_t n){
    char dir[1024]; path_dir(from,dir,sizeof(dir));
    if(dir[0]&&rel[0]!='/') snprintf(out,n,"%s%s",dir,rel);
    else{ strncpy(out,rel,n-1); out[n-1]='\0'; }
}
static void strip_ext(const char *path, char *out, size_t n){
    strncpy(out,path,n-1); out[n-1]='\0';
    char *dot=strrchr(out,'.'); const char *sl=strrchr(out,'/');
    if(dot&&(!sl||dot>sl))*dot='\0';
}

static bool try_import_from(const char *dir, const char *name, Compiler *parent_C){
    char base[1024]; snprintf(base,sizeof(base),"%s%s",dir,name);
    if(already_imported(base)) return true;
    char fn_p[1100],cn_p[1100],sr_p[1100],co_p[1100];
    snprintf(fn_p,sizeof(fn_p),"%s.function",base);
    snprintf(cn_p,sizeof(cn_p),"%s.chn2",base);
    snprintf(sr_p,sizeof(sr_p),"%s.chn",base);
    snprintf(co_p,sizeof(co_p),"%s.cco",base);
    if(file_exists(fn_p)){
        if(already_imported(fn_p)) return true;
        mark_imported(fn_p); mark_imported(base);
        FunctionObject **fns;int nf;
        BCResult r=bc_read_functions(fn_p,&fns,&nf);
        if(r!=BC_OK){fprintf(stderr,"error: load '%s': %s\n\n",fn_p,bc_result_str(r));return false;}
        for(int i=0;i<nf;i++) if(parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i];
        free(fns); return true;
    }
    if(file_exists(co_p)) return try_import_cco(co_p, parent_C);
    if(file_exists(cn_p)){
        if(already_imported(cn_p)) return true;
        mark_imported(cn_p); mark_imported(base);
        Chunk dummy;FunctionObject **fns;int nf;
        BCResult r=bc_read_program(cn_p,&dummy,&fns,&nf,NULL,NULL);
        if(r!=BC_OK){fprintf(stderr,"error: load '%s': %s\n\n",cn_p,bc_result_str(r));return false;}
        for(int i=0;i<nf;i++) if(fns[i]->exported&&parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i];
        free(fns); return true;
    }
    if(file_exists(sr_p)){
        if(already_imported(sr_p)) return true;
        mark_imported(sr_p); mark_imported(base);
        return compile_and_import(sr_p,parent_C);
    }
    return false;
}

static bool do_import(const char *rel_path, Compiler *parent_C){
    if(strncmp(rel_path,"::lib:",6)==0){
        const char *cco_name = rel_path + 6;
        char resolved[1024]; resolve_path(parent_C->source_file, cco_name, resolved, sizeof(resolved));
        const char *dot = strrchr(cco_name, '.');
        bool has_cco_ext = dot && strcmp(dot,".cco")==0;

        

        #define RECORD_CCO_IMP(p) do { \
            if(parent_C->imp_name_count < MAX_FUNCS) \
                strncpy(parent_C->imp_names[parent_C->imp_name_count++], (p), 255); \
        } while(0)

        if(has_cco_ext){
            if(file_exists(resolved)){
                bool ok = try_import_cco(resolved, parent_C);
                if(ok) RECORD_CCO_IMP(resolved);
                return ok;
            }
        } else {
            char with_ext[1100]; snprintf(with_ext,sizeof(with_ext),"%s.cco",resolved);
            if(file_exists(with_ext)){
                bool ok = try_import_cco(with_ext, parent_C);
                if(ok) RECORD_CCO_IMP(with_ext);
                return ok;
            }
            char local_dir[1024]="";
            if(parent_C->source_file[0]){
                const char *sl=strrchr(parent_C->source_file,'/');
                if(sl){ size_t dl=(size_t)(sl-parent_C->source_file)+1; memcpy(local_dir,parent_C->source_file,dl); local_dir[dl]='\0'; }
                else strncpy(local_dir,"./",sizeof(local_dir)-1);
            } else strncpy(local_dir,"./",sizeof(local_dir)-1);
            char probe[1200]; snprintf(probe,sizeof(probe),"%s%s.cco",local_dir,cco_name);
            if(file_exists(probe)){
                bool ok = try_import_cco(probe, parent_C);
                if(ok) RECORD_CCO_IMP(probe);
                return ok;
            }
            if(chn_bin_dir[0]){
                char instprobe[1200]; snprintf(instprobe,sizeof(instprobe),"%s/chn-libs/%s.cco",chn_bin_dir,cco_name);
                if(file_exists(instprobe)){
                    bool ok = try_import_cco(instprobe, parent_C);
                    if(ok) RECORD_CCO_IMP(instprobe);
                    return ok;
                }
            }
        }
        #undef RECORD_CCO_IMP
        fprintf(stderr,"error: cannot find CCO bundle '%s'\n\n", cco_name);
        return false;
    }

    char resolved[1024]; resolve_path(parent_C->source_file,rel_path,resolved,sizeof(resolved));
    char base[1024]; strip_ext(resolved,base,sizeof(base));
    const char *sl2=strrchr(rel_path,'/'),*dot2=strrchr(rel_path,'.');
    bool bare_name=!sl2&&!(dot2&&dot2>rel_path);
    if(!bare_name){
        if(already_imported(resolved)||already_imported(base)) return true;
        char fn_p[1100],cn_p[1100],sr_p[1100],co_p[1100];
        snprintf(fn_p,sizeof(fn_p),"%s.function",base);
        snprintf(cn_p,sizeof(cn_p),"%s.chn2",base);
        snprintf(co_p,sizeof(co_p),"%s.cco",base);
        bool has_ext=dot2&&(!sl2||dot2>sl2);
        if(!has_ext) snprintf(sr_p,sizeof(sr_p),"%s.chn",resolved);
        else strncpy(sr_p,resolved,sizeof(sr_p)-1);
        #define RECORD_IMP() do{ if(parent_C->imp_name_count<MAX_FUNCS) \
            strncpy(parent_C->imp_names[parent_C->imp_name_count++],rel_path,255); }while(0)
        if(has_ext && dot2 && strcmp(dot2,".cco")==0){ RECORD_IMP(); return try_import_cco(resolved, parent_C); }
        if(file_exists(fn_p)){ mark_imported(fn_p);mark_imported(base); FunctionObject **fns;int nf; BCResult r=bc_read_functions(fn_p,&fns,&nf); if(r!=BC_OK){fprintf(stderr,"error: %s\n\n",bc_result_str(r));return false;} for(int i=0;i<nf;i++) if(parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i]; free(fns); RECORD_IMP(); return true; }
        if(file_exists(co_p)){ RECORD_IMP(); return try_import_cco(co_p, parent_C); }
        if(file_exists(cn_p)){ mark_imported(cn_p);mark_imported(base); Chunk dummy;FunctionObject **fns;int nf; BCResult r=bc_read_program(cn_p,&dummy,&fns,&nf,NULL,NULL); if(r!=BC_OK){fprintf(stderr,"error: %s\n\n",bc_result_str(r));return false;} for(int i=0;i<nf;i++) if(fns[i]->exported&&parent_C->import_count<MAX_FUNCS) parent_C->imports[parent_C->import_count++]=fns[i]; free(fns); RECORD_IMP(); return true; }
        if(file_exists(sr_p)){ mark_imported(sr_p);mark_imported(base); bool ok=compile_and_import(sr_p,parent_C); if(ok) RECORD_IMP(); return ok; }
        #undef RECORD_IMP
        fprintf(stderr,"error: cannot find import '%s'\n\n",rel_path); return false;
    }
    if(search_chn_libs(rel_path,parent_C)){
        if(parent_C->imp_name_count < MAX_FUNCS)
            strncpy(parent_C->imp_names[parent_C->imp_name_count++], rel_path, 255);
        return true;
    }
    fprintf(stderr,"error: cannot find module '%s'\n\n",rel_path); return false;
}

static bool compile_and_import(const char *src, Compiler *parent_C){
    char *source=read_file(src); if(!source) return false;
    const char *pf=g_source_file,*ps=g_source_code;
    error_init(src,source);
    Parser P; parser_init(&P,source);
    ASTNode *ast=parser_parse(&P);
    if(g_had_error){ast_free(ast);free(source);error_init(pf,ps);return false;}
    Compiler *C=(Compiler*)malloc(sizeof(Compiler));
    if(!C){ast_free(ast);free(source);error_init(pf,ps);
           fprintf(stderr,"ChnMemoryError: out of memory\n");return false;}
    compiler_init(C,src);
    C->global_count=parent_C->global_count;
    for(int i=0;i<parent_C->import_count;i++){
        if(C->import_count>=MAX_FUNCS){
            fprintf(stderr,"ChnImportError: import table full loading '%s' (limit %d)\n",src,MAX_FUNCS);
            free(C);ast_free(ast);free(source);error_init(pf,ps);return false;
        }
        C->imports[C->import_count++]=parent_C->imports[i];
    }
    C->import_handler=chn_import_handler;
    bool ok=compiler_compile(C,ast);
    ast_free(ast); free(source);
    if(ok&&!C->had_error){
        parent_C->global_count=C->global_count;
        for(int i=0;i<C->func_count;i++){
            if(C->functions[i]->exported){
                if(parent_C->import_count>=MAX_FUNCS){
                    fprintf(stderr,"ChnImportError: import table full loading '%s' (limit %d)\n",src,MAX_FUNCS);
                    free(C);error_init(pf,ps);return false;
                }
                parent_C->imports[parent_C->import_count++]=C->functions[i];
            }
        }
    }
    error_init(pf,ps);
    bool result=ok&&!C->had_error;
    free(C);
    return result;
}

bool compile_source(const char *filepath, const char *source, Compiler *C){
    error_init(filepath,source);
    Parser P; parser_init(&P,source);
    ASTNode *ast=parser_parse(&P);
    if(g_had_error){ast_free(ast);return false;}
    int saved_opt=C->opt_level;
    compiler_init(C,filepath);
    C->opt_level=saved_opt;
    C->import_handler=do_import;
    chn_import_handler=do_import;
    bool ok=compiler_compile(C,ast);
    ast_free(ast);
    return ok&&!C->had_error&&!g_had_error;
}

static int run_chunk(Chunk *ch){
    VM vm; vm_init(&vm);
    vm.global_count = ch->var_count;
    VMResult res=vm_run(&vm,ch);
    vm_free(&vm);
    return (res==VM_OK&&!g_had_error)?0:1;
}

typedef enum{MODE_RUN,MODE_DISASM,MODE_AST,MODE_COMPILE_ONLY,MODE_CHECK}RunMode;

static int run_source_file(const char *filepath, RunMode mode, const char *out_path, bool func_only, int opt_level){
    char *source=read_file(filepath); if(!source) return 1;
    if(mode==MODE_AST){
        error_init(filepath,source);
        Parser P; parser_init(&P,source);
        ASTNode *ast=parser_parse(&P);
        if(!g_had_error){printf("=== AST: %s ===\n",filepath);ast_print(ast,0);}
        ast_free(ast); free(source); return g_had_error?1:0;
    }
    Compiler *C=(Compiler*)malloc(sizeof(Compiler));
    if(!C){free(source);fprintf(stderr,"ChnMemoryError: out of memory\n");return 1;}
    C->opt_level=opt_level;
    if(!compile_source(filepath,source,C)){free(source);free(C);return 1;}
    free(source);
    if(mode==MODE_DISASM){
        chunk_disasm(&C->top_chunk,filepath);
        for(int i=0;i<C->func_count;i++){
            char lbl[320]; snprintf(lbl,sizeof(lbl),"%s func %s()",visibility_name(C->functions[i]->visibility),C->functions[i]->name);
            chunk_disasm(&C->functions[i]->chunk,lbl);
        }
        free(C); return 0;
    }
    if(mode==MODE_CHECK){ printf("OK  %s -- no errors\n",filepath); free(C); return 0; }
    if(mode==MODE_COMPILE_ONLY){
        char auto_path[1024]; const char *wpath=out_path;
        if(!wpath){
            strip_ext(filepath,auto_path,sizeof(auto_path));
            strncat(auto_path,func_only?".function":".chn2",sizeof(auto_path)-strlen(auto_path)-1);
            wpath=auto_path;
        }
        BCResult r=func_only?bc_write_functions(wpath,C):bc_write_program(wpath,C);
        if(r!=BC_OK){fprintf(stderr,"error: write '%s': %s\n\n",wpath,bc_result_str(r));free(C);return 1;}
        printf("OK  compiled %s  ->  %s\n",filepath,wpath);
        printf("    %d function(s)  |  %d instruction(s)  |  %s format\n",
               C->func_count, C->top_chunk.code_len, g_minify ? "minified" : "plain");
        free(C); return 0;
    }
    int rc=run_chunk(&C->top_chunk);
    free(C); return rc;
}

static bool load_cco_deps(const char *cco_path){
    char dep_imps[256][256]; int ndep = 0;
    BCResult ri = bc_read_cco_entry_imports(cco_path, dep_imps, &ndep);
    if(ri != BC_OK) return true;
    if(ndep == 0)   return true;

    char cco_dir[1024]; path_dir(cco_path, cco_dir, sizeof(cco_dir));
    Compiler *tmp_C=(Compiler*)malloc(sizeof(Compiler));
    if(!tmp_C){fprintf(stderr,"ChnMemoryError: out of memory\n");return false;}
    compiler_init(tmp_C, cco_path);
    tmp_C->import_handler = do_import;
    chn_import_handler   = do_import;

    bool all_ok = true;
    for(int i = 0; i < ndep; i++){
        if(!dep_imps[i][0]) continue;
        const char *dep_name = dep_imps[i];

        
        if(strchr(dep_name,'/')){
            bool resolved = false;
            if(file_exists(dep_name)){
                resolved = already_imported(dep_name) ? true : try_import_cco(dep_name, tmp_C);
            } else {
                
                const char *dn_dot = strrchr(dep_name,'.');
                if(!dn_dot || strcmp(dn_dot,".cco")!=0){
                    char probe[1300]; snprintf(probe,sizeof(probe),"%s.cco",dep_name);
                    if(file_exists(probe))
                        resolved = already_imported(probe) ? true : try_import_cco(probe, tmp_C);
                }
            }
            if(resolved) continue;
        }

        
        bool is_bare = !strchr(dep_name,'/') && !strchr(dep_name,'.');
        if(is_bare){
            char probe[1200]; snprintf(probe,sizeof(probe),"%s%s.cco",cco_dir,dep_name);
            if(file_exists(probe)){
                if(!already_imported(probe)){
                    bool ok = try_import_cco(probe, tmp_C);
                    if(!ok){
                        fprintf(stderr,
                            "error: unresolved CCO dependency\n"
                            "  importing module: %s\n"
                            "  imported module:  %s\n\n",
                            cco_path, probe);
                        all_ok = false;
                    }
                }
                continue;
            }
        }

        
        char dep_abs[1200]; snprintf(dep_abs,sizeof(dep_abs),"%s%s",cco_dir,dep_name);
        bool found = false;
        char with_cco[1204]; snprintf(with_cco,sizeof(with_cco),"%s.cco",dep_abs);
        if(file_exists(with_cco))
            found = already_imported(with_cco) ? true : try_import_cco(with_cco, tmp_C);
        else if(file_exists(dep_abs))
            found = already_imported(dep_abs) ? true : try_import_cco(dep_abs, tmp_C);
        if(!found) found = do_import(dep_name, tmp_C);
        if(!found){
            fprintf(stderr,
                "error: unresolved CCO dependency\n"
                "  importing module: %s\n"
                "  imported module:  %s\n\n",
                cco_path, dep_name);
            all_ok = false;
        }
    }
    free(tmp_C);
    return all_ok;
}

static int run_cco_file(const char *path){
    gc_init();
    error_init(path, "");

    

    imported_count = 0;
    if(!load_cco_deps(path)){
        gc_free_all(); return 1;
    }

    
    FunctionObject **fns = NULL; int nf = 0;
    Chunk entry_chunk; bool has_entry = false;
    BCResult r = bc_read_cco_entry(path, &fns, &nf, &entry_chunk, &has_entry);
    if(r != BC_OK){
        fprintf(stderr,"error: load '%s': %s\n\n",path,bc_result_str(r));
        gc_free_all(); return 1;
    }

    if(!has_entry){
        fprintf(stderr,
            "error: '%s' is a library CCO -- "
            "it has no entry main and cannot be run directly.\n"
            "       Import it with:  imp::lib %.*s\n\n",
            path,
            (int)(strrchr(path,'.') ? strrchr(path,'.') - path : (int)strlen(path)),
            path);
        free(fns); gc_free_all(); return 1;
    }

    
    for(int i = 0; i < nf; i++) func_register(fns[i]);
    free(fns);

    
    VM vm; vm_init_no_gc(&vm);
    vm.global_count = entry_chunk.var_count;
    VMResult res = vm_run(&vm, &entry_chunk);
    vm_free(&vm);
    gc_free_all();
    return (res == VM_OK && !g_had_error) ? 0 : 1;
}

static int run_chn2_file(const char *path){
    gc_init();
    error_init(path,"");
    char imp_names[256][256]; int nimp=0;
    BCResult r1=bc_read_imports(path, imp_names, &nimp);
    if(r1!=BC_OK){fprintf(stderr,"error: load '%s': %s\n\n",path,bc_result_str(r1));return 1;}
    if(nimp>0){
        imported_count=0;
        Compiler *tmp_C=(Compiler*)malloc(sizeof(Compiler));
        if(!tmp_C){fprintf(stderr,"ChnMemoryError: out of memory\n");return 1;}
        compiler_init(tmp_C, path);
        tmp_C->import_handler=do_import; chn_import_handler=do_import;
        for(int i=0;i<nimp;i++) if(imp_names[i][0]) do_import(imp_names[i], tmp_C);
        free(tmp_C);
    }
    Chunk top; FunctionObject **fns; int nf;
    BCResult r2=bc_read_program(path,&top,&fns,&nf,NULL,NULL);
    if(r2!=BC_OK){fprintf(stderr,"error: load '%s': %s\n\n",path,bc_result_str(r2));return 1;}
    free(fns);
    VM vm; vm_init_no_gc(&vm);
    vm.global_count = top.var_count;
    VMResult res=vm_run(&vm,&top);
    vm_free(&vm); gc_free_all();
    return (res==VM_OK&&!g_had_error)?0:1;
}

static void repl(void){ chn_repl_run(); }

static void usage(const char *p){
    printf("CHN 1.1\n\n"
           "Run:\n"
           "  %s <file.chn>                     Run source file\n"
           "  %s <file.chn> [args...]            Pass runtime arguments (os::args)\n"
           "  %s <file.cco>                     Run CCO with entry main\n\n"
           "Compile:\n"
           "  %s <file.chn> -c <out.chn2>       Compile to bytecode\n"
           "  %s a.chn b.chn -oc out.cco        Bundle into CCO\n\n"
           "Options:\n"
           "  --no-minify     Disable bytecode minification (CCO/CHNO)\n"
           "  --check / -k    Syntax check only\n"
           "  --disasm / -d   Disassemble bytecode\n"
           "  --ast   / -a    Dump AST\n"
           "  -O0/-O1/-O2     Optimisation level (default -O2)\n"
           "  --version / -v  Version\n\n"
           "Runtime args: everything after the entry filename is passed to os::args()\n\n",
           p,p,p,p,p);
}

#define CHN_VERSION  "1.1"
#define MAX_INPUT_FILES 64

static bool is_chn_flag(const char *a){
    if(!strcmp(a,"--help")||!strcmp(a,"-h"))   return true;
    if(!strcmp(a,"--version")||!strcmp(a,"-v")) return true;
    if(!strcmp(a,"--disasm")||!strcmp(a,"-d")) return true;
    if(!strcmp(a,"--ast")||!strcmp(a,"-a"))    return true;
    if(!strcmp(a,"--check")||!strcmp(a,"-k"))  return true;
    if(!strcmp(a,"--func")||!strcmp(a,"-f"))   return true;
    if(!strcmp(a,"-c"))                        return true;
    if(!strcmp(a,"-oc"))                       return true;
    if(!strcmp(a,"--no-minify"))               return true;
    if(!strncmp(a,"-O",2)&&a[2]>='0'&&a[2]<='2') return true;
    return false;
}

int main(int argc, char **argv){
    {
        char self[1024]="";
        ssize_t n=readlink("/proc/self/exe",self,sizeof(self)-1);
        if(n>0){ self[n]='\0'; char *sl=strrchr(self,'/'); if(sl){ *sl='\0'; strncpy(chn_bin_dir,self,sizeof(chn_bin_dir)-1); } }
    }

    if(argc==1){ chn_repl_run(); return 0; }

    RunMode     mode        = MODE_RUN;
    const char *input_files[MAX_INPUT_FILES];
    int         input_count = 0;
    const char *out_path    = NULL;
    bool        func_only   = false;
    bool        cco_mode    = false;
    int         opt_level   = 2;

    for(int i = 1; i < argc; i++){
        if(!strcmp(argv[i],"-oc")){ cco_mode = true; break; }
    }

    

    bool   entry_seen = false;
    char  *runtime_args[512];
    int    runtime_argc = 0;

    for(int i = 1; i < argc; i++){
        const char *a = argv[i];

        

        if(entry_seen && !cco_mode){
            if(!strcmp(a,"--")){
                for(int j=i+1; j<argc && runtime_argc<512; j++)
                    runtime_args[runtime_argc++] = argv[j];
                break;
            }
            
            if(is_chn_flag(a)) {  }
            else {
                if(runtime_argc < 512) runtime_args[runtime_argc++] = argv[i];
                continue;
            }
        }

        if(!strcmp(a,"--help")||!strcmp(a,"-h")){ usage(argv[0]); return 0; }
        else if(!strcmp(a,"--version")||!strcmp(a,"-v")){
            printf("CHN %s\n", CHN_VERSION); return 0;
        }
        else if(!strcmp(a,"--disasm")||!strcmp(a,"-d")) mode=MODE_DISASM;
        else if(!strncmp(a,"-O",2)){
            if(a[2]>='0'&&a[2]<='2') opt_level=a[2]-'0';
            else{ fprintf(stderr,"error: unknown optimisation level '%s'\n  use -O0, -O1, or -O2\n",a); return 1; }
        }
        else if(!strcmp(a,"--ast")||!strcmp(a,"-a"))   mode=MODE_AST;
        else if(!strcmp(a,"--check")||!strcmp(a,"-k")) mode=MODE_CHECK;
        else if(!strcmp(a,"--no-minify")){
            g_minify = false;
        }
        else if(!strcmp(a,"-c")){
            mode=MODE_COMPILE_ONLY;
            
            if(i+1<argc && !is_chn_flag(argv[i+1]) && argv[i+1][0]!='-')
                out_path=argv[++i];
        }
        else if(!strcmp(a,"-oc")){
            cco_mode=true; mode=MODE_COMPILE_ONLY;
            if(i+1<argc){ out_path=argv[++i]; }
            else{ fprintf(stderr,"error: -oc requires an output filename\n"); return 1; }
        }
        else if(!strcmp(a,"--func")||!strcmp(a,"-f")) func_only=true;
        else if(!strcmp(a,"--")){
            
            if(!cco_mode){
                for(int j=i+1; j<argc && runtime_argc<512; j++)
                    runtime_args[runtime_argc++] = argv[j];
                break;
            }
        }
        else if(a[0]!='-'){
            
            if(input_count < MAX_INPUT_FILES){
                input_files[input_count++] = a;
                if(!cco_mode) entry_seen = true; 
            } else {
                fprintf(stderr,"error: too many input files (max %d)\n",MAX_INPUT_FILES);
                return 1;
            }
        }
        else{
            

            fprintf(stderr,"error: unknown option '%s'\n\n",a);
            return 1;
        }
    }

    
    g_runtime_argc = runtime_argc;
    g_runtime_argv = runtime_argc > 0 ? runtime_args : NULL;

    if(input_count==0){ fprintf(stderr,"error: no input file\n"); usage(argv[0]); return 1; }

    if(cco_mode){
        if(!out_path){ fprintf(stderr,"error: -oc requires an output filename\n"); return 1; }

        char      **p1_sources=(char**)     calloc(MAX_INPUT_FILES,sizeof(char*));
        ASTNode   **p1_asts   =(ASTNode**)  calloc(MAX_INPUT_FILES,sizeof(ASTNode*));
        Compiler  **p1_C      =(Compiler**) calloc(MAX_INPUT_FILES,sizeof(Compiler*));
        const char**p1_names  =(const char**)calloc(MAX_INPUT_FILES,sizeof(char*));
        int p1_count=0;
        if(!p1_sources||!p1_asts||!p1_C||!p1_names){
            free(p1_sources);free(p1_asts);free(p1_C);free(p1_names);
            fprintf(stderr,"error: out of memory\n"); return 1;
        }

        for(int i=0;i<input_count;i++){
            const char *fp=input_files[i];
            const char *ext=strrchr(fp,'.');
            if(ext&&(strcmp(ext,".chn2")==0||strcmp(ext,".chno")==0||strcmp(ext,".cco")==0)){
                fprintf(stderr,"skip: '%s' already compiled -- only .chn for -oc\n",fp); continue;
            }
            char *source=read_file(fp);
            if(!source){ fprintf(stderr,"error: cannot read '%s'\n",fp); continue; }
            const char *prev_src=g_source_file, *prev_code=g_source_code;
            error_init(fp,source);
            Parser P; parser_init(&P,source);
            ASTNode *ast=parser_parse(&P);
            if(g_had_error){ ast_free(ast);free(source);error_init(prev_src,prev_code);continue; }
            error_init(prev_src,prev_code);
            p1_C[p1_count]=(Compiler*)calloc(1,sizeof(Compiler));
            if(!p1_C[p1_count]){ ast_free(ast);free(source);continue; }
            compiler_init(p1_C[p1_count],fp);
            p1_C[p1_count]->opt_level=opt_level;
            p1_C[p1_count]->import_handler=do_import;
            chn_import_handler=do_import;
            g_had_error=false; g_suppress_errors=true;
            bool ok=compiler_compile(p1_C[p1_count],ast);
            g_suppress_errors=false; g_had_error=false; (void)ok;
            p1_sources[p1_count]=source; p1_asts[p1_count]=ast;
            p1_names[p1_count]=fp; p1_count++;
        }

        if(p1_count==0){ fprintf(stderr,"error: no files parsed\n"); return 1; }

        Compiler  *compilers[MAX_INPUT_FILES]={0};
        const char *valid_names[MAX_INPUT_FILES];
        int ok_count=0; bool any_internal=false;

        for(int i=0;i<p1_count;i++){
            compilers[ok_count]=(Compiler*)calloc(1,sizeof(Compiler));
            if(!compilers[ok_count]) continue;
            compiler_init(compilers[ok_count],p1_names[i]);
            compilers[ok_count]->opt_level=opt_level;
            compilers[ok_count]->import_handler=do_import;
            compilers[ok_count]->is_bundle_pass=true;
            chn_import_handler=do_import;
            for(int j=0;j<p1_count;j++){
                if(j==i) continue;
                for(int k=0;k<p1_C[j]->func_count;k++){
                    FunctionObject *f=p1_C[j]->functions[k];
                    if(compilers[ok_count]->import_count<MAX_FUNCS)
                        compilers[ok_count]->imports[compilers[ok_count]->import_count++]=f;
                }
            }
            for(int k=0;k<p1_C[i]->import_count;k++){
                FunctionObject *f=p1_C[i]->imports[k];
                bool already=false;
                for(int m=0;m<compilers[ok_count]->import_count;m++)
                    if(compilers[ok_count]->imports[m]==f){already=true;break;}
                if(!already&&compilers[ok_count]->import_count<MAX_FUNCS)
                    compilers[ok_count]->imports[compilers[ok_count]->import_count++]=f;
            }
            const char *prev_src=g_source_file, *prev_code=g_source_code;
            error_init(p1_names[i],p1_sources[i]);
            bool ok=compiler_compile(compilers[ok_count],p1_asts[i]);
            error_init(prev_src,prev_code);
            if(!ok||compilers[ok_count]->had_error){
                fprintf(stderr,"error: compilation failed for '%s'\n",p1_names[i]);
                free(compilers[ok_count]); compilers[ok_count]=NULL; continue;
            }
            for(int k=0;k<compilers[ok_count]->func_count;k++){
                FunctionObject *p2f=compilers[ok_count]->functions[k];
                for(int j2=0;j2<p1_count;j2++){
                    if(!p1_C[j2]) continue;
                    for(int m=0;m<p1_C[j2]->func_count;m++){
                        FunctionObject *p1f=p1_C[j2]->functions[m];
                        if(p1f&&strcmp(p1f->name,p2f->name)==0&&p1f->arity==p2f->arity){
                            p1f->chunk=p2f->chunk; p1f->exported=p2f->exported;
                            p1f->visibility=p2f->visibility;
                        }
                    }
                }
            }
            for(int k=0;k<compilers[ok_count]->func_count;k++)
                if(!compilers[ok_count]->functions[k]->exported){any_internal=true;break;}
            valid_names[ok_count]=p1_names[i]; ok_count++;
        }

        for(int i=0;i<p1_count;i++){ ast_free(p1_asts[i]); free(p1_sources[i]); if(p1_C[i]) free(p1_C[i]); }
        free(p1_sources); free(p1_asts); free(p1_C); free(p1_names);

        if(ok_count==0){ fprintf(stderr,"error: no files compiled -- CCO not written\n"); return 1; }

        BCResult r=bc_write_cco(out_path,compilers,valid_names,ok_count);
        for(int i=0;i<ok_count;i++) free(compilers[i]);
        if(r!=BC_OK){ fprintf(stderr,"error: write CCO '%s': %s\n\n",out_path,bc_result_str(r)); return 1; }

        printf("OK  %s  <-- CCO bundle%s\n",
               out_path, any_internal?" (intra-bundle linkage)":"");
        printf("    %d source file(s) compiled and packed:\n",ok_count);
        for(int i=0;i<ok_count;i++) printf("      + %s\n",valid_names[i]);
        char hint[1024]; strncpy(hint,out_path,sizeof(hint)-1); hint[sizeof(hint)-1]='\0';
        char *hdot=strrchr(hint,'.'); if(hdot) *hdot='\0';
        printf("    Import with:  imp::lib %s\n",hint);
        if(!g_minify) printf("    [--no-minify: plain output]\n");
        return 0;
    }

    const char *filepath=input_files[0];
    if(input_count>1&&mode!=MODE_COMPILE_ONLY){
        fprintf(stderr,"warning: multiple files given without -oc; only '%s' will be processed.\n",filepath);
    }

    const char *dot=strrchr(filepath,'.');
    bool is_cco  = dot && !strcmp(dot,".cco");
    bool is_chn2 = dot && (!strcmp(dot,".chnc")||!strcmp(dot,".chno")||!strcmp(dot,".chn2"));

    if(is_cco){
        if(mode==MODE_COMPILE_ONLY){ fprintf(stderr,"error: cannot re-compile a CCO\n\n"); return 1; }
        return run_cco_file(filepath);
    }
    if(is_chn2){
        if(mode==MODE_COMPILE_ONLY){ fprintf(stderr,"error: cannot re-compile bytecode\n\n"); return 1; }
        return run_chn2_file(filepath);
    }
    return run_source_file(filepath,mode,out_path,func_only,opt_level);
}
