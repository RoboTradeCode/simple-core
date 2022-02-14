#include "logging.h"

/**
 * Инициализация логирования
 */
void init_logging()
{
    // Параметры для логирования в файлы
    int max_size = 1048576 * 5;  // 5 MiB
    int max_files = 1;

    // Логирование биржевых стаканов, баланса и ошибок
    auto orderbooks = spdlog::rotating_logger_mt("orderbooks", "logs/orderbooks.log", max_size, max_files);
    auto balance = spdlog::rotating_logger_mt("balance", "logs/balance.log", max_size, max_files);
    auto errors = spdlog::rotating_logger_mt("errors", "logs/errors.log", max_size, max_files);

    // Логирование ордеров
    auto orders_file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/orders.log", max_size, max_files);
    auto orders_stdout = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(orders_file);
    sinks.push_back(orders_stdout);
    auto orders = std::make_shared<spdlog::logger>("orders", begin(sinks), end(sinks));
    spdlog::register_logger(orders);

    // Параметры сброса буфера
    spdlog::flush_every(std::chrono::seconds(5));
}
