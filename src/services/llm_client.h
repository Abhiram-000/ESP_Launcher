/*
 * llm_client.h — Cloud LLM HTTP Client
 * Supports OpenAI-compatible API endpoints (OpenAI, Anthropic-proxy, Ollama).
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Provider selection ───────────────────────────────────────────────────── */
typedef enum { LLM_PROVIDER_OPENAI = 0, LLM_PROVIDER_CUSTOM = 1 } llm_provider_t;

/* ── Result ───────────────────────────────────────────────────────────────── */
typedef enum {
    LLM_OK          = 0,
    LLM_ERR_WIFI    = 1,
    LLM_ERR_HTTP    = 2,
    LLM_ERR_JSON    = 3,
    LLM_ERR_OOM     = 4,
    LLM_ERR_TIMEOUT = 5,
} llm_err_t;

/* Streamed chunk callback — called for each token as it arrives.
 * Returning false from the callback aborts the stream.               */
typedef bool (*llm_chunk_cb_t)(const char* token, size_t len, void* user);

/* ── API ──────────────────────────────────────────────────────────────────── */
#ifdef __cplusplus
extern "C" {
#endif

/* One-time init: loads API key from NVS. Call once at app start. */
void llm_client_init(void);

/* Send a user message with the current history context.
 * on_chunk is called for each streamed token.
 * Returns LLM_OK if the full response was received.                  */
llm_err_t llm_client_send(const char* user_msg,
                           llm_chunk_cb_t on_chunk,
                           void* user_ctx);

/* Save / load API key to/from NVS */
bool llm_client_set_api_key(const char* key);
bool llm_client_get_api_key(char* out, size_t max_len);

/* Reset conversation history (trims history.jsonl) */
void llm_client_clear_history(void);

/* Append assistant reply to history.jsonl after stream completes */
void llm_client_commit_reply(const char* reply, size_t len);

#ifdef __cplusplus
}
#endif
