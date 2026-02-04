#include "kstd.h"
#include "kvm.h"
#include "kapi.h"
#include "kgc.h" // Include KGC for kgc_alloc
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <wininet.h>
#include <direct.h>
#include <io.h>
#define getcwd _getcwd
#define mkdir(p) _mkdir(p)
#define rmdir _rmdir
#define unlink _unlink
#define access _access
#else
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#define mkdir(p) mkdir(p, 0755)
#endif

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

static char* to_string(KValue v) {
    char buf[64];
    if (v.type == VAL_INT) sprintf(buf, "%lld", v.as.integer);
    else if (v.type == VAL_FLOAT) sprintf(buf, "%f", v.as.single_prec);
    else if (v.type == VAL_DOUBLE) sprintf(buf, "%lf", v.as.double_prec);
    else if (v.type == VAL_BOOL) sprintf(buf, "%s", v.as.boolean ? "true" : "false");
    else if (v.type == VAL_STRING) return strdup(v.as.str);
    else if (v.type == VAL_OBJ) {
        KObj* obj = (KObj*)v.as.obj;
        if (obj->header.type == OBJ_STRING) return strdup(((KObjString*)obj)->chars);
        return strdup("[Object]");
    }
    else return strdup("null");
    return strdup(buf);
}

// Access VM internals safely
#if defined(_MSC_VER)
extern __declspec(thread) KVM* g_current_vm;
#else
extern __thread KVM* g_current_vm;
#endif

static KVM* get_vm() {
    return g_current_vm;
}

// Forward declaration
void kstd_register();

static int get_arg_start() {
    KVM* vm = get_vm();
    if (vm && vm->native_argc > 0) {
        KValue v = vm->native_args[0];
        if (v.type == VAL_OBJ && ((KObj*)v.as.obj)->header.type == OBJ_CLASS_INSTANCE) {
             // Check if it's a module (klass == NULL)
             if (((KObjInstance*)v.as.obj)->klass == NULL) return 1;
        }
    }
    return 0;
}

static KObjArray* get_arg_array(int index) {
    KVM* vm = get_vm();
    if (!vm || !vm->native_args) return NULL;
    // Bounds check
    if (index >= vm->native_argc) return NULL;
    
    KValue val = vm->native_args[index];
    if (val.type == VAL_OBJ && ((KObj*)val.as.obj)->header.type == OBJ_ARRAY) {
        return (KObjArray*)val.as.obj;
    }
    return NULL;
}

static KObjInstance* get_arg_instance(int index) {
    KVM* vm = get_vm();
    if (!vm || !vm->native_args) return NULL;
    if (index >= vm->native_argc) return NULL;
    
    KValue val = vm->native_args[index];
    if (val.type == VAL_OBJ && ((KObj*)val.as.obj)->header.type == OBJ_CLASS_INSTANCE) {
        return (KObjInstance*)val.as.obj;
    }
    return NULL;
}

static void push_value(KValue v) {
    KVM* vm = get_vm();
    if (vm) kvm_push(vm, v);
}

// Helper to create a new String Object directly
static KObjString* alloc_string(KVM* vm, const char* chars, int length) {
    KObjString* str = (KObjString*)kgc_alloc(vm->gc, sizeof(KObjString) - sizeof(KObjHeader), OBJ_STRING);
    
    str->length = length;
    str->chars = (char*)malloc(length + 1);
    memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
    str->hash = 0;
    return str;
}

// Helper to create a new Array Object
static KObjArray* alloc_array(KVM* vm, int length) {
    // Temporary: Use malloc to isolate GC issues
    KObjArray* arr = (KObjArray*)malloc(sizeof(KObjArray));
    arr->header.type = OBJ_ARRAY;
    arr->header.marked = false;
    arr->header.next = NULL; // Don't track in GC list
    
    arr->length = length;
    arr->capacity = length > 0 ? length : 0;
    if (length > 0) {
        arr->elements = (KValue*)malloc(sizeof(KValue) * length);
        if (arr->elements == NULL) {
             arr->length = 0;
             arr->capacity = 0;
        } else {
            // Init with null
            for(int i=0; i<length; i++) arr->elements[i].type = VAL_NULL;
        }
    } else {
        arr->elements = NULL;
    }
    return arr;
}

// Helper to create a new Instance Object (acting as Map/Object)
static KObjInstance* alloc_instance(KVM* vm, KObjClass* klass) {
    KObjInstance* ins = (KObjInstance*)kgc_alloc(vm->gc, sizeof(KObjInstance) - sizeof(KObjHeader), OBJ_CLASS_INSTANCE);
    
    ins->klass = klass; 
    init_table(&ins->fields);
    return ins;
}

// -------------------------------------------------------------------------
// OS 庫
// -------------------------------------------------------------------------

static void std_os_print() {
    int start = get_arg_start();
    int count = KGetArgCount();
    for (int i = start; i < count; i++) {
        KValue v = get_vm()->native_args[i];
        char* s = to_string(v);
        printf("%s", s);
        free(s);
    }
    fflush(stdout);
    KReturnVoid();
}

static void std_os_println() {
    int start = get_arg_start();
    int count = KGetArgCount();
    for (int i = start; i < count; i++) {
        KValue v = get_vm()->native_args[i];
        char* s = to_string(v);
        printf("%s", s);
        free(s);
    }
    printf("\n");
    fflush(stdout);
    KReturnVoid();
}

static void std_os_input() {
    // Ignore self if present
    char buffer[1024];
    if (fgets(buffer, sizeof(buffer), stdin)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
        KReturnString(buffer);
    } else {
        KReturnString("");
    }
}

static void std_os_system() {
    int start = get_arg_start();
    KString cmd = KGetArgString(start);
    if (cmd) system(cmd);
    KReturnVoid();
}

static void std_os_printf() {
    int start = get_arg_start();
    KString fmt = KGetArgString(start);
    if (!fmt) { KReturnVoid(); return; }
    
    int arg_count = KGetArgCount();
    int current_arg = start + 1;
    
    for (const char* p = fmt; *p; p++) {
        if (*p == '{') {
            const char* end = strchr(p, '}');
            if (end) {
                if (current_arg < arg_count) {
                    KValue val = get_vm()->native_args[current_arg++];
                    char* s = to_string(val);
                    printf("%s", s);
                    free(s);
                } else {
                    // Not enough args, print placeholder raw? or empty?
                    // Let's print the placeholder raw to be safe, or just nothing?
                    // Common behavior: print placeholder if missing arg
                    fwrite(p, 1, end - p + 1, stdout);
                }
                p = end;
            } else {
                putchar(*p);
            }
        } else {
            putchar(*p);
        }
    }
    fflush(stdout); // Ensure output is visible immediately
    KReturnVoid();
}

static void std_os_open() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    // KString mode = KGetArgString(start + 1); // ignored
    if (!path) { KReturnString(""); return; }
    
    FILE* f = fopen(path, "rb");
    if (!f) { KReturnString(""); return; }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buf = (char*)malloc(fsize + 1);
    fread(buf, 1, fsize, f);
    buf[fsize] = '\0';
    fclose(f);
    
    KReturnString(buf);
    free(buf);
}

static void std_os_write() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    KString content = KGetArgString(start + 1);
    if (!path || !content) { KReturnVoid(); return; }
    
    FILE* f = fopen(path, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
    KReturnVoid();
}

static void std_os_writeBytes() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    KObjArray* arr = get_arg_array(start + 1);
    if (!path || !arr) { KReturnVoid(); return; }
    
    FILE* f = fopen(path, "wb");
    if (f) {
        for (int i=0; i<arr->length; i++) {
            KValue v = arr->elements[i];
            uint8_t b = 0;
            if (v.type == VAL_INT) b = (uint8_t)v.as.integer;
            fwrite(&b, 1, 1, f);
        }
        fclose(f);
    }
    KReturnVoid();
}

static void std_os_mkDir() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    if (path) mkdir(path);
    KReturnVoid();
}

static void std_os_rmDir() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    if (path) rmdir(path);
    KReturnVoid();
}

static void std_os_rmFile() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    if (path) unlink(path);
    KReturnVoid();
}

static void std_os_mkFile() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    if (path) {
        FILE* f = fopen(path, "w");
        if (f) fclose(f);
    }
    KReturnVoid();
}

static void std_os_getMainFilePath() {
    KReturnString("main.k");
}

static void std_os_getOSName() {
#ifdef _WIN32
    KReturnString("Windows");
#else
    KReturnString("POSIX");
#endif
}

static void std_os_getOSVersion() {
    KReturnString("1.0");
}

