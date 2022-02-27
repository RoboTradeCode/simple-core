#include "logging.h"

/**
 * Инициализация логирования
 */
void init_logging()
{
    // Параметры для логирования в файлы
    int max_size = 1048576 * 2;  // 2 MiB
    int max_files = 5;

    // Логирование биржевых стаканов
    auto orderbooks = spdlog::rotating_logger_mt<spdlog::async_factory>(
        "orderbooks",
        "logs/orderbooks.log",
        max_size,
        max_files
    );

    // Логирование баланса и ошибок
    auto balance = spdlog::rotating_logger_mt<spdlog::async_factory>(
        "balance",
        "logs/balance.log",
        max_size,
        max_files
    );

    // Логирование ошибок
    auto errors = spdlog::rotating_logger_mt<spdlog::async_factory>(
        "errors",
        "logs/errors.log",
        max_size,
        max_files
    );

    // Логирование ордеров
    std::vector<spdlog::sink_ptr> orders_sinks;
    auto orders_file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/orders.log", max_size, max_files);
    auto orders_stdout = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    orders_sinks.push_back(orders_file);
    orders_sinks.push_back(orders_stdout);
    auto orders = std::make_shared<spdlog::logger>("orders", begin(orders_sinks), end(orders_sinks));
    spdlog::register_logger(orders);

    // Политика сброса буфера
    spdlog::flush_every(std::chrono::seconds(5));
}
