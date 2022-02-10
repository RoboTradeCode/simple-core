#ifndef TRADE_CORE_LOGGING_H
#define TRADE_CORE_LOGGING_H


#include <boost/log/trivial.hpp>
#include <boost/log/expressions/keyword.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;

typedef src::severity_channel_logger<logging::trivial::severity_level, std::string> logger_t;

/**
 * Инициализация логирования
 */
void init_logging();


#endif  // TRADE_CORE_LOGGING_H