static void std_os_getOSArch() {
#ifdef _WIN64
    KReturnString("x64");
#else
    KReturnString("x86");
#endif
}

// -------------------------------------------------------------------------
// Time 庫
// -------------------------------------------------------------------------

static void std_time_now() {
    time_t t = time(NULL);
    KReturnInt((KInt)t * 1000);
}

static void std_time_ticks() {
#ifdef _WIN32
    KReturnInt((KInt)GetTickCount64());
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    KReturnInt((KInt)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000));
#endif
}

static void std_time_sleep() {
    int start = get_arg_start();
    KInt ms = KGetArgInt(start);
#ifdef _WIN32
    HANDLE timer = CreateWaitableTimer(NULL, TRUE, NULL);
    if (timer) {
        LARGE_INTEGER li;
        li.QuadPart = -ms * 10000LL;
        SetWaitableTimer(timer, &li, 0, NULL, NULL, 0);
        WaitForSingleObject(timer, INFINITE);
        CloseHandle(timer);
    } else {
        Sleep((DWORD)ms);
    }
#else
    usleep(ms * 1000);
#endif
    KReturnVoid();
}

static void std_time_format() {
    int start = get_arg_start();
    KInt ts = KGetArgInt(start) / 1000;
    KString fmt = KGetArgString(start + 1);
    if (!fmt) { KReturnString(""); return; }
    
    time_t t = (time_t)ts;
    struct tm* tm_info = localtime(&t);
    char buf[128];
    strftime(buf, sizeof(buf), fmt, tm_info);
    KReturnString(buf);
}

static void std_time_parse() {
    KReturnInt(0);
}

static void std_time_getYear() {
    int start = get_arg_start();
    time_t t = (time_t)(KGetArgInt(start) / 1000);
    struct tm* tm_info = localtime(&t);
    if (tm_info) KReturnInt(tm_info->tm_year + 1900);
    else KReturnInt(0);
}

static void std_time_getMonth() {
    int start = get_arg_start();
    time_t t = (time_t)(KGetArgInt(start) / 1000);
    struct tm* tm_info = localtime(&t);
    if (tm_info) KReturnInt(tm_info->tm_mon + 1);
    else KReturnInt(0);
}

static void std_time_getDay() {
    int start = get_arg_start();
    time_t t = (time_t)(KGetArgInt(start) / 1000);
    struct tm* tm_info = localtime(&t);
    if (tm_info) KReturnInt(tm_info->tm_mday);
    else KReturnInt(0);
}

static void std_time_getHour() {
    int start = get_arg_start();
    time_t t = (time_t)(KGetArgInt(start) / 1000);
    struct tm* tm_info = localtime(&t);
    if (tm_info) KReturnInt(tm_info->tm_hour);
    else KReturnInt(0);
}

static void std_time_getMinute() {
    int start = get_arg_start();
    time_t t = (time_t)(KGetArgInt(start) / 1000);
    struct tm* tm_info = localtime(&t);
    if (tm_info) KReturnInt(tm_info->tm_min);
    else KReturnInt(0);
}

static void std_time_getSecond() {
    int start = get_arg_start();
    time_t t = (time_t)(KGetArgInt(start) / 1000);
    struct tm* tm_info = localtime(&t);
    if (tm_info) KReturnInt(tm_info->tm_sec);
    else KReturnInt(0);
}

// -------------------------------------------------------------------------
// Net 庫
// -------------------------------------------------------------------------

static void lazy_init_winsock() {
#ifdef _WIN32
    static bool inited = false;
    if (!inited) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        inited = true;
    }
#endif
}

static void std_net_http_get() {
    int start = get_arg_start();
    KString url = KGetArgString(start);
    if (!url) { KReturnString(""); return; }
    
#ifdef _WIN32
    HINTERNET hInternet = InternetOpenA("Korelin/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) { KReturnString(""); return; }
    
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        KReturnString("");
        return;
    }
    
    char buffer[4096];
    DWORD bytesRead;
    char* result = (char*)malloc(1);
    result[0] = '\0';
    int totalLen = 0;
    
    while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        result = (char*)realloc(result, totalLen + bytesRead + 1);
        memcpy(result + totalLen, buffer, bytesRead);
        totalLen += bytesRead;
        result[totalLen] = '\0';
    }
    
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    
    KReturnString(result);
    free(result);
#else
    KReturnString("");
#endif
}

static void std_net_http_post() {
    KReturnString("POST not fully implemented");
}

static void std_net_listen() {
    lazy_init_winsock();
    int start = get_arg_start();
    KString proto = KGetArgString(start); 
    KString addr = KGetArgString(start + 1);
    
    if (!proto || !addr) { KReturnInt(0); return; }
    
    char ip_str[64] = "0.0.0.0";
    int port = 0;
    char* colon = strchr(addr, ':');
    if (colon) {
        int ip_len = colon - addr;
        if (ip_len > 0 && ip_len < 63) {
            strncpy(ip_str, addr, ip_len);
            ip_str[ip_len] = '\0';
        }
        port = atoi(colon + 1);
    } else {
        port = atoi(addr);
    }
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) { KReturnInt(0); return; }
    
    // Allow address reuse
    char opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ip_str);
    if (server.sin_addr.s_addr == INADDR_NONE) {
        // Fallback to INADDR_ANY if parsing failed or explicit "0.0.0.0"
        server.sin_addr.s_addr = INADDR_ANY;
    }
    server.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        closesocket(sock);
        KReturnInt(0);
        return;
    }
    
    if (listen(sock, 5) == SOCKET_ERROR) {
        closesocket(sock);
        KReturnInt(0);
        return;
    }
    
    KReturnInt((KInt)sock);
}

static void std_net_accept() {
    int start = get_arg_start();
    SOCKET server_sock = (SOCKET)KGetArgInt(start);
    
    struct sockaddr_in client;
    int c = sizeof(struct sockaddr_in);
    SOCKET client_sock = accept(server_sock, (struct sockaddr *)&client, &c);
    
    if (client_sock == INVALID_SOCKET) {
        KReturnInt(0);
    } else {
        KReturnInt((KInt)client_sock);
    }
}

static void std_net_dial() {
    lazy_init_winsock();
    int start = get_arg_start();
    KString proto = KGetArgString(start);
    KString addr = KGetArgString(start + 1);
    
    if (!proto || !addr) { KReturnInt(0); return; }
    
    char ip[64] = "127.0.0.1";
    int port = 80;
    
    char* colon = strchr(addr, ':');
    if (colon) {
        int ip_len = colon - addr;
        if (ip_len > 0 && ip_len < 63) {
            strncpy(ip, addr, ip_len);
            ip[ip_len] = '\0';
        }
        port = atoi(colon + 1);
    } else {
        strncpy(ip, addr, 63);
    }
    
    struct hostent *he;
    if ((he = gethostbyname(ip)) == NULL) {
         KReturnInt(0);
         return;
    }
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) { KReturnInt(0); return; }
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr = *((struct in_addr *)he->h_addr);
    memset(&(server.sin_zero), 0, 8);
    
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        closesocket(sock);
        KReturnInt(0);
    } else {
        KReturnInt((KInt)sock);
    }
}


static void std_net_tcpSend() {
    int start = get_arg_start();
    SOCKET sock = (SOCKET)KGetArgInt(start);
    KString data = KGetArgString(start + 1);
    if (data) send(sock, data, strlen(data), 0);
    KReturnVoid();
}

static void std_net_tcpRecv() {
    int start = get_arg_start();
    SOCKET sock = (SOCKET)KGetArgInt(start);
    int bufSize = (int)KGetArgInt(start + 1);
    if (bufSize <= 0) bufSize = 1024;
    
    char* buf = (char*)malloc(bufSize + 1);
    int received = recv(sock, buf, bufSize, 0);
    if (received > 0) {
        buf[received] = '\0';
        KReturnString(buf);
    } else {
        KReturnString("");
    }
    free(buf);
}

static void std_net_tcpClose() {
    int start = get_arg_start();
    SOCKET sock = (SOCKET)KGetArgInt(start);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    KReturnVoid();
}

static void std_net_setNonBlocking() {
    int start = get_arg_start();
    SOCKET sock = (SOCKET)KGetArgInt(start);
    bool enable = KGetArgBool(start + 1);
    
#ifdef _WIN32
    u_long mode = enable ? 1 : 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (enable) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    fcntl(sock, F_SETFL, flags);
#endif
    KReturnVoid();
}

