/*
 * llm_client.c — Cloud LLM HTTP Client Implementation
 * Streams responses token-by-token using esp_http_client.
 * History is maintained in /games/ai/history.jsonl (JSONL format).
 */

#include "llm_client.h"
#include "../config/sys_config.h"
#include "wifi_mgr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char* TAG = "LLM";

/* ── Internal state ─────────────────────────────────────────────────────── */
static char s_api_key[128]  = {0};
static char s_model[64]     = AI_LLM_MODEL_DEFAULT;

/* Per-request streaming context */
typedef struct {
    llm_chunk_cb_t  cb;
    void*           user;
    char*           reply_buf;      /* Accumulates full reply for history */
    size_t          reply_len;
    size_t          reply_cap;
    bool            inside_content; /* JSON "content" field detector state */
    char            json_key[32];   /* Running key accumulator             */
    bool            in_key;
    bool            in_value;
    int             depth;
} stream_ctx_t;

/* ── One-time init ─────────────────────────────────────────────────────── */
void llm_client_init(void) {
    char key[128] = {0};
    if (llm_client_get_api_key(key, sizeof(key))) {
        strncpy(s_api_key, key, sizeof(s_api_key) - 1);
        ESP_LOGI(TAG, "API key loaded from NVS");
    } else {
        ESP_LOGW(TAG, "No API key in NVS — use llm_client_set_api_key() first");
    }
}

