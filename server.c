#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "thread_pool.h"

#define BUFFER_SIZE 8192
#define QUEUE_SIZE 1024
#define SIG_BUF_SIZE 0x300  // 768 bytes
#define TOKEN_OFFSET 0x100   // 256 bytes
#define EXTRA_OFFSET 0x200   // 512 bytes

#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <link.h>

typedef long long (*sign_func)(char*, unsigned char*, int, int, unsigned char*);

static uintptr_t offset;
static sign_func sign;
static void* module;
static uintptr_t module_base;
static pthread_mutex_t sign_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    const char* short_ver;
    const char* full_ver;
    uintptr_t offset;
} version_offset_t;

static const version_offset_t VERSIONS[] = {
    {"12912", "3.1.2-12912", 0x33C38E0},
    {"13107", "3.1.2-13107", 0x33C3920},
    {"23361", "3.2.7-23361", 0x4C93C57},
    {"24815", "3.2.9-24815", 0x4E5D3B7},
    {"25765", "3.2.10-25765", 0x4F176D6},
    {"39038", "3.2.19-39038", 0x5ADE220},
    {"40990", "3.2.20-40990", 0x5000000},
    {"260401", "3.2.27-260401", 0x5D07ED0},
};

static const int VERSION_COUNT = sizeof(VERSIONS) / sizeof(VERSIONS[0]);

const char* get_full_version(const char* short_ver) {
    for (int i = 0; i < VERSION_COUNT; i++) {
        if (strcmp(VERSIONS[i].short_ver, short_ver) == 0) {
            return VERSIONS[i].full_ver;
        }
    }
    return short_ver;
}

int get_offset_for_version(const char* version, uintptr_t* out_offset) {
    for (int i = 0; i < VERSION_COUNT; i++) {
        if (strcmp(VERSIONS[i].short_ver, version) == 0 || 
            strcmp(VERSIONS[i].full_ver, version) == 0) {
            *out_offset = VERSIONS[i].offset;
            return 0;
        }
    }
    return -1;
}

int callback(struct dl_phdr_info* info, size_t, void*) {
    if (info->dlpi_name && strstr(info->dlpi_name, "wrapper.node")) {
        module_base = info->dlpi_addr;
        return 1;
    }
    return 0;
}

int load_module(uintptr_t off) {
    char* libs_to_load[] = {"libgnutls.so.30", "./libsymbols.so", NULL};
    for (int i = 0; libs_to_load[i] != NULL; i++) {
        dlopen(libs_to_load[i], RTLD_LAZY | RTLD_GLOBAL);
    }
    
    module = dlopen("./wrapper.node", RTLD_LAZY);
    if (!module) {
        fprintf(stderr, "dlopen wrapper.node failed: %s\n", dlerror());
        return -1;
    }
    
    dl_iterate_phdr(callback, NULL);
    if (module_base == 0) {
        fprintf(stderr, "Failed to find module base\n");
        return -1;
    }
    
    offset = off;
    sign = (sign_func)(module_base + offset);
    return 0;
}

int hex_decode(const char* in, unsigned char* out, int* out_len) {
    int len = strlen(in);
    *out_len = len / 2;
    // Boundary check: prevent overflow
    if (*out_len > BUFFER_SIZE) {
        return -1;
    }
    for (int i = 0; i < len; i += 2) {
        char c1 = in[i], c2 = in[i + 1];
        int v1, v2;
        if (c1 >= '0' && c1 <= '9') v1 = c1 - '0';
        else if (c1 >= 'A' && c1 <= 'F') v1 = c1 - 'A' + 10;
        else if (c1 >= 'a' && c1 <= 'f') v1 = c1 - 'a' + 10;
        else return -1;
        if (c2 >= '0' && c2 <= '9') v2 = c2 - '0';
        else if (c2 >= 'A' && c2 <= 'F') v2 = c2 - 'A' + 10;
        else if (c2 >= 'a' && c2 <= 'f') v2 = c2 - 'a' + 10;
        else return -1;
        out[i / 2] = (v1 << 4) | v2;
    }
    return 0;
}