// Forward declaration or copy implementation of write_array if it's static
// Since write_array is not standard, let's implement a local helper
static void kstd_write_array(KVM* vm, KObjArray* arr, KValue val) {
    if (arr->length < arr->capacity) {
        arr->elements[arr->length++] = val;
    } else {
        // Simple grow
        int new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
        
        KValue* new_elements = (KValue*)realloc(arr->elements, sizeof(KValue) * new_cap);
        if (new_elements == NULL) {
            printf("FATAL: Out of memory in array push\n");
            return;
        }
        arr->elements = new_elements;
        arr->capacity = new_cap;
        arr->elements[arr->length++] = val;
    }
}

static void std_net_select() {
    int start = get_arg_start();
    KObjArray* read_arr = get_arg_array(start);
    KObjArray* write_arr = get_arg_array(start + 1);
    KInt timeout_ms = KGetArgInt(start + 2);
    
    // printf("DEBUG: std_net_select enter. read_len=%d write_len=%d timeout=%lld\n", 
    //        read_arr ? read_arr->length : -1, 
    //        write_arr ? write_arr->length : -1, 
    //        timeout_ms);
    // fflush(stdout);

    // Allocate fd_set on heap to avoid stack issues and size limits
    fd_set* readfds = (fd_set*)malloc(sizeof(fd_set));
    fd_set* writefds = (fd_set*)malloc(sizeof(fd_set));
    
    if (!readfds || !writefds) {
        if (readfds) free(readfds);
        if (writefds) free(writefds);
        printf("Out of memory for fd_set\n");
        KReturnInt(0);
        return;
    }

    FD_ZERO(readfds);
    FD_ZERO(writefds);
    
    SOCKET max_fd = 0;
    int fd_count = 0;
    
    if (read_arr) {
        for (int i = 0; i < read_arr->length; i++) {
            if (fd_count >= FD_SETSIZE) break;
            if (read_arr->elements[i].type == VAL_INT) {
                SOCKET s = (SOCKET)read_arr->elements[i].as.integer;
                if (s != INVALID_SOCKET && s > 0) {
                    FD_SET(s, readfds);
                    // if (s > max_fd) max_fd = s; // Ignored on Windows
                    fd_count++;
                }
            }
        }
    }
    
    if (write_arr) {
        for (int i = 0; i < write_arr->length; i++) {
            if (fd_count >= FD_SETSIZE) break;
            if (write_arr->elements[i].type == VAL_INT) {
                SOCKET s = (SOCKET)write_arr->elements[i].as.integer;
                if (s != INVALID_SOCKET && s > 0) {
                    FD_SET(s, writefds);
                    // if (s > max_fd) max_fd = s;
                    fd_count++;
                }
            }
        }
    }
    
    if (fd_count == 0) {
        // Nothing to select, just sleep?
        free(readfds);
        free(writefds);
        Sleep(timeout_ms);
        KReturnInt(0);
        return;
    }
    
    // printf("DEBUG: Calling select with %d fds\n", fd_count); fflush(stdout);
    
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    int res = select(0, readfds, writefds, NULL, &tv); // nfds ignored on Windows
    
    if (res == SOCKET_ERROR) {
        printf("select error: %d\n", WSAGetLastError());
        free(readfds);
        free(writefds);
        KReturnInt(0);
        return;
    }
    
    if (res > 0) {
        KVM* vm = get_vm();
        
        KObjArray* res_reads = alloc_array(vm, 0);
        KValue v_read; v_read.type = VAL_OBJ; v_read.as.obj = (KObj*)res_reads;
        kvm_push(vm, v_read); // Protect from GC
        
        KObjArray* res_writes = alloc_array(vm, 0);
        KValue v_write; v_write.type = VAL_OBJ; v_write.as.obj = (KObj*)res_writes;
        kvm_push(vm, v_write); // Protect from GC
        
        if (read_arr) {
            for (int i = 0; i < read_arr->length; i++) {
                if (read_arr->elements[i].type == VAL_INT) {
                    SOCKET s = (SOCKET)read_arr->elements[i].as.integer;
                    if (s != INVALID_SOCKET && s > 0 && FD_ISSET(s, readfds)) {
                        KValue v; v.type = VAL_INT; v.as.integer = (KInt)s;
                        kstd_write_array(vm, res_reads, v);
                    }
                }
            }
        }
        
        if (write_arr) {
            for (int i = 0; i < write_arr->length; i++) {
                if (write_arr->elements[i].type == VAL_INT) {
                    SOCKET s = (SOCKET)write_arr->elements[i].as.integer;
                    if (s != INVALID_SOCKET && s > 0 && FD_ISSET(s, writefds)) {
                        KValue v; v.type = VAL_INT; v.as.integer = (KInt)s;
                        kstd_write_array(vm, res_writes, v);
                    }
                }
            }
        }
        
        KObjArray* final_res = alloc_array(vm, 2);
        
        // Pop protected values
        kvm_pop(vm); // res_writes
        kvm_pop(vm); // res_reads
        
        final_res->elements[0] = v_read;
        final_res->elements[1] = v_write;
        
        KValue v_final; v_final.type = VAL_OBJ; v_final.as.obj = (KObj*)final_res;
        
        kvm_push(vm, v_final);
    } else {
        KReturnInt(0);
    }
    
    free(readfds);
    free(writefds);
}

static void std_net_getIP() {
    lazy_init_winsock();
    int start = get_arg_start();
    KString domain = KGetArgString(start);
    if (!domain) { KReturnString(""); return; }
    
    struct hostent* host = gethostbyname(domain);
    if (host) {
        struct in_addr** addr_list = (struct in_addr**)host->h_addr_list;
        if (addr_list[0]) {
            KReturnString(inet_ntoa(*addr_list[0]));
            return;
        }
    }
    KReturnString("");
}

// -------------------------------------------------------------------------
// String 庫
// -------------------------------------------------------------------------

static void std_string_len() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KReturnInt(s ? strlen(s) : 0);
}

static void std_string_sub() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KInt start_idx = KGetArgInt(start + 1);
    KInt end_idx = KGetArgInt(start + 2);
    if (!s) { KReturnString(""); return; }
    int len = strlen(s);
    if (start_idx < 0) start_idx = 0;
    if (end_idx > len) end_idx = len;
    if (start_idx >= end_idx) { KReturnString(""); return; }
    
    int sublen = end_idx - start_idx;
    char* res = (char*)malloc(sublen + 1);
    strncpy(res, s + start_idx, sublen);
    res[sublen] = '\0';
    KReturnString(res);
    free(res);
}

static void std_string_replace() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KString old = KGetArgString(start + 1);
    KString newS = KGetArgString(start + 2);
    if (!s || !old || !newS) { KReturnString(s ? s : ""); return; }
    
    // Implement replace all
    int count = 0;
    const char* p = s;
    int oldLen = strlen(old);
    int newLen = strlen(newS);
    
    if (oldLen == 0) { KReturnString(s); return; }
    
    while ((p = strstr(p, old))) {
        count++;
        p += oldLen;
    }
    
    int newSize = strlen(s) + count * (newLen - oldLen);
    char* res = (char*)malloc(newSize + 1);
    char* dst = res;
    p = s;
    const char* next;
    
    while ((next = strstr(p, old))) {
        int copyLen = next - p;
        memcpy(dst, p, copyLen);
        dst += copyLen;
        memcpy(dst, newS, newLen);
        dst += newLen;
        p = next + oldLen;
    }
    strcpy(dst, p);
    
    KReturnString(res);
    free(res);
}

static void std_string_int() {
    int start = get_arg_start();
    KValue v = get_vm()->native_args[start];
    char buf[64];
    if (v.type == VAL_INT) sprintf(buf, "%lld", (long long)v.as.integer);
    else if (v.type == VAL_BOOL) sprintf(buf, "%d", v.as.boolean);
    // else if (v.type == VAL_FLOAT) sprintf(buf, "%g", v.as.number); // FLOAT not supported in this version?
    else if (v.type == VAL_STRING) { KReturnString(v.as.str); return; }
    else sprintf(buf, "null");
    KReturnString(buf);
}

static void std_string_toUpper() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    if (!s) { KReturnString(""); return; }
    char* res = strdup(s);
    for(int i=0; res[i]; i++) res[i] = toupper(res[i]);
    KReturnString(res);
    free(res);
}

static void std_string_toLower() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    if (!s) { KReturnString(""); return; }
    char* res = strdup(s);
    for(int i=0; res[i]; i++) res[i] = tolower(res[i]);
    KReturnString(res);
    free(res);
}

