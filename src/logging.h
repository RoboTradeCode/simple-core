#ifndef TRADE_CORE_LOGGING_H
#define TRADE_CORE_LOGGING_H


#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/logger.h>

/**
 * Инициализация логирования
 */
void init_logging();


#endif  // TRADE_CORE_LOGGING_H