void hex_encode(const unsigned char* in, int len, char* out) {
    static const char HEX[] = "0123456789ABCDEF";
    for (int i = 0; i < len; i++) {
        out[i * 2] = HEX[in[i] >> 4];
        out[i * 2 + 1] = HEX[in[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

int parse_json_str(const char* body, const char* key, char* out, int out_len) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* p = strstr(body, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        const char* end = strchr(p, '"');
        if (end) {
            int len = end - p < out_len - 1 ? end - p : out_len - 1;
            strncpy(out, p, len);
            out[len] = '\0';
            return 0;
        }
    } else if (*p >= '0' && *p <= '9') {
        int len = 0;
        while (p[len] >= '0' && p[len] <= '9') len++;
        len = len < out_len - 1 ? len : out_len - 1;
        strncpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

int parse_json_int(const char* body, const char* key, int* out) {
    char tmp[64];
    if (parse_json_str(body, key, tmp, sizeof(tmp)) == 0) {
        *out = atoi(tmp);
        return 0;
    }
    return -1;
}

void send_response(int fd, const char* status, const char* body, int body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n", status, body_len);
    write(fd, header, header_len);
    if (body && body_len > 0) {
        write(fd, body, body_len);
    }
}

void do_sign(int client_fd, const char* version, const char* cmd, const char* src, int seq) {
    uintptr_t off;
    if (get_offset_for_version(version, &off) != 0) {
        send_response(client_fd, "400 Bad Request", "Unsupported version", 20);
        return;
    }
    
    const char* full_ver = get_full_version(version);
    
    pthread_mutex_lock(&sign_mutex);
    
    static int module_loaded = 0;
    static uintptr_t last_offset = 0;
    
    if (!module_loaded || last_offset != off) {
        if (module) {
            dlclose(module);
            module = NULL;
        }
        if (load_module(off) != 0) {
            pthread_mutex_unlock(&sign_mutex);
            send_response(client_fd, "500 Internal Server Error", "Sign init failed", 15);
            return;
        }
        module_loaded = 1;
        last_offset = off;
    }
    
    unsigned char src_buf[BUFFER_SIZE];
    int src_len = 0;
    
    if (hex_decode(src, src_buf, &src_len) != 0) {
        pthread_mutex_unlock(&sign_mutex);
        send_response(client_fd, "400 Bad Request", "Invalid src format", 18);
        return;
    }
    
    // src_len boundary check
    if (src_len <= 0 || src_len > 4096) {
        pthread_mutex_unlock(&sign_mutex);
        send_response(client_fd, "400 Bad Request", "Invalid src length", 20);
        return;
    }
    
    unsigned char sig_buf[SIG_BUF_SIZE] = {0};
    memset(sig_buf, 0, sizeof(sig_buf));
    
    // Call sign function
    sign((char*)cmd, src_buf, src_len, seq, sig_buf);
    
    pthread_mutex_unlock(&sign_mutex);
    
    // Read length fields with boundary checks
    int token_len = sig_buf[TOKEN_OFFSET - 1];  // 255
    int extra_len = sig_buf[EXTRA_OFFSET - 1];   // 511
    int sign_len = sig_buf[SIG_BUF_SIZE - 1];   // 767
    
    // Validate lengths - they should be reasonable values
    if (token_len < 0 || token_len > 255 || extra_len < 0 || extra_len > 255 || sign_len < 0 || sign_len > 255) {
        send_response(client_fd, "500 Internal Server Error", "Invalid sign response", 21);
        return;
    }
    
    // Calculate total required space and verify it fits
    int total_hex = (token_len + extra_len + sign_len) * 2;
    if (total_hex > 4096) {
        send_response(client_fd, "500 Internal Server Error", "Sign too long", 17);
        return;
    }
    
    char token_hex[512], extra_hex[512], sign_hex[512];
    
    hex_encode(sig_buf, token_len, token_hex);
    hex_encode(sig_buf + TOKEN_OFFSET, extra_len, extra_hex);
    hex_encode(sig_buf + EXTRA_OFFSET, sign_len, sign_hex);
    
    char body[4096];
    int body_len = snprintf(body, sizeof(body),
        "{\"platform\":\"Linux\",\"value\":{\"token\":\"%s\",\"extra\":\"%s\",\"sign\":\"%s\"},\"version\":\"%s\"}",
        token_hex, extra_hex, sign_hex, full_ver);
    
    send_response(client_fd, "200 OK", body, body_len);
}

void handle_appinfo(int client_fd, const char* version) {
    const char* full_ver = get_full_version(version);
    char body[2048];
    int body_len = snprintf(body, sizeof(body),
        "{\"AppClientVersion\":39038,\"AppId\":1600001615,\"AppIdQrCode\":537313942,"
        "\"CurrentVersion\":\"%s\",\"Kernel\":\"Linux\",\"MainSigMap\":169742560,"
        "\"MiscBitmap\":32764,\"NTLoginType\":1,\"Os\":\"Linux\","
        "\"PackageName\":\"com.tencent.qq\",\"PtVersion\":\"2.0.0\",\"SsoVersion\":19,"
        "\"SubAppId\":537313942,\"SubSigMap\":0,\"VendorOs\":\"linux\","
        "\"WtLoginSdk\":\"nt.wtlogin.0.0.1\"}", full_ver);
    send_response(client_fd, "200 OK", body, body_len);
}

typedef struct {
    int client_fd;
    char buffer[BUFFER_SIZE];
} client_task_t;

void handle_client(void* arg) {
    client_task_t* task = (client_task_t*)arg;
    int fd = task->client_fd;
    char* buf = task->buffer;
    
    int n = read(fd, buf, BUFFER_SIZE - 1);
    if (n <= 0) {
        close(fd);
        free(task);
        return;
    }
    buf[n] = '\0';
    
    char method[16], path[256], version_str[16];
    if (sscanf(buf, "%15s %255s %15s", method, path, version_str) != 3) {
        send_response(fd, "400 Bad Request", "Bad request", 12);
        close(fd);
        free(task);
        return;
    }
    
    char* body_start = strstr(buf, "\r\n\r\n");
    if (!body_start) {
        send_response(fd, "400 Bad Request", "No body", 8);
        close(fd);
        free(task);
        return;
    }
    body_start += 4;
    
    int content_length = 0;
    char* cl = strstr(buf, "Content-Length:");
    if (cl) {
        cl += 15;
        while (*cl == ' ') cl++;
        content_length = atoi(cl);
    }
    
    // Limit content length to prevent overflow
    if (content_length < 0 || content_length > 65536) {
        send_response(fd, "400 Bad Request", "Invalid content length", 25);
        close(fd);
        free(task);
        return;
    }
    
    char body[8192] = {0};
    int header_len = body_start - buf;
    int received = n - header_len;
    
    if (content_length > 0 && received < content_length) {
        int need = content_length - received;
        if (need > sizeof(body) - 1) {
            need = sizeof(body) - 1;
        }
        int r = read(fd, body, need);
        if (r > 0) {
            memcpy(body + received, buf + header_len, n - header_len);
            body[n - header_len + r] = '\0';
        } else {
            int copy_len = content_length < sizeof(body) - 1 ? content_length : sizeof(body) - 1;
            strncpy(body, body_start, copy_len);
            body[copy_len] = '\0';
        }
    } else {
        int copy_len = n - header_len;
        if (copy_len > content_length) copy_len = content_length;
        if (copy_len > sizeof(body) - 1) copy_len = sizeof(body) - 1;
        strncpy(body, body_start, copy_len);
        body[copy_len] = '\0';
    }
    
    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        send_response(fd, "200 OK", "NTQQ Sign Server MT", 20);
    }
    else if (strcmp(method, "GET") == 0 && strncmp(path, "/api/sign/", 10) == 0) {
        const char* ver = path + 10;
        char* slash = strchr(ver, '/');
        if (slash) *slash = '\0';
        handle_appinfo(fd, ver);
    }
    else if (strcmp(method, "POST") == 0 && strncmp(path, "/api/sign/", 10) == 0) {
        const char* ver = path + 10;
        char* slash = strchr(ver, '/');
        if (slash) *slash = '\0';
        
        char cmd[128] = {0}, src[8192] = {0};
        int seq = 0;
        
        if (parse_json_str(body, "cmd", cmd, sizeof(cmd)) == 0 &&
            parse_json_int(body, "seq", &seq) == 0 &&
            parse_json_str(body, "src", src, sizeof(src)) == 0) {
            do_sign(fd, ver, cmd, src, seq);
        } else {
            send_response(fd, "400 Bad Request", "Bad request", 12);
        }
    }
    else {
        send_response(fd, "404 Not Found", "Not Found", 9);
    }
    
    close(fd);
    free(task);
}

static thread_pool_t pool;

void sigint_handler(int sig) {
    printf("\nShutting down...\n");
    thread_pool_shutdown(&pool);
    exit(0);
}

int main(int argc, char* argv[]) {
    int port = 11479;
    int threads = 16;
    
    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) threads = atoi(argv[2]);
    
    signal(SIGINT, sigint_handler);
    signal(SIGPIPE, SIG_IGN);
    
    thread_pool_init(&pool, threads, QUEUE_SIZE);
    printf("[INFO] Thread pool initialized with %d threads\n", threads);
    
    if (load_module(0x5ADE220) != 0) {
        fprintf(stderr, "[ERROR] Failed to load sign module\n");
        return 1;
    }
    printf("[INFO] Sign module loaded\n");
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_fd, 128) < 0) {
        perror("listen");
        return 1;
    }
    
    printf("[INFO] Server listening on 0.0.0.0:%d\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        client_task_t* task = (client_task_t*)malloc(sizeof(client_task_t));
        task->client_fd = client_fd;
        
        thread_pool_submit(&pool, handle_client, task);
    }
    
    thread_pool_shutdown(&pool);
    return 0;
}