static void std_string_trim() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    if (!s) { KReturnString(""); return; }
    while(isspace(*s)) s++;
    char* res = strdup(s);
    char* end = res + strlen(res) - 1;
    while(end > res && isspace(*end)) end--;
    *(end+1) = '\0';
    KReturnString(res);
    free(res);
}

static void std_string_split() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KString sep = KGetArgString(start + 1);
    if (!s || !sep) { KReturnInt(0); return; }

    // Count
    int count = 0;
    const char* p = s;
    int seplen = strlen(sep);
    if (seplen == 0) {
        count = strlen(s);
    } else {
        while ((p = strstr(p, sep)) != NULL) {
            count++;
            p += seplen;
        }
        count++;
    }
    
    KObjArray* arr = alloc_array(get_vm(), count);
    KValue v_arr; v_arr.type = VAL_OBJ; v_arr.as.obj = (KObj*)arr;
    push_value(v_arr); // Protect arr
    
    // Fill
    p = s;
    if (seplen == 0) {
        for (int i = 0; i < count; i++) {
            char buf[2] = { s[i], '\0' };
            KObjString* ks = alloc_string(get_vm(), buf, 1);
            KValue v; v.type = VAL_OBJ; v.as.obj = ks;
            arr->elements[i] = v;
        }
    } else {
        int idx = 0;
        const char* start_ptr = s;
        const char* end;
        while ((end = strstr(start_ptr, sep)) != NULL) {
            int len = end - start_ptr;
            KObjString* ks = alloc_string(get_vm(), start_ptr, len);
            KValue v; v.type = VAL_OBJ; v.as.obj = ks;
            arr->elements[idx++] = v;
            start_ptr = end + seplen;
        }
        int len = strlen(start_ptr);
        KObjString* ks = alloc_string(get_vm(), start_ptr, len);
        KValue v; v.type = VAL_OBJ; v.as.obj = ks;
        arr->elements[idx] = v;
    }
    
    // Already on stack top
    // KValue res; res.type = VAL_OBJ; res.as.obj = arr;
    // push_value(res);
}

static void std_string_join() {
    int start = get_arg_start();
    KObjArray* arr = get_arg_array(start);
    KString sep = KGetArgString(start + 1);
    if (!arr || !sep) { KReturnString(""); return; }
    
    int len = 0;
    int seplen = strlen(sep);
    for(int i=0; i<arr->length; i++) {
        char* s = to_string(arr->elements[i]);
        len += strlen(s);
        if (i < arr->length - 1) len += seplen;
        free(s);
    }
    
    char* res = (char*)malloc(len + 1);
    res[0] = '\0';
    for(int i=0; i<arr->length; i++) {
        char* s = to_string(arr->elements[i]);
        strcat(res, s);
        if (i < arr->length - 1) strcat(res, sep);
        free(s);
    }
    KReturnString(res);
    free(res);
}

static void std_string_indexOf() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KString sub = KGetArgString(start + 1);
    if (!s || !sub) { KReturnInt(-1); return; }
    char* p = strstr(s, sub);
    KReturnInt(p ? (int)(p - s) : -1);
}

static void std_string_lastIndexOf() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KString sub = KGetArgString(start + 1);
    if (!s || !sub) { KReturnInt(-1); return; }
    
    char* p = (char*)s;
    char* last = NULL;
    while ((p = strstr(p, sub)) != NULL) {
        last = p;
        p++;
    }
    KReturnInt(last ? (int)(last - s) : -1);
}

// -------------------------------------------------------------------------
// Array Class
// -------------------------------------------------------------------------

static void std_array_push() {
    KObjArray* arr = get_arg_array(0);
    KValue val = get_vm()->native_args[1];
    if (arr) {
        kstd_write_array(get_vm(), arr, val);
    }
    KReturnVoid();
}

static void std_array_pop() {
    KObjArray* arr = get_arg_array(0);
    if (arr && arr->length > 0) {
        arr->length--;
        push_value(arr->elements[arr->length]);
        // Ideally shrink capacity? No need for now.
    } else {
        KValue nullVal; nullVal.type = VAL_NULL;
        push_value(nullVal);
    }
}

static void std_array_len() {
    KObjArray* arr = get_arg_array(0);
    if (arr) {
        KReturnInt(arr->length);
    } else {
        KReturnInt(0);
    }
}

static void std_array_removeAt() {
    KObjArray* arr = get_arg_array(0);
    KInt idx = KGetArgInt(1);
    
    if (arr && idx >= 0 && idx < arr->length) {
        // Shift elements
        for(int i = idx; i < arr->length - 1; i++) {
            arr->elements[i] = arr->elements[i+1];
        }
        arr->length--;
    }
    KReturnVoid();
}

static void std_array_clear() {
    KObjArray* arr = get_arg_array(0);
    if (arr) {
        arr->length = 0;
    }
    KReturnVoid();
}

// -------------------------------------------------------------------------
// Math 庫
// -------------------------------------------------------------------------

static void std_math_abs() {
    int start = get_arg_start();
    KValue v = get_vm()->native_args[start];
    if (v.type == VAL_FLOAT) KReturnFloat(fabsf(v.as.single_prec));
    else if (v.type == VAL_DOUBLE) KReturnFloat(fabs(v.as.double_prec));
    else KReturnInt(llabs(v.as.integer));
}

static void std_math_max() {
    int start = get_arg_start();
    KValue a = get_vm()->native_args[start];
    KValue b = get_vm()->native_args[start + 1];
    double da = (a.type == VAL_INT) ? (double)a.as.integer : (a.type == VAL_FLOAT ? a.as.single_prec : a.as.double_prec);
    double db = (b.type == VAL_INT) ? (double)b.as.integer : (b.type == VAL_FLOAT ? b.as.single_prec : b.as.double_prec);
    KReturnInt((KInt)(da > db ? da : db));
}

static void std_math_min() {
    int start = get_arg_start();
    KValue a = get_vm()->native_args[start];
    KValue b = get_vm()->native_args[start + 1];
    double da = (a.type == VAL_INT) ? (double)a.as.integer : (a.type == VAL_FLOAT ? a.as.single_prec : a.as.double_prec);
    double db = (b.type == VAL_INT) ? (double)b.as.integer : (b.type == VAL_FLOAT ? b.as.single_prec : b.as.double_prec);
    KReturnInt((KInt)(da < db ? da : db));
}

static void std_math_pow() {
    int start = get_arg_start();
    KInt base = KGetArgInt(start);
    KInt exp = KGetArgInt(start + 1);
    KReturnInt((KInt)pow(base, exp));
}

static void std_math_sqrt() {
    int start = get_arg_start();
    KInt val = KGetArgInt(start);
    KReturnFloat(sqrt(val));
}

static void std_math_round() {
    int start = get_arg_start();
    KFloat f = KGetArgFloat(start);
    KReturnInt((KInt)round(f));
}

static void std_math_floor() {
    int start = get_arg_start();
    KFloat f = KGetArgFloat(start);
    KReturnInt((KInt)floor(f));
}

static void std_math_ceil() {
    int start = get_arg_start();
    KFloat f = KGetArgFloat(start);
    KReturnInt((KInt)ceil(f));
}

static void std_math_random() {
    int start = get_arg_start();
    KInt min = KGetArgInt(start);
    KInt max = KGetArgInt(start + 1);
    int r = rand() % (max - min + 1) + min;
    KReturnInt(r);
}

static void std_math_pi() {
    KReturnFloat(3.1415926535f);
}

// -------------------------------------------------------------------------
// Algorithm 庫
// -------------------------------------------------------------------------

static int compare_values(const void* a, const void* b) {
    KValue va = *(KValue*)a;
    KValue vb = *(KValue*)b;
    if (va.type == VAL_INT && vb.type == VAL_INT) return (int)(va.as.integer - vb.as.integer);
    return 0;
}

static void std_algo_sort() {
    int start = get_arg_start();
    KObjArray* arr = get_arg_array(start);
    if (arr) {
        qsort(arr->elements, arr->length, sizeof(KValue), compare_values);
    }
    KReturnVoid();
}

static void std_algo_reverse() {
    int start = get_arg_start();
    KObjArray* arr = get_arg_array(start);
    if (arr) {
        for(int i=0; i<arr->length/2; i++) {
            KValue tmp = arr->elements[i];
            arr->elements[i] = arr->elements[arr->length - 1 - i];
            arr->elements[arr->length - 1 - i] = tmp;
        }
    }
    KReturnVoid();
}

