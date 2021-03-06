#include "stubs.h"
#include "ruby.h"
#include <tcl.h>
#include <tk.h>

/*------------------------------*/

#ifdef __MACOS__
# include <tkMac.h>
# include <Quickdraw.h>

static int call_macinit = 0;

static void
_macinit()
{
    if (!call_macinit) {
        tcl_macQdPtr = &qd; /* setup QuickDraw globals */
        Tcl_MacSetEventProc(TkMacConvertEvent); /* setup event handler */
        call_macinit = 1;
    }
}
#endif

/*------------------------------*/

static int nativethread_checked = 0;

static void
_nativethread_consistency_check(ip)
    Tcl_Interp *ip;
{
    if (nativethread_checked || ip == (Tcl_Interp *)NULL) {
        return;
    }

    /* If the variable "tcl_platform(threaded)" exists,  
       then the Tcl interpreter was compiled with threads enabled. */
    if (Tcl_GetVar2(ip, "tcl_platform", "threaded", TCL_GLOBAL_ONLY) != (char*)NULL) {
#ifdef HAVE_NATIVETHREAD
        /* consistent */
#else
        rb_warn("Inconsistency. Loaded Tcl/Tk libraries are enabled nativethread-support. But `tcltklib' is not. The inconsistency causes SEGV or other troubles frequently.");
#endif
    } else {
#ifdef HAVE_NATIVETHREAD
        rb_warning("Inconsistency.`tcltklib' is enabled nativethread-support. But loaded Tcl/Tk libraries are not. (Probably, the inconsistency doesn't cause any troubles.)");
#else
        /* consistent */
#endif
    }

    Tcl_ResetResult(ip);

    nativethread_checked = 1;
}

/*------------------------------*/

#if defined USE_TCL_STUBS && defined USE_TK_STUBS

#if defined _WIN32 || defined __CYGWIN__
# include "util.h"
# include <windows.h>
  typedef HINSTANCE DL_HANDLE;
# define DL_OPEN LoadLibrary
# define DL_SYM GetProcAddress
# define TCL_INDEX 4
# define TK_INDEX 3
# define TCL_NAME "tcl89%s"
# define TK_NAME "tk89%s"
# undef DLEXT
# define DLEXT ".dll"
#elif defined HAVE_DLOPEN
# include <dlfcn.h>
  typedef void *DL_HANDLE;
# define DL_OPEN(file) dlopen(file, RTLD_LAZY|RTLD_GLOBAL)
# define DL_SYM dlsym
# define TCL_INDEX 8
# define TK_INDEX 7
# define TCL_NAME "libtcl8.9%s"
# define TK_NAME "libtk8.9%s"
#endif

static DL_HANDLE tcl_dll = (DL_HANDLE)0;
static DL_HANDLE tk_dll  = (DL_HANDLE)0;

int
ruby_open_tcl_dll(appname)
    char *appname;
{
    void (*p_Tcl_FindExecutable)(const char *);
    int n;
    char *ruby_tcl_dll = 0;
    char tcl_name[20];

    if (tcl_dll) return TCLTK_STUBS_OK;

    ruby_tcl_dll = getenv("RUBY_TCL_DLL");
#if defined _WIN32
    if (ruby_tcl_dll) ruby_tcl_dll = ruby_strdup(ruby_tcl_dll);
#endif
    if (ruby_tcl_dll) {
        tcl_dll = (DL_HANDLE)DL_OPEN(ruby_tcl_dll);
    } else {
        snprintf(tcl_name, sizeof tcl_name, TCL_NAME, DLEXT);
        /* examine from 8.9 to 8.1 */
        for (n = '9'; n > '0'; n--) {
            tcl_name[TCL_INDEX] = n;
            tcl_dll = (DL_HANDLE)DL_OPEN(tcl_name);
            if (tcl_dll)
                break;
        }
    }

#if defined _WIN32
    if (ruby_tcl_dll) ruby_xfree(ruby_tcl_dll);
#endif

    if (!tcl_dll)
        return NO_TCL_DLL;

    p_Tcl_FindExecutable = (void (*)(const char *))DL_SYM(tcl_dll, "Tcl_FindExecutable");
    if (!p_Tcl_FindExecutable)
        return NO_FindExecutable;

    if (appname) {
        p_Tcl_FindExecutable(appname);
    } else {
        p_Tcl_FindExecutable("ruby");
    }

    return TCLTK_STUBS_OK;
}

int
ruby_open_tk_dll()
{
    int n;
    char *ruby_tk_dll = 0;
    char tk_name[20];

    if (!tcl_dll) {
        /* int ret = ruby_open_tcl_dll(RSTRING(rb_argv0)->ptr); */
        int ret = ruby_open_tcl_dll(rb_argv0 ? RSTRING(rb_argv0)->ptr : 0);
        if (ret != TCLTK_STUBS_OK) return ret;
    }

    if (tk_dll) return TCLTK_STUBS_OK;

    ruby_tk_dll = getenv("RUBY_TK_DLL");
    if (ruby_tk_dll) {
        tk_dll = (DL_HANDLE)DL_OPEN(ruby_tk_dll);
    } else {
        snprintf(tk_name, sizeof tk_name, TK_NAME, DLEXT);
        /* examine from 8.9 to 8.1 */
        for (n = '9'; n > '0'; n--) {
            tk_name[TK_INDEX] = n;
            tk_dll = (DL_HANDLE)DL_OPEN(tk_name);
            if (tk_dll)
                break;
        }
    }

    if (!tk_dll)
        return NO_TK_DLL;

    return TCLTK_STUBS_OK;
}

