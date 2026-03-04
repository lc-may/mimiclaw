#pragma once

#include "linux/linux_compat.h"

/**
 * Initialize the serial CLI.
 */
esp_err_t serial_cli_init(void);

/**
 * Run the CLI main loop.
 * Blocks until 'exit' command or Ctrl+D is received.
 *
 * @return 0 on normal exit
 */
int serial_cli_run(void);

/**
 * Signal the CLI to stop.
 * Called from signal handlers for graceful shutdown.
 */
void serial_cli_stop(void);