static void std_algo_find() {
    int start = get_arg_start();
    KObjArray* arr = get_arg_array(start);
    KValue val = get_vm()->native_args[start + 1];
    if (arr) {
        for(int i=0; i<arr->length; i++) {
            if (arr->elements[i].type == val.type) {
                if (val.type == VAL_INT && arr->elements[i].as.integer == val.as.integer) {
                    KReturnInt(i); return;
                }
            }
        }
    }
    KReturnInt(-1);
}

static void std_algo_sum() {
    int start = get_arg_start();
    KObjArray* arr = get_arg_array(start);
    long long sum = 0;
    if (arr) {
        for(int i=0; i<arr->length; i++) {
            if (arr->elements[i].type == VAL_INT) sum += arr->elements[i].as.integer;
        }
    }
    KReturnInt(sum);
}

static void std_algo_average() {
    int start = get_arg_start();
    KObjArray* arr = get_arg_array(start);
    long long sum = 0;
    if (arr && arr->length > 0) {
        for(int i=0; i<arr->length; i++) {
            if (arr->elements[i].type == VAL_INT) sum += arr->elements[i].as.integer;
        }
        KReturnFloat((float)sum / arr->length);
    } else {
        KReturnFloat(0);
    }
}

// -------------------------------------------------------------------------
// JSON 庫
// -------------------------------------------------------------------------

// JSON Parser State
typedef struct {
    const char* source;
    int current;
    KVM* vm;
} JsonParser;

static void json_skip_whitespace(JsonParser* parser) {
    while (parser->source[parser->current] != '\0' && isspace(parser->source[parser->current])) {
        parser->current++;
    }
}

static char json_peek(JsonParser* parser) {
    return parser->source[parser->current];
}

static char json_advance(JsonParser* parser) {
    if (parser->source[parser->current] == '\0') return '\0';
    return parser->source[parser->current++];
}

static bool json_match(JsonParser* parser, char c) {
    if (json_peek(parser) == c) {
        parser->current++;
        return true;
    }
    return false;
}

// Forward declarations
static KValue json_parse_value(JsonParser* parser);

static KValue json_parse_object(JsonParser* parser) {
    json_advance(parser); // consume '{'
    json_skip_whitespace(parser);
    
    // Create Map instance
    KValue mapClass;
    KObjClass* cls = NULL;
    if (table_get(&parser->vm->globals, "Map", &mapClass) && mapClass.type == VAL_OBJ && ((KObj*)mapClass.as.obj)->header.type == OBJ_CLASS) {
        cls = (KObjClass*)mapClass.as.obj;
    }
    
    KObjInstance* instance = alloc_instance(parser->vm, cls);
    
    if (json_peek(parser) == '}') {
        json_advance(parser);
        KValue v; v.type = VAL_OBJ; v.as.obj = (KObj*)instance;
        return v;
    }
    
    while (true) {
        json_skip_whitespace(parser);
        if (json_peek(parser) != '"') break; // Error
        
        // Parse key
        KValue keyVal = json_parse_value(parser);
        char* key = NULL;
        if (keyVal.type == VAL_OBJ && ((KObj*)keyVal.as.obj)->header.type == OBJ_STRING) {
             key = ((KObjString*)keyVal.as.obj)->chars;
        } else if (keyVal.type == VAL_STRING) {
             key = keyVal.as.str; 
        } else {
            break; 
        }
        
        json_skip_whitespace(parser);
        if (!json_match(parser, ':')) break; // Error
        
        KValue val = json_parse_value(parser);
        table_set(&instance->fields, key, val);
        
        json_skip_whitespace(parser);
        if (!json_match(parser, ',')) break;
    }
    
    if (!json_match(parser, '}')) {
        // Error
    }
    
    KValue v; v.type = VAL_OBJ; v.as.obj = (KObj*)instance;
    return v;
}

static KValue json_parse_array(JsonParser* parser) {
    json_advance(parser); // consume '['
    json_skip_whitespace(parser);
    
    int capacity = 8;
    int count = 0;
    KValue* temp = (KValue*)malloc(sizeof(KValue) * capacity);
    
    if (json_peek(parser) != ']') {
        while (true) {
            KValue val = json_parse_value(parser);
            
            if (count >= capacity) {
                capacity *= 2;
                temp = (KValue*)realloc(temp, sizeof(KValue) * capacity);
            }
            temp[count++] = val;
            
            json_skip_whitespace(parser);
            if (!json_match(parser, ',')) break;
        }
    }
    
    if (!json_match(parser, ']')) {
        // Error handling?
    }
    
    KObjArray* arr = alloc_array(parser->vm, count);
    for(int i=0; i<count; i++) {
        arr->elements[i] = temp[i];
    }
    free(temp);
    
    KValue v; v.type = VAL_OBJ; v.as.obj = (KObj*)arr;
    return v;
}

static KValue json_parse_string(JsonParser* parser) {
    json_advance(parser); // consume '"'
    
    int start = parser->current;
    while (json_peek(parser) != '"' && json_peek(parser) != '\0') {
        if (json_peek(parser) == '\\') {
            parser->current++; // skip escape
        }
        parser->current++;
    }
    
    int len = parser->current - start;
    
    char* s = (char*)malloc(len + 1);
    memcpy(s, parser->source + start, len);
    s[len] = '\0';
    
    json_advance(parser); // consume closing '"'
    
    KObjString* ks = alloc_string(parser->vm, s, len);
    free(s);
    KValue v; v.type = VAL_OBJ; v.as.obj = (KObj*)ks;
    return v;
}

static KValue json_parse_number(JsonParser* parser) {
    int start = parser->current;
    if (json_peek(parser) == '-') parser->current++;
    while (isdigit((unsigned char)json_peek(parser))) parser->current++;
    
    if (json_peek(parser) == '.') {
        parser->current++;
        while (isdigit((unsigned char)json_peek(parser))) parser->current++;
        // Float
        int len = parser->current - start;
        char* buf = (char*)malloc(len + 1);
        memcpy(buf, parser->source + start, len);
        buf[len] = '\0';
        double d = atof(buf);
        free(buf);
        KValue v; v.type = VAL_DOUBLE; v.as.double_prec = d;
        return v;
    } else {
        // Int
        int len = parser->current - start;
        char* buf = (char*)malloc(len + 1);
        memcpy(buf, parser->source + start, len);
        buf[len] = '\0';
        long long i = atoll(buf);
        free(buf);
        KValue v; v.type = VAL_INT; v.as.integer = i;
        return v;
    }
}

static KValue json_parse_value(JsonParser* parser) {
    json_skip_whitespace(parser);
    char c = json_peek(parser);
    
    if (c == '{') return json_parse_object(parser);
    if (c == '[') return json_parse_array(parser);
    if (c == '"') return json_parse_string(parser);
    if (c == '-' || isdigit((unsigned char)c)) return json_parse_number(parser);
    if (strncmp(parser->source + parser->current, "true", 4) == 0) {
        parser->current += 4;
        KValue v; v.type = VAL_BOOL; v.as.boolean = true;
        return v;
    }
    if (strncmp(parser->source + parser->current, "false", 5) == 0) {
        parser->current += 5;
        KValue v; v.type = VAL_BOOL; v.as.boolean = false;
        return v;
    }
    if (strncmp(parser->source + parser->current, "null", 4) == 0) {
        parser->current += 4;
        KValue v; v.type = VAL_NULL;
        return v;
    }
    
    // Error
    parser->current++; 
    KValue v; v.type = VAL_NULL;
    return v;
}

static void std_json_parse() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    if (!s) { KReturnInt(0); return; }
    
    JsonParser parser;
    parser.source = s;
    parser.current = 0;
    parser.vm = get_vm();
    
    KValue v = json_parse_value(&parser);
    push_value(v);
}

static void std_json_stringify() {
    int start = get_arg_start();
    KString s = KGetArgString(start);
    KReturnString(s ? s : "null");
}

static void std_json_get() {
    int start = get_arg_start();
    KObjInstance* obj = get_arg_instance(start);
    KString key = KGetArgString(start + 1);
    if (obj && key) {
        KValue val;
        if (table_get(&obj->fields, key, &val)) {
            push_value(val);
            return;
        }
    }
    KReturnVoid();
}

static void std_json_set() {
    int start = get_arg_start();
    KObjInstance* obj = get_arg_instance(start);
    KString key = KGetArgString(start + 1);
    KValue val = get_vm()->native_args[start + 2];
    if (obj && key) {
        table_set(&obj->fields, key, val);
    }
    KReturnVoid();
}

// -------------------------------------------------------------------------
// Thread 庫
// -------------------------------------------------------------------------

typedef struct {
    KObjFunction* func;
    KTable* parent_globals;
    KValue arg;
    bool has_arg;
} ThreadArgs;