int
ruby_open_tcltk_dll(appname)
    char *appname;
{
    return( ruby_open_tcl_dll(appname) || ruby_open_tk_dll() );
}

int 
tcl_stubs_init_p()
{
    return(tclStubsPtr != (TclStubs*)NULL);
}

int 
tk_stubs_init_p()
{
    return(tkStubsPtr != (TkStubs*)NULL);
}


Tcl_Interp *
ruby_tcl_create_ip_and_stubs_init(st)
    int *st;
{
    Tcl_Interp *tcl_ip;

    if (st) *st = 0;

    if (tcl_stubs_init_p()) {
        tcl_ip = Tcl_CreateInterp();

        if (!tcl_ip) {
            if (st) *st = FAIL_CreateInterp;
            return (Tcl_Interp*)NULL;
        }

        _nativethread_consistency_check(tcl_ip);

        return tcl_ip;

    } else {
        Tcl_Interp *(*p_Tcl_CreateInterp)();
        Tcl_Interp *(*p_Tcl_DeleteInterp)();

        if (!tcl_dll) {
            /* int ret = ruby_open_tcl_dll(RSTRING(rb_argv0)->ptr); */
            int ret = ruby_open_tcl_dll(rb_argv0 ? RSTRING(rb_argv0)->ptr : 0);

            if (ret != TCLTK_STUBS_OK) {
                if (st) *st = ret;
                return (Tcl_Interp*)NULL;
            }
        }

        p_Tcl_CreateInterp 
            = (Tcl_Interp *(*)())DL_SYM(tcl_dll, "Tcl_CreateInterp");
        if (!p_Tcl_CreateInterp) {
            if (st) *st = NO_CreateInterp;
            return (Tcl_Interp*)NULL;
        }

        p_Tcl_DeleteInterp 
            = (Tcl_Interp *(*)())DL_SYM(tcl_dll, "Tcl_DeleteInterp");
        if (!p_Tcl_DeleteInterp) {
            if (st) *st = NO_DeleteInterp;
            return (Tcl_Interp*)NULL;
        }

        tcl_ip = (*p_Tcl_CreateInterp)();
        if (!tcl_ip) {
            if (st) *st = FAIL_CreateInterp;
            return (Tcl_Interp*)NULL;
        }

        if (!Tcl_InitStubs(tcl_ip, "8.1", 0)) {
            if (st) *st = FAIL_Tcl_InitStubs;
            (*p_Tcl_DeleteInterp)(tcl_ip);
            return (Tcl_Interp*)NULL;
        }

        _nativethread_consistency_check(tcl_ip);

        return tcl_ip;
    }
}

int
ruby_tcl_stubs_init()
{
    int st;
    Tcl_Interp *tcl_ip;

    if (!tcl_stubs_init_p()) {
        tcl_ip = ruby_tcl_create_ip_and_stubs_init(&st);

        if (!tcl_ip) return st;

        Tcl_DeleteInterp(tcl_ip);
    }

    return TCLTK_STUBS_OK;
}

int
ruby_tk_stubs_init(tcl_ip)
    Tcl_Interp *tcl_ip;
{
    Tcl_ResetResult(tcl_ip);

    if (tk_stubs_init_p()) {
        if (Tk_Init(tcl_ip) == TCL_ERROR) {
            return FAIL_Tk_Init;
        }
    } else {
        int (*p_Tk_Init)(Tcl_Interp *);

        if (!tk_dll) {
            int ret = ruby_open_tk_dll();
            if (ret != TCLTK_STUBS_OK) return ret;
        }

        p_Tk_Init = (int (*)(Tcl_Interp *))DL_SYM(tk_dll, "Tk_Init");
        if (!p_Tk_Init)
            return NO_Tk_Init;

        if ((*p_Tk_Init)(tcl_ip) == TCL_ERROR)
            return FAIL_Tk_Init;

        if (!Tk_InitStubs(tcl_ip, "8.1", 0))
            return FAIL_Tk_InitStubs;

#ifdef __MACOS__
        _macinit();
#endif
    }

    return TCLTK_STUBS_OK;
}

