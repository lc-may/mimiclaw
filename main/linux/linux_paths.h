/**
 * @file linux_paths.h
 * @brief Path utilities for Linux data directory management
 */

#pragma once

#include <stddef.h>

/**
 * Get the base data directory path (~/.mimiclaw)
 * @return Pointer to static buffer with path
 */
const char *mimi_get_data_dir(void);

/**
 * Resolve a relative or virtual path within the data directory.
 * Accepts forms like "config/SOUL.md", ".mimiclaw/config/SOUL.md",
 * "~/.mimiclaw/config/SOUL.md", or the already-expanded absolute path.
 * @param rel_path Relative or virtual path
 * @param buf Buffer to store full path
 * @param buf_size Size of buffer
 * @return 0 on success, -1 on error
 */
int mimi_get_full_path(const char *rel_path, char *buf, size_t buf_size);

/**
 * Ensure data directory and subdirectories exist
 * @return 0 on success, -1 on error
 */
int mimi_ensure_data_dirs(void);

/* Convenience macros for common paths */
#define MIMI_PATH_MEMORY   "memory/MEMORY.md"
#define MIMI_PATH_SOUL     "config/SOUL.md"
#define MIMI_PATH_USER     "config/USER.md"
#define MIMI_PATH_HEARTBEAT "HEARTBEAT.md"
#define MIMI_PATH_CRON     "cron.json"
#define MIMI_PATH_SESSIONS "sessions/"
#define MIMI_PATH_SKILLS   "skills/"
#define MIMI_PATH_LLM_TOOLS_JS "llm_tools/js/"