#ifdef _WIN32
unsigned __stdcall thread_proc(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    
    // Allocate VM on heap to save stack space
    KVM* vm = (KVM*)malloc(sizeof(KVM));
    if (!vm) {
        free(args);
        return 1;
    }
    kvm_init(vm);
    
    // Bind new VM to this thread
    KBindVM(vm);
    
    // Register standard libraries for this thread's VM
    kstd_register();
    
    // Copy globals from parent VM (shallow copy)
    if (args->parent_globals && args->parent_globals->entries) {
        for (int i = 0; i < args->parent_globals->capacity; i++) {
            KTableEntry* entry = &args->parent_globals->entries[i];
            if (entry->key != NULL) {
                table_set(&vm->globals, entry->key, entry->value);
            }
        }
    }
    
    // Copy bytecode chunk but ensure we don't double free it
    vm->chunk = args->func->chunk;
    
    // Create a stack frame for the function
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->chunk = args->func->chunk;
    frame->ip = args->func->chunk->code + args->func->entry_point;
    frame->base_registers = vm->registers;
    
    // Pass Argument: Push to local register 0
    if (args->has_arg) {
        // In KVM, registers are just a window on the stack/register file
        // For a new function call at base level, R0 is registers[0]
        vm->registers[0] = args->arg;
    }
    
    vm->ip = frame->ip;
    
    kvm_run(vm);
    
    // Cleanup
    kvm_free(vm);
    free(vm);
    free(args);
    return 0;
}
#endif

static void std_thread_create() {
    int start = get_arg_start();
    KVM* vm = get_vm();
    KValue v = vm->native_args[start];
    
    KValue arg_val;
    bool has_arg = false;
    
    // Check for optional argument
    if (start + 1 < MAX_NATIVE_ARGS && vm->native_args[start + 1].type != VAL_NULL) {
         // This is tricky because get_arg_start() logic isn't fully visible here, 
         // but native_args is populated by vm before call.
         // We assume native call protocol passes args sequentially.
         // But wait, get_arg_start returns index in native_args.
         // We need to check if there is another arg.
         // The current VM implementation of native calls might not pass argument count.
         // We'll peek at the next slot. If it's valid, we take it.
         // Better: check if we are provided a second arg.
         // Korelin VM doesn't explicitly pass argc to native functions in current impl?
         // Let's assume if the user passed it, it's in native_args[start+1].
         // However, unpassed args might be garbage or previous values.
         // Safety: We only support 1 optional arg if provided. 
         // But we can't distinguish "not provided" from "provided null/0" easily without argc.
         // Let's assume the user ALWAYS provides it if they want to use it.
         // And since we are modifying the stdlib, we can say thread.create(func, [arg])
         // But how do we know if arg was provided?
         // We will try to read it. If the call was thread.create(func), the next arg might be old data.
         // We really need argc in native calls. 
         // Looking at kapi.c/kvm.c, native calls don't seem to push argc.
         // WORKAROUND: For now, we'll blindly take the 2nd argument if we want to support it.
         // But to be safe, let's just assume we always pass it.
         // If user calls thread.create(func), they get a garbage/null arg?
         // Let's modify logic:
         // If we want to pass arg, we must use thread.create(func, arg).
         // If we use thread.create(func), the 2nd arg is undefined.
         // So we will just read it.
         arg_val = vm->native_args[start + 1];
         has_arg = true; 
    }

    if (v.type == VAL_OBJ && ((KObj*)v.as.obj)->header.type == OBJ_FUNCTION) {
        KObjFunction* func = (KObjFunction*)v.as.obj;
        ThreadArgs* args = (ThreadArgs*)malloc(sizeof(ThreadArgs));
        args->func = func;
        args->parent_globals = &vm->globals;
        args->arg = arg_val;
        args->has_arg = has_arg;
        
#ifdef _WIN32
        unsigned tid;
        HANDLE h = (HANDLE)_beginthreadex(NULL, 0, thread_proc, args, 0, &tid);
        if (h) {
            // We must close the handle if we don't return it to user to join?
            // Actually, we return it. The user script receives it.
            // If user script doesn't join, it leaks handle.
            // But we can't auto-close it because then join would fail.
            // But we should probably use _beginthread which auto-closes if we don't need join?
            // But we support join.
            // So script MUST call join or we need a detach?
            // Let's implement thread.detach(h) which closes handle.
            KReturnInt((KInt)(uintptr_t)h);
        } else {
            KReturnInt(0);
        }
#else
        KReturnInt(0);
#endif
    } else {
        KReturnInt(-1);
    }
}

static void std_thread_join() {
    int start = get_arg_start();
    KInt handle = KGetArgInt(start);
#ifdef _WIN32
    if (handle) {
        HANDLE h = (HANDLE)(uintptr_t)handle;
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    }
#endif
    KReturnVoid();
}

static void std_thread_id() {
#ifdef _WIN32
    KReturnInt((KInt)GetCurrentThreadId());
#else
    KReturnInt(0);
#endif
}

static void std_thread_detach() {
    int start = get_arg_start();
    KInt handle = KGetArgInt(start);
#ifdef _WIN32
    if (handle) {
        CloseHandle((HANDLE)(uintptr_t)handle);
    }
#endif
    KReturnVoid();
}

static void std_thread_kill() {
    int start = get_arg_start();
    KInt handle = KGetArgInt(start);
#ifdef _WIN32
    if (handle) {
        TerminateThread((HANDLE)(uintptr_t)handle, 0);
        CloseHandle((HANDLE)(uintptr_t)handle);
    }
#endif
    KReturnVoid();
}

static CRITICAL_SECTION g_lock;
static bool g_lock_inited = false;

static void std_thread_mutex_create() {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(cs);
    KReturnInt((KInt)(uintptr_t)cs);
}

static void std_thread_mutex_lock() {
    int start = get_arg_start();
    KInt mutex = KGetArgInt(start);
    if (mutex) {
        EnterCriticalSection((CRITICAL_SECTION*)(uintptr_t)mutex);
    }
    KReturnVoid();
}

static void std_thread_mutex_unlock() {
    int start = get_arg_start();
    KInt mutex = KGetArgInt(start);
    if (mutex) {
        LeaveCriticalSection((CRITICAL_SECTION*)(uintptr_t)mutex);
    }
    KReturnVoid();
}

static void std_thread_mutex_destroy() {
    int start = get_arg_start();
    KInt mutex = KGetArgInt(start);
    if (mutex) {
        CRITICAL_SECTION* cs = (CRITICAL_SECTION*)(uintptr_t)mutex;
        DeleteCriticalSection(cs);
        free(cs);
    }
    KReturnVoid();
}

static void std_thread_lock() {
    if (!g_lock_inited) { InitializeCriticalSection(&g_lock); g_lock_inited = true; }
    EnterCriticalSection(&g_lock);
    KReturnVoid();
}

static void std_thread_unlock() {
    if (g_lock_inited) LeaveCriticalSection(&g_lock);
    KReturnVoid();
}

static void std_thread_sleep() {
    std_time_sleep();
}

// -------------------------------------------------------------------------
// Dynlib 庫
// -------------------------------------------------------------------------

static void std_dynlib_load() {
    int start = get_arg_start();
    KString path = KGetArgString(start);
    if (!path) { KReturnInt(0); return; }
#ifdef _WIN32
    HMODULE h = LoadLibraryA(path);
    KReturnInt((KInt)(uintptr_t)h);
#else
    void* h = dlopen(path, RTLD_LAZY);
    KReturnInt((KInt)(uintptr_t)h);
#endif
}

static void std_dynlib_get() {
    int start = get_arg_start();
    KInt h = KGetArgInt(start);
    KString name = KGetArgString(start + 1);
    if (!h || !name) { KReturnInt(0); return; }
#ifdef _WIN32
    FARPROC p = GetProcAddress((HMODULE)(uintptr_t)h, name);
    KReturnInt((KInt)(uintptr_t)p);
#else
    void* p = dlsym((void*)(uintptr_t)h, name);
    KReturnInt((KInt)(uintptr_t)p);
#endif
}

static void std_dynlib_call() {
    int start = get_arg_start();
    KInt func = KGetArgInt(start);
    KInt arg1 = KGetArgInt(start + 1);
    if (func) {
        typedef int (*FuncType)(int);
        FuncType f = (FuncType)(uintptr_t)func;
        int res = f((int)arg1);
        KReturnInt(res);
    } else {
        KReturnInt(0);
    }
}

