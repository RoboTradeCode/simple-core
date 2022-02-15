#include "logging.h"

/**
 * Инициализация логирования
 */
void init_logging()
{
    // Параметры для логирования в файлы
    int max_size = 1048576 * 5;  // 5 MiB
    int max_files = 1;

    // Логирование биржевых стаканов, баланса, ошибок и ордеров
    auto orderbooks = spdlog::rotating_logger_mt<spdlog::async_factory>("orderbooks", "logs/orderbooks.log", max_size, max_files);
    auto balance = spdlog::rotating_logger_mt<spdlog::async_factory>("balance", "logs/balance.log", max_size, max_files);
    auto errors = spdlog::rotating_logger_mt<spdlog::async_factory>("errors", "logs/errors.log", max_size, max_files);
    auto orders = spdlog::rotating_logger_mt<spdlog::async_factory>("orders", "logs/orders.log", max_size, max_files);

    // Политика сброса буфера
    spdlog::flush_every(std::chrono::seconds(5));
}
