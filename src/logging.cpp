#include "logging.h"

BOOST_LOG_ATTRIBUTE_KEYWORD(a_severity, "Severity", logging::trivial::severity_level)
BOOST_LOG_ATTRIBUTE_KEYWORD(a_channel, "Channel", std::string)

/**
 * Инициализация логирования
 */
void init_logging()
{
    // Добавление поддержки %TimeStamp% в формате
    logging::add_common_attributes();

    // Логирование ордеров в стандартный поток вывода
    logging::add_console_log(
        std::cout,
        keywords::filter = a_channel == "orders",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::auto_flush = true                     // Обновление лога после каждой записи
    );

    // Логирование биржевых стаканов в файл
    logging::add_file_log(
        keywords::filter = a_channel == "orderbooks",
        keywords::file_name = "logs/orderbooks.log",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::open_mode = std::ios_base::app,       // Дозапись
        keywords::auto_flush = true,                    // Обновление лога после каждой записи
        keywords::rotation_size = 10 * 1024 * 1024      // 10 MB
    );

    // Логирование балансов в файл
    logging::add_file_log(
        keywords::filter = a_channel == "balances",
        keywords::file_name = "logs/balances.log",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::open_mode = std::ios_base::app,       // Дозапись
        keywords::auto_flush = true,                    // Обновление лога после каждой записи
        keywords::rotation_size = 10 * 1024 * 1024      // 10 MB
    );

    // Логирование ордеров в файл
    logging::add_file_log(
        keywords::filter = a_channel == "orders",
        keywords::file_name = "logs/orders.log",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::open_mode = std::ios_base::app,       // Дозапись
        keywords::auto_flush = true,                    // Обновление лога после каждой записи
        keywords::rotation_size = 10 * 1024 * 1024      // 10 MB
    );

    // TODO: Найти оптимальный размер ротации для каждого лога
    // TODO: Отключить обновление лога после каждой записи, когда завершится этап отладки
}
