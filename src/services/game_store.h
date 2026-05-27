#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAME_STORE_ERR_OK = 0,
    GAME_STORE_ERR_NO_WIFI,
    GAME_STORE_ERR_NETWORK,
    GAME_STORE_ERR_BAD_METADATA,
    GAME_STORE_ERR_NO_SPACE,
    GAME_STORE_ERR_ALREADY_INSTALLED,
    GAME_STORE_ERR_CORRUPT_ROM,
    GAME_STORE_ERR_VERSION_SAME,
    GAME_STORE_ERR_FS_ERROR,
    GAME_STORE_ERR_ABORTED,
} game_store_err_t;

typedef struct {
    char     id[32];          // Unique slug, used as filename
    char     name[48];
    char     version[12];
    char     genre[20];
    char     description[128];
    uint32_t size_bytes;
    uint32_t crc32;
    char     url[192];
    uint8_t  mapper;
    bool     is_installed;
    bool     has_update;      // cloud version > local version
} game_entry_t;

typedef struct {
    int              progress_pct;   // 0–100, UI polls this
    int              bytes_done;
    int              bytes_total;
    game_store_err_t last_error;
    char             active_game_id[32];
    bool             is_busy;        // true during any network operation
} game_store_status_t;

/**
 * @brief Initialize the Game Store system.
 * Mounts LittleFS, loads local catalog, and cleans up temp files.
 */
game_store_err_t game_store_init(void);

/**
 * @brief Fetch the latest catalog from the cloud.
 * This function blocks until completion. (Can be called from a dedicated task).
 */
game_store_err_t game_store_refresh_catalog(void);

/**
 * @brief Start downloading a game by ID.
 * This is blocking. Run in a separate task.
 */
game_store_err_t game_store_install(const char* id);

/**
 * @brief Delete a locally installed game.
 */
game_store_err_t game_store_delete(const char* id);

/**
 * @brief Abort any ongoing download or network operation.
 */
game_store_err_t game_store_abort(void);

/**
 * @brief Get the current status of the game store (e.g., download progress).
 * Safe to call from UI task.
 */
void game_store_get_status(game_store_status_t* out);

/**
 * @brief Get a merged list of games (local + cloud).
 * @param out Array of game_entry_t to populate.
 * @param max_count Maximum number of entries to populate.
 * @return Number of entries returned.
 */
int game_store_get_catalog(game_entry_t* out, int max_count);

/**
 * @brief Get the full LittleFS path to an installed ROM.
 * @param id The game ID.
 * @param path_out Buffer to store the path.
 * @param len Size of the buffer.
 * @return true if game is installed and path was copied, false otherwise.
 */
bool game_store_get_rom_path(const char* id, char* path_out, size_t len);

/**
 * @brief Get total free space in bytes on the games partition.
 */
size_t game_store_get_free_bytes(void);

/**
 * @brief Get total capacity in bytes on the games partition.
 */
size_t game_store_get_total_bytes(void);

#ifdef __cplusplus
}
#endif