int
ruby_tk_stubs_safeinit(tcl_ip)
    Tcl_Interp *tcl_ip;
{
    Tcl_ResetResult(tcl_ip);

    if (tk_stubs_init_p()) {
        if (Tk_SafeInit(tcl_ip) == TCL_ERROR)
            return FAIL_Tk_Init;
    } else {
        int (*p_Tk_SafeInit)(Tcl_Interp *);

        if (!tk_dll) {
            int ret = ruby_open_tk_dll();
            if (ret != TCLTK_STUBS_OK) return ret;
        }

        p_Tk_SafeInit = (int (*)(Tcl_Interp *))DL_SYM(tk_dll, "Tk_SafeInit");
        if (!p_Tk_SafeInit)
            return NO_Tk_Init;

        if ((*p_Tk_SafeInit)(tcl_ip) == TCL_ERROR)
            return FAIL_Tk_Init;

        if (!Tk_InitStubs(tcl_ip, "8.1", 0))
            return FAIL_Tk_InitStubs;

#ifdef __MACOS__
        _macinit();
#endif
    }

    return TCLTK_STUBS_OK;
}

int
ruby_tcltk_stubs()
{
    int st;
    Tcl_Interp *tcl_ip;

    /* st = ruby_open_tcltk_dll(RSTRING(rb_argv0)->ptr); */
    st = ruby_open_tcltk_dll(rb_argv0 ? RSTRING(rb_argv0)->ptr : 0);
    switch(st) {
    case NO_FindExecutable:
        return -7;
    case NO_TCL_DLL:
    case NO_TK_DLL:
        return -1;
    }

    tcl_ip = ruby_tcl_create_ip_and_stubs_init(&st);
    if (!tcl_ip) {
        switch(st) {
        case NO_CreateInterp:
        case NO_DeleteInterp:
            return -2;
        case FAIL_CreateInterp:
            return -3;
        case FAIL_Tcl_InitStubs:
            return -5;
        }
    }

    st = ruby_tk_stubs_init(tcl_ip);
    switch(st) {
    case NO_Tk_Init:
        Tcl_DeleteInterp(tcl_ip);
        return -4;
    case FAIL_Tk_Init:
    case FAIL_Tk_InitStubs:
        Tcl_DeleteInterp(tcl_ip);
        return -6;
    }

    Tcl_DeleteInterp(tcl_ip);

    return 0;
}

/*###################################################*/
#else /* ! USE_TCL_STUBS || ! USE_TK_STUBS) */
/*###################################################*/

static int open_tcl_dll = 0;
static int call_tk_stubs_init = 0;

int
ruby_open_tcl_dll(appname)
    char *appname;
{
    if (appname) {
        Tcl_FindExecutable(appname);
    } else {
        Tcl_FindExecutable("ruby");
    }
    open_tcl_dll = 1;

    return TCLTK_STUBS_OK;
}

int ruby_open_tk_dll()
{
    if (!open_tcl_dll) {
        /* ruby_open_tcl_dll(RSTRING(rb_argv0)->ptr); */
        ruby_open_tcl_dll(rb_argv0 ? RSTRING(rb_argv0)->ptr : 0);
    }

    return TCLTK_STUBS_OK;
}

int ruby_open_tcltk_dll(appname)
    char *appname;
{
    return( ruby_open_tcl_dll(appname) || ruby_open_tk_dll() );
}

int 
tcl_stubs_init_p()
{
    return 1;
}

int 
tk_stubs_init_p()
{
    return call_tk_stubs_init;
}

Tcl_Interp *
ruby_tcl_create_ip_and_stubs_init(st)
    int *st;
{
    Tcl_Interp *tcl_ip;

    if (!open_tcl_dll) {
        /* ruby_open_tcl_dll(RSTRING(rb_argv0)->ptr); */
        ruby_open_tcl_dll(rb_argv0 ? RSTRING(rb_argv0)->ptr : 0);
    }

    if (st) *st = 0;
    tcl_ip = Tcl_CreateInterp();
    if (!tcl_ip) {
        if (st) *st = FAIL_CreateInterp;
        return (Tcl_Interp*)NULL;
    }

    _nativethread_consistency_check(tcl_ip);

    return tcl_ip;
}

int 
ruby_tcl_stubs_init()
{
    return TCLTK_STUBS_OK;
}

int 
ruby_tk_stubs_init(tcl_ip)
    Tcl_Interp *tcl_ip;
{
    if (Tk_Init(tcl_ip) == TCL_ERROR)
        return FAIL_Tk_Init;

    if (!call_tk_stubs_init) {
#ifdef __MACOS__
        _macinit();
#endif
        call_tk_stubs_init = 1;
    }

    return TCLTK_STUBS_OK;
}

int
ruby_tk_stubs_safeinit(tcl_ip)
    Tcl_Interp *tcl_ip;
{
#if TCL_MAJOR_VERSION >= 8
    if (Tk_SafeInit(tcl_ip) == TCL_ERROR)
        return FAIL_Tk_Init;

    if (!call_tk_stubs_init) {
#ifdef __MACOS__
        _macinit();
#endif
        call_tk_stubs_init = 1;
    }

    return TCLTK_STUBS_OK;

#else /* TCL_MAJOR_VERSION < 8 */

    return FAIL_Tk_Init;
#endif
}

int 
ruby_tcltk_stubs()
{
    /* Tcl_FindExecutable(RSTRING(rb_argv0)->ptr); */
    Tcl_FindExecutable(rb_argv0 ? RSTRING(rb_argv0)->ptr : 0);
    return 0;
}

#endif