static void std_dynlib_unload() {
    int start = get_arg_start();
    KInt h = KGetArgInt(start);
#ifdef _WIN32
    FreeLibrary((HMODULE)(uintptr_t)h);
#else
    dlclose((void*)(uintptr_t)h);
#endif
    KReturnVoid();
}

static void std_dynlib_getLastError() {
    KReturnString("Unknown Error");
}

// -------------------------------------------------------------------------
// Map Class
// -------------------------------------------------------------------------

static void std_map_init() {
    // Nothing to do, instance created with empty fields
    KReturnVoid();
}

static void std_map_set() {
    KObjInstance* self = get_arg_instance(0);
    KString key = KGetArgString(1);
    KValue val = get_vm()->native_args[2];

    if (self && key) {
        table_set(&self->fields, key, val);
    }
    KReturnVoid();
}

static void std_map_get() {
    KObjInstance* self = get_arg_instance(0);
    KString key = KGetArgString(1);
    
    if (self && key) {
        KValue val;
        if (table_get(&self->fields, key, &val)) {
            push_value(val);
            return;
        }
    }
    KValue nullVal; nullVal.type = VAL_NULL;
    push_value(nullVal);
}

static void std_map_remove() {
    KObjInstance* self = get_arg_instance(0);
    KString key = KGetArgString(1);
    
    if (self && key) {
        KValue nullVal; nullVal.type = VAL_NULL;
        // Soft delete: set to null
        table_set(&self->fields, key, nullVal);
    }
    KReturnVoid();
}

static void std_map_contains() {
    KObjInstance* self = get_arg_instance(0);
    KString key = KGetArgString(1);
    
    if (self && key) {
        KValue val;
        if (table_get(&self->fields, key, &val)) {
            if (val.type != VAL_NULL) {
                KReturnBool(true);
                return;
            }
        }
    }
    KReturnBool(false);
}

static void std_map_size() {
    KObjInstance* self = get_arg_instance(0);
    int count = 0;
    if (self) {
        for (int i = 0; i < self->fields.capacity; i++) {
            KTableEntry* entry = &self->fields.entries[i];
            if (entry->key != NULL && entry->value.type != VAL_NULL) {
                count++;
            }
        }
    }
    KReturnInt(count);
}

static void std_map_keys() {
    KObjInstance* self = get_arg_instance(0);
    if (!self) { KReturnVoid(); return; }
    
    // Count first
    int count = 0;
    for (int i = 0; i < self->fields.capacity; i++) {
        KTableEntry* entry = &self->fields.entries[i];
        if (entry->key != NULL && entry->value.type != VAL_NULL) {
            count++;
        }
    }
    
    KObjArray* arr = alloc_array(get_vm(), count);
    int idx = 0;
    for (int i = 0; i < self->fields.capacity; i++) {
        KTableEntry* entry = &self->fields.entries[i];
        if (entry->key != NULL && entry->value.type != VAL_NULL) {
            int len = strlen(entry->key);
            KObjString* ks = alloc_string(get_vm(), entry->key, len);
            KValue v; v.type = VAL_OBJ; v.as.obj = ks;
            arr->elements[idx++] = v;
        }
    }
    
    KValue res; res.type = VAL_OBJ; res.as.obj = arr;
    push_value(res);
}

static void std_map_values() {
    KObjInstance* self = get_arg_instance(0);
    if (!self) { KReturnVoid(); return; }
    
    // Count first
    int count = 0;
    for (int i = 0; i < self->fields.capacity; i++) {
        KTableEntry* entry = &self->fields.entries[i];
        if (entry->key != NULL && entry->value.type != VAL_NULL) {
            count++;
        }
    }
    
    KObjArray* arr = alloc_array(get_vm(), count);
    int idx = 0;
    for (int i = 0; i < self->fields.capacity; i++) {
        KTableEntry* entry = &self->fields.entries[i];
        if (entry->key != NULL && entry->value.type != VAL_NULL) {
            arr->elements[idx++] = entry->value;
        }
    }
    
    KValue res; res.type = VAL_OBJ; res.as.obj = arr;
    push_value(res);
}

// -------------------------------------------------------------------------
// Register
// -------------------------------------------------------------------------

// -------------------------------------------------------------------------
// Global Conversion Functions
// -------------------------------------------------------------------------

static void std_global_int() {
    int start = get_arg_start();
    // printf("DEBUG: std_global_int start=%d argc=%d\n", start, get_vm()->native_argc);
    KValue v = get_vm()->native_args[start];
    
    if (v.type == VAL_INT) KReturnInt(v.as.integer);
    else if (v.type == VAL_FLOAT) {
        KReturnInt((KInt)v.as.single_prec);
    }
    else if (v.type == VAL_DOUBLE) {
        KReturnInt((KInt)v.as.double_prec);
    }
    else if (v.type == VAL_BOOL) {
        KReturnInt(v.as.boolean ? 1 : 0);
    }
    else if (v.type == VAL_STRING) {
        // printf("DEBUG: converting string '%s'\n", v.as.str);
        KReturnInt(atoll(v.as.str));
    } else if (v.type == VAL_OBJ) {
        KObj* obj = (KObj*)v.as.obj;
        if (obj->header.type == OBJ_STRING) {
             // printf("DEBUG: converting obj string '%s'\n", ((KObjString*)obj)->chars);
            KReturnInt(atoll(((KObjString*)obj)->chars));
        } else {
            KReturnInt(0);
        }
    } else {
        KReturnInt(0);
    }
}

static void std_global_float() {
    int start = get_arg_start();
    KValue v = get_vm()->native_args[start];
    
    if (v.type == VAL_INT) KReturnFloat((double)v.as.integer);
    else if (v.type == VAL_FLOAT) KReturnFloat(v.as.single_prec);
    else if (v.type == VAL_DOUBLE) KReturnFloat(v.as.double_prec);
    else if (v.type == VAL_BOOL) KReturnFloat(v.as.boolean ? 1.0 : 0.0);
    else if (v.type == VAL_STRING) {
        KReturnFloat(atof(v.as.str));
    } else if (v.type == VAL_OBJ) {
        KObj* obj = (KObj*)v.as.obj;
        if (obj->header.type == OBJ_STRING) {
            KReturnFloat(atof(((KObjString*)obj)->chars));
        } else {
            KReturnFloat(0.0);
        }
    } else {
        KReturnFloat(0.0);
    }
}

static void std_global_string() {
    int start = get_arg_start();
    KValue v = get_vm()->native_args[start];
    char* s = to_string(v);
    KReturnString(s);
    free(s);
}

static void std_global_bool() {
    int start = get_arg_start();
    KValue v = get_vm()->native_args[start];
    
    if (v.type == VAL_BOOL) KReturnBool(v.as.boolean);
    else if (v.type == VAL_INT) KReturnBool(v.as.integer != 0);
    else if (v.type == VAL_FLOAT) KReturnBool(v.as.single_prec != 0.0f);
    else if (v.type == VAL_DOUBLE) KReturnBool(v.as.double_prec != 0.0);
    else if (v.type == VAL_NULL) KReturnBool(false);
    else if (v.type == VAL_STRING) KReturnBool(strlen(v.as.str) > 0);
    else if (v.type == VAL_OBJ) {
        KObj* obj = (KObj*)v.as.obj;
        if (obj->header.type == OBJ_STRING) {
            KReturnBool(((KObjString*)obj)->length > 0);
        } else {
            KReturnBool(true); // Non-null object is true
        }
    } else {
        KReturnBool(false);
    }
}

static void register_exception(const char* name) {
    KVM* vm = get_vm();
    
    // Create class object
    KObjClass* klass = (KObjClass*)malloc(sizeof(KObjClass));
    klass->header.type = OBJ_CLASS;
    klass->header.marked = false;
    klass->header.next = vm->objects;
    klass->header.size = sizeof(KObjClass);
    vm->objects = (KObjHeader*)klass;
    
    klass->name = strdup(name);
    klass->parent = NULL; 
    init_table(&klass->methods);
    
    // Set to globals
    KValue val;
    val.type = VAL_OBJ;
    val.as.obj = (KObj*)klass;
    
    table_set(&vm->globals, name, val);
}

static void register_exception_classes() {
    register_exception("Error");
    register_exception("DivisionByZeroError");
    register_exception("NilReferenceError");
    register_exception("IndexOutOfBoundsError");
    register_exception("TypeMismatchError");
    register_exception("FileNotFoundError");
    register_exception("IllegalArgumentError");
    register_exception("RuntimeError");
}

