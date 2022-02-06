#include "logging.h"

BOOST_LOG_ATTRIBUTE_KEYWORD(a_severity, "Severity", logging::trivial::severity_level)
BOOST_LOG_ATTRIBUTE_KEYWORD(a_channel, "Channel", std::string)

void init_logging()
{
    logging::add_common_attributes();

    logging::add_console_log(
        std::cout,
        keywords::filter = a_channel == "orders",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::auto_flush = true
    );

    logging::add_file_log(
        keywords::filter = a_channel == "orderbooks",
        keywords::file_name = "logs/orderbooks.log",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::open_mode = std::ios_base::app,
        keywords::auto_flush = true,
        keywords::rotation_size = 10 * 1024 * 1024
    );

    logging::add_file_log(
        keywords::filter = a_channel == "balance",
        keywords::file_name = "logs/balance.log",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::open_mode = std::ios_base::app,
        keywords::auto_flush = true,
        keywords::rotation_size = 10 * 1024 * 1024
    );

    logging::add_file_log(
        keywords::filter = a_channel == "orders",
        keywords::file_name = "logs/orders.log",
        keywords::format = "[%TimeStamp%]: %Message%",
        keywords::open_mode = std::ios_base::app,
        keywords::auto_flush = true,
        keywords::rotation_size = 10 * 1024 * 1024
    );
}
