#include "config.h"

const char* DEFAULT_SELL_COEFFICIENT = "1.0015";
const char* DEFAULT_BUY_COEFFICIENT = "0.9985";
const int DEFAULT_SLEEP_MS = 1;
const char* DEFAULT_SUBSCRIBER_CHANNEL = "aeron:udp?control-mode=manual";
const char* DEFAULT_PUBLISHER_CHANNEL = "aeron:udp?control=localhost:40456|control-mode=dynamic";
const int DEFAULT_ORDERBOOKS_STREAM_ID = 1001;
const int DEFAULT_BALANCES_STREAM_ID = 1002;
const int DEFAULT_GATEWAY_STREAM_ID = 1003;
const int DEFAULT_METRICS_STREAM_ID = 1004;
const int DEFAULT_ERRORS_STREAM_ID = 1005;
const int DEFAULT_BUFFER_SIZE = 1400;

/**
 * Преобразует файл конфигурации в структуру, понятную ядру
 *
 * @param file_path Путь к файлу конфигурации в формате TOML
 * @return Конфигурация ядра
 */
core_config parse_config(std::string_view file_path)
{
    // Инициализация структуры и корня файла конфигурации
    core_config config;
    toml::table tbl = toml::parse_file(file_path);

    // Сокращения для удобства доступа
    toml::node_view exchange = tbl["exchange"];
    toml::node_view aeron = tbl["aeron"];
    toml::node_view balances = aeron["balances"];
    toml::node_view orderbooks = aeron["orderbooks"];
    toml::node_view gateway = aeron["gateway"];
    toml::node_view metrics = aeron["metrics"];
    toml::node_view errors = aeron["errors"];

    // Заполнение exchange
    config.exchange.sell_coefficient = exchange["sell_coefficient"].value_or(DEFAULT_SELL_COEFFICIENT);
    config.exchange.buy_coefficient = exchange["buy_coefficient"].value_or(DEFAULT_BUY_COEFFICIENT);

    // Заполнение idle_strategy_sleep_ms
    config.aeron.idle_strategy_sleep_ms = aeron["idle_strategy_sleep_ms"].value_or(DEFAULT_SLEEP_MS);

    // Заполнение orderbooks
    toml::array* orderbooks_destinations = orderbooks["destinations"].as_array();
    config.aeron.orderbooks.channel = orderbooks["channel"].value_or(DEFAULT_SUBSCRIBER_CHANNEL);
    config.aeron.orderbooks.stream_id = orderbooks["stream_id"].value_or(DEFAULT_ORDERBOOKS_STREAM_ID);
    for (toml::node& destination: *orderbooks_destinations)
    {
        config.aeron.orderbooks.destinations.emplace_back(destination.value_or(""));
    }

    // Заполнение balances
    toml::array* balances_destinations = balances["destinations"].as_array();
    config.aeron.balances.channel = balances["channel"].value_or(DEFAULT_SUBSCRIBER_CHANNEL);
    config.aeron.balances.stream_id = balances["stream_id"].value_or(DEFAULT_BALANCES_STREAM_ID);
    for (toml::node& destination: *balances_destinations)
    {
        config.aeron.balances.destinations.emplace_back(destination.value_or(""));
    }

    // Заполнение gateway
    config.aeron.gateway.channel = gateway["channel"].value_or(DEFAULT_PUBLISHER_CHANNEL);
    config.aeron.gateway.stream_id = gateway["stream_id"].value_or(DEFAULT_GATEWAY_STREAM_ID);
    config.aeron.gateway.buffer_size = gateway["buffer_size"].value_or(DEFAULT_BUFFER_SIZE);

    // Заполнение metrics
    config.aeron.metrics.channel = metrics["channel"].value_or(DEFAULT_PUBLISHER_CHANNEL);
    config.aeron.metrics.stream_id = metrics["stream_id"].value_or(DEFAULT_METRICS_STREAM_ID);
    config.aeron.metrics.buffer_size = metrics["buffer_size"].value_or(DEFAULT_BUFFER_SIZE);

    // Заполнение errors
    config.aeron.errors.channel = errors["channel"].value_or(DEFAULT_PUBLISHER_CHANNEL);
    config.aeron.errors.stream_id = errors["stream_id"].value_or(DEFAULT_ERRORS_STREAM_ID);
    config.aeron.errors.buffer_size = errors["buffer_size"].value_or(DEFAULT_BUFFER_SIZE);

    return config;
}