void kstd_register() {
    srand((unsigned)time(NULL));
    
    register_exception_classes();
    
    // OS
    KLibNew("os");
    KLibAdd("os", "function", "print", (void*)&std_os_print);
    KLibAdd("os", "function", "println", (void*)&std_os_println);
    KLibAdd("os", "function", "input", (void*)&std_os_input);
    KLibAdd("os", "function", "system", (void*)&std_os_system);
    KLibAdd("os", "function", "printf", (void*)&std_os_printf);
    KLibAdd("os", "function", "open", (void*)&std_os_open);
    KLibAdd("os", "function", "write", (void*)&std_os_write);
    KLibAdd("os", "function", "writeBytes", (void*)&std_os_writeBytes);
    KLibAdd("os", "function", "mkDir", (void*)&std_os_mkDir);
    KLibAdd("os", "function", "rmDir", (void*)&std_os_rmDir);
    KLibAdd("os", "function", "rmFile", (void*)&std_os_rmFile);
    KLibAdd("os", "function", "mkFile", (void*)&std_os_mkFile);
    KLibAdd("os", "function", "getMainFilePath", (void*)&std_os_getMainFilePath);
    KLibAdd("os", "function", "getOSName", (void*)&std_os_getOSName);
    KLibAdd("os", "function", "getOSVersion", (void*)&std_os_getOSVersion);
    KLibAdd("os", "function", "getOSArch", (void*)&std_os_getOSArch);
    
    // Time
    KLibNew("time");
    KLibAdd("time", "function", "now", (void*)&std_time_now);
    KLibAdd("time", "function", "ticks", (void*)&std_time_ticks);
    KLibAdd("time", "function", "sleep", (void*)&std_time_sleep);
    KLibAdd("time", "function", "format", (void*)&std_time_format);
    KLibAdd("time", "function", "parse", (void*)&std_time_parse);
    KLibAdd("time", "function", "getYear", (void*)&std_time_getYear);
    KLibAdd("time", "function", "getMonth", (void*)&std_time_getMonth);
    KLibAdd("time", "function", "getDay", (void*)&std_time_getDay);
    KLibAdd("time", "function", "getHour", (void*)&std_time_getHour);
    KLibAdd("time", "function", "getMinute", (void*)&std_time_getMinute);
    KLibAdd("time", "function", "getSecond", (void*)&std_time_getSecond);
    
    // Net
    KLibNew("net");
    KLibAdd("net", "function", "httpGet", (void*)&std_net_http_get);
    KLibAdd("net", "function", "httpPost", (void*)&std_net_http_post);
    KLibAdd("net", "function", "dial", (void*)&std_net_dial);
    KLibAdd("net", "function", "listen", (void*)&std_net_listen);
    KLibAdd("net", "function", "accept", (void*)&std_net_accept);
    KLibAdd("net", "function", "tcpSend", (void*)&std_net_tcpSend);
    KLibAdd("net", "function", "tcpRecv", (void*)&std_net_tcpRecv);
    KLibAdd("net", "function", "tcpClose", (void*)&std_net_tcpClose);
    KLibAdd("net", "function", "getIP", (void*)&std_net_getIP);
    KLibAdd("net", "function", "setNonBlocking", (void*)&std_net_setNonBlocking);
    KLibAdd("net", "function", "select", (void*)&std_net_select);
    
    // String
    KLibNew("string");
    KLibAdd("string", "function", "len", (void*)&std_string_len);
    KLibAdd("string", "function", "sub", (void*)&std_string_sub);
    KLibAdd("string", "function", "replace", (void*)&std_string_replace);
    KLibAdd("string", "function", "toUpper", (void*)&std_string_toUpper);
    KLibAdd("string", "function", "toLower", (void*)&std_string_toLower);
    KLibAdd("string", "function", "trim", (void*)&std_string_trim);
    KLibAdd("string", "function", "split", (void*)&std_string_split);
    KLibAdd("string", "function", "join", (void*)&std_string_join);
    KLibAdd("string", "function", "indexOf", (void*)&std_string_indexOf);
    KLibAdd("string", "function", "lastIndexOf", (void*)&std_string_lastIndexOf);
    KLibAdd("string", "function", "int", (void*)&std_string_int);
    
    // Math
    KLibNew("math");
    KLibAdd("math", "function", "abs", (void*)&std_math_abs);
    KLibAdd("math", "function", "max", (void*)&std_math_max);
    KLibAdd("math", "function", "min", (void*)&std_math_min);
    KLibAdd("math", "function", "pow", (void*)&std_math_pow);
    KLibAdd("math", "function", "sqrt", (void*)&std_math_sqrt);
    KLibAdd("math", "function", "round", (void*)&std_math_round);
    KLibAdd("math", "function", "floor", (void*)&std_math_floor);
    KLibAdd("math", "function", "ceil", (void*)&std_math_ceil);
    KLibAdd("math", "function", "random", (void*)&std_math_random);
    KLibAdd("math", "function", "pi", (void*)&std_math_pi);
    
    // Algorithm
    KLibNew("algorithm");
    KLibAdd("algorithm", "function", "sort", (void*)&std_algo_sort);
    KLibAdd("algorithm", "function", "reverse", (void*)&std_algo_reverse);
    KLibAdd("algorithm", "function", "find", (void*)&std_algo_find);
    KLibAdd("algorithm", "function", "sum", (void*)&std_algo_sum);
    KLibAdd("algorithm", "function", "average", (void*)&std_algo_average);
    
    // JSON
    KLibNew("json");
    KLibAdd("json", "function", "stringify", (void*)&std_json_stringify);
    KLibAdd("json", "function", "parse", (void*)&std_json_parse);
    KLibAdd("json", "function", "get", (void*)&std_json_get);
    KLibAdd("json", "function", "set", (void*)&std_json_set);
    
    // Thread
    KLibNew("thread");
    KLibAdd("thread", "function", "create", (void*)&std_thread_create);
    KLibAdd("thread", "function", "join", (void*)&std_thread_join);
    KLibAdd("thread", "function", "detach", (void*)&std_thread_detach);
    KLibAdd("thread", "function", "sleep", (void*)&std_thread_sleep);
    KLibAdd("thread", "function", "lock", (void*)&std_thread_lock);
    KLibAdd("thread", "function", "unlock", (void*)&std_thread_unlock);
    KLibAdd("thread", "function", "id", (void*)&std_thread_id);
    KLibAdd("thread", "function", "kill", (void*)&std_thread_kill);
    KLibAdd("thread", "function", "mutex", (void*)&std_thread_mutex_create);
    KLibAdd("thread", "function", "mutexLock", (void*)&std_thread_mutex_lock);
    KLibAdd("thread", "function", "mutexUnlock", (void*)&std_thread_mutex_unlock);
    KLibAdd("thread", "function", "mutexDestroy", (void*)&std_thread_mutex_destroy);
    
    // Dynlib
    KLibNew("dynlib");
    KLibAdd("dynlib", "function", "load", (void*)&std_dynlib_load);
    KLibAdd("dynlib", "function", "get", (void*)&std_dynlib_get);
    KLibAdd("dynlib", "function", "call", (void*)&std_dynlib_call);
    KLibAdd("dynlib", "function", "unload", (void*)&std_dynlib_unload);
    KLibAdd("dynlib", "function", "getLastError", (void*)&std_dynlib_getLastError);

    // Global Conversion Functions (Moved to OS as requested)
    KLibAdd("os", "function", "int", (void*)&std_global_int);
    KLibAdd("os", "function", "float", (void*)&std_global_float);
    KLibAdd("os", "function", "string", (void*)&std_global_string);
    KLibAdd("os", "function", "bool", (void*)&std_global_bool);

    // Map Class
    KLibNewClass("Map");
    KLibAddMethod("Map", "_init", (void*)&std_map_init);
    KLibAddMethod("Map", "set", (void*)&std_map_set);
    KLibAddMethod("Map", "get", (void*)&std_map_get);
    KLibAddMethod("Map", "remove", (void*)&std_map_remove);
    KLibAddMethod("Map", "contains", (void*)&std_map_contains);
    KLibAddMethod("Map", "size", (void*)&std_map_size);
    KLibAddMethod("Map", "keys", (void*)&std_map_keys);
    KLibAddMethod("Map", "values", (void*)&std_map_values);

    // Array Class
    KLibNewClass("Array");
    KLibAddMethod("Array", "push", (void*)&std_array_push);
    KLibAddMethod("Array", "pop", (void*)&std_array_pop);
    KLibAddMethod("Array", "len", (void*)&std_array_len);
    KLibAddMethod("Array", "removeAt", (void*)&std_array_removeAt);
    KLibAddMethod("Array", "clear", (void*)&std_array_clear);
}