/* ── NVS key management ─────────────────────────────────────────────────── */
bool llm_client_set_api_key(const char* key) {
    nvs_handle_t h;
    if (nvs_open(AI_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_str(h, AI_NVS_KEY_APIKEY, key) == ESP_OK;
    nvs_commit(h); nvs_close(h);
    if (ok) strncpy(s_api_key, key, sizeof(s_api_key) - 1);
    return ok;
}

bool llm_client_get_api_key(char* out, size_t max_len) {
    nvs_handle_t h;
    if (nvs_open(AI_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = max_len;
        bool ok = nvs_get_str(h, AI_NVS_KEY_APIKEY, out, &len) == ESP_OK;
        nvs_close(h);
        if (ok && out[0] != '\0') return true;
    }
    
    if (s_api_key[0] != '\0') {
        strncpy(out, s_api_key, max_len - 1);
        out[max_len - 1] = '\0';
        return true;
    }
    return false;
}

/* ── History management ─────────────────────────────────────────────────── */
/* Trim history.jsonl if it exceeds AI_CONTEXT_MAX_CHARS by dropping the
 * oldest lines (FIFO). Simple single-pass approach using PSRAM buffer.    */
static void _trim_history(void) {
    FILE* f = fopen(AI_HISTORY_PATH, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= AI_CONTEXT_MAX_CHARS) { fclose(f); return; }

    /* Skip oldest lines until size fits */
    char line[512];
    long kept_start = 0;
    while (ftell(f) < sz - AI_CONTEXT_MAX_CHARS) {
        if (!fgets(line, sizeof(line), f)) break;
        kept_start = ftell(f);
    }
    long kept_len = sz - kept_start;
    char* buf = (char*)heap_caps_malloc(kept_len + 1, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(f); return; }
    fseek(f, kept_start, SEEK_SET);
    fread(buf, 1, kept_len, f); fclose(f);

    FILE* w = fopen(AI_HISTORY_PATH, "wb");
    if (w) { fwrite(buf, 1, kept_len, w); fclose(w); }
    heap_caps_free(buf);
}

static char* _escape_json(const char* input) {
    if (!input) return NULL;
    size_t len = strlen(input);
    char* esc = (char*)heap_caps_malloc(len * 2 + 1, MALLOC_CAP_SPIRAM);
    if (!esc) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (input[i]) {
            case '"':  esc[j++] = '\\'; esc[j++] = '"';  break;
            case '\\': esc[j++] = '\\'; esc[j++] = '\\'; break;
            case '\n': esc[j++] = '\\'; esc[j++] = 'n';  break;
            case '\t': esc[j++] = '\\'; esc[j++] = 't';  break;
            case '\r': esc[j++] = '\\'; esc[j++] = 'r';  break;
            default:   esc[j++] = input[i];               break;
        }
    }
    esc[j] = '\0';
    return esc;
}

static void _append_history(const char* role, const char* content) {
    char* esc = _escape_json(content);
    if (!esc) return;
    FILE* f = fopen(AI_HISTORY_PATH, "a");
    if (f) {
        fprintf(f, "{\"role\":\"%s\",\"content\":\"%s\"}\n", role, esc);
        fclose(f);
    }
    heap_caps_free(esc);
    _trim_history();
}

void llm_client_clear_history(void) { remove(AI_HISTORY_PATH); }

void llm_client_commit_reply(const char* reply, size_t len) {
    if (!reply || len == 0) return;
    char* temp = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM);
    if (!temp) return;
    memcpy(temp, reply, len);
    temp[len] = '\0';
    _append_history("assistant", temp);
    heap_caps_free(temp);
}

/* ── Build conversation JSON from history + new message ────────────────── */
static char* _build_payload(const char* user_msg) {
    /* Header */
    char header[256];
    snprintf(header, sizeof(header),
        "{\"model\":\"%s\",\"max_tokens\":%d,\"stream\":true,"
        "\"messages\":[",
        s_model, AI_LLM_MAX_TOKENS);

    /* Load system prompt */
    char sysprompt[1024] = "You are a helpful AI assistant on an ESP32-S3 handheld. "
        "Your responses MUST be extremely concise, brief, and under 150 characters total. Keep explanations minimal. "
        "For coding tasks, write clean, self-contained Lua 5.4 scripts using the available hardware libraries: "
        "sys (sleep(ms), time(), heap(), reboot()), "
        "gpio (mode(pin,mode), write(pin,val), read(pin), toggle(pin)), "
        "i2c (write, read), uart, spi, "
        "adc (read(pin)), pwm (setup, set), "
        "fs (list(), read(file), write(file,data), delete(file)), "
        "net (wifi_connect(ssid,pass), is_connected(), http_get(url)). "
        "Always return ready-to-run Lua code without generic explanations.";
    FILE* fp = fopen(AI_SYSPROMPT_PATH, "r");
    if (fp) { fread(sysprompt, 1, sizeof(sysprompt) - 1, fp); fclose(fp); }

    char* payload = (char*)heap_caps_malloc(AI_CONTEXT_MAX_CHARS + 2048, MALLOC_CAP_SPIRAM);
    if (!payload) return NULL;
    int pos = 0;
    
    char* esc_sys = _escape_json(sysprompt);
    if (esc_sys) {
        pos += snprintf(payload + pos, 2048, "%s{\"role\":\"system\",\"content\":\"%s\"},", header, esc_sys);
        heap_caps_free(esc_sys);
    } else {
        pos += snprintf(payload + pos, 2048, "%s{\"role\":\"system\",\"content\":\"%s\"},", header, sysprompt);
    }

    /* Append history */
    FILE* hist = fopen(AI_HISTORY_PATH, "r");
    if (hist) {
        char line[512];
        while (fgets(line, sizeof(line), hist)) {
            int l = strlen(line);
            if (line[l-1] == '\n') line[l-1] = '\0';
            l = strlen(line);
            if (l < 5 || line[0] != '{' || line[l-1] != '}') {
                continue; // Skip malformed or empty history lines
            }
            if (pos < AI_CONTEXT_MAX_CHARS) {
                pos += snprintf(payload + pos, AI_CONTEXT_MAX_CHARS - pos, "%s,", line);
            }
        }
        fclose(hist);
    }

    /* Current user message */
    char* esc_user = _escape_json(user_msg);
    size_t remain = (pos < AI_CONTEXT_MAX_CHARS + 2048) ? AI_CONTEXT_MAX_CHARS + 2048 - pos : 0;
    if (esc_user) {
        pos += snprintf(payload + pos, remain, "{\"role\":\"user\",\"content\":\"%s\"}]}", esc_user);
        heap_caps_free(esc_user);
    } else {
        pos += snprintf(payload + pos, remain, "{\"role\":\"user\",\"content\":\"%s\"}]}", user_msg);
    }
    return payload;
}

/* ── Minimal SSE / JSON stream parser ──────────────────────────────────── */
/* Extracts "delta":{..."content":"<token>...} from streaming JSON lines. */
static bool _extract_content(const char* line, char* out, size_t out_max) {
    const char* needle = "\"content\":\"";
    const char* p = strstr(line, needle);
    if (!p) return false;
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i < out_max - 1) {
        if (*p == '\\' && *(p+1) == 'n') { out[i++] = '\n'; p += 2; }
        else if (*p == '\\') { p++; if (*p) out[i++] = *p++; }
        else out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

/* HTTP event handler */
static esp_err_t _http_event(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    stream_ctx_t* ctx = (stream_ctx_t*)evt->user_data;
    if (!ctx) return ESP_OK;

    char* data = (char*)evt->data;
    int   len  = evt->data_len;

    /* Split data by newline — each SSE line is "data: {...}\n" */
    char line[512] = {0};
    int  lp = 0;
    for (int i = 0; i < len; i++) {
        if (data[i] == '\n') {
            line[lp] = '\0';
            if (strncmp(line, "data: ", 6) == 0) {
                char token[128] = {0};
                if (_extract_content(line + 6, token, sizeof(token)) && token[0]) {
                    /* Deliver token to UI */
                    if (ctx->cb) ctx->cb(token, strlen(token), ctx->user);
                    /* Accumulate for history */
                    size_t tl = strlen(token);
                    if (ctx->reply_len + tl + 1 < ctx->reply_cap) {
                        memcpy(ctx->reply_buf + ctx->reply_len, token, tl);
                        ctx->reply_len += tl;
                        ctx->reply_buf[ctx->reply_len] = '\0';
                    }
                }
            }
            lp = 0;
        } else if (lp < (int)sizeof(line) - 1) {
            line[lp++] = data[i];
        }
    }
    return ESP_OK;
}

/* ── Public send ─────────────────────────────────────────────────────────── */
llm_err_t llm_client_send(const char* user_msg, llm_chunk_cb_t on_chunk, void* user_ctx) {
    if (!wifi_mgr_is_connected()) return LLM_ERR_WIFI;
    if (s_api_key[0] == '\0')     return LLM_ERR_HTTP;

    char* payload = _build_payload(user_msg);
    if (!payload) return LLM_ERR_OOM;

    char* reply_buf = (char*)heap_caps_calloc(1, AI_STREAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!reply_buf) { heap_caps_free(payload); return LLM_ERR_OOM; }

    stream_ctx_t ctx = {
        .cb = on_chunk, .user = user_ctx,
        .reply_buf = reply_buf, .reply_len = 0, .reply_cap = AI_STREAM_BUF_SIZE,
    };

    char auth_hdr[160];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", s_api_key);

    esp_http_client_config_t cfg = {
        .url                  = AI_LLM_OPENAI_URL,
        .method               = HTTP_METHOD_POST,
        .timeout_ms           = AI_HTTP_RX_TIMEOUT_MS,
        .event_handler        = _http_event,
        .user_data            = &ctx,
        .buffer_size_tx       = 4096,
        .buffer_size          = 1024,
        .crt_bundle_attach    = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_header(h, "Authorization", auth_hdr);
    esp_http_client_set_post_field(h, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(h);
    int status    = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    heap_caps_free(payload);

    llm_err_t result = LLM_OK;
    if (err != ESP_OK || (status != 200 && status != 0)) {
        ESP_LOGE(TAG, "HTTP err=%d status=%d", err, status);
        result = LLM_ERR_HTTP;
    } else {
        /* Persist user message and reply to history */
        _append_history("user", user_msg);
        if (ctx.reply_len > 0) llm_client_commit_reply(reply_buf, ctx.reply_len);
    }

    heap_caps_free(reply_buf);
    return result;
}
