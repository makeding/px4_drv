#pragma once

#include <cerrno>

/* Userspace view required by the shared SmartCard state machine. */
enum it930x_uart_baudrate {
	IT930X_UART_BAUDRATE_9600 = 0,
	IT930X_UART_BAUDRATE_19200 = 1,
};
