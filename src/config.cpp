#include "config.hpp"
#include <filesystem>

// Значения по умолчанию
const char* DEFAULT_BTC_THRESHOLD      = "0.0008";
const char* DEFAULT_USDT_THRESHOLD     = "40";
const char* DEFAULT_SELL_RATIO         = "1.0015";
const char* DEFAULT_BUY_RATIO          = "0.9985";
const char* DEFAULT_LOWER_BOUND_RATIO  = "0.9995";
const char* DEFAULT_UPPER_BOUND_RATIO  = "1.0005";
const char* DEFAULT_SUBSCRIBER_CHANNEL = "aeron:ipc";
const char* DEFAULT_PUBLISHER_CHANNEL  = "aeron:ipc?control=localhost:40456|control-mode=dynamic";

const int DEFAULT_ORDERBOOKS_STREAM_ID     = 1006;
const int DEFAULT_BALANCES_STREAM_ID       = 1005;
const int DEFAULT_ORDER_STATUSES_STREAM_ID = 1007;
const int DEFAULT_GATEWAY_STREAM_ID        = 1004;
const int DEFAULT_METRICS_STREAM_ID        = 100;
const int DEFAULT_ERRORS_STREAM_ID         = 1008;
const int DEFAULT_IDLE_STRATEGY_SLEEP_MS   = 1;
const int DEFAULT_BUFFER_SIZE              = 1400;

/**
 * Преобразует файл конфигурации в структуру, понятную ядру
 *
 * @param file_path Путь к файлу конфигурации в формате TOML
 * @return Конфигурация ядра
 */
core_config parse_config(std::string config_file_path_)
{
    // проверим существование конфигурационного файла
    std::filesystem::path path(config_file_path_);
    if(not std::filesystem::exists(path) && not std::filesystem::is_regular_file(path)){
        throw std::invalid_argument("File " + config_file_path_ + " doesn't exists.");
    }
    // Инициализация структуры и корня файла конфигурации
    core_config config;
    toml::table tbl = toml::parse_file(config_file_path_);

    // Сокращения для удобства доступа
    toml::node_view gate             = tbl["core"];
    toml::node_view configurator     = tbl["configuration"];
    toml::node_view aeron            = tbl["aeron"];
    toml::node_view agent_subscriber = aeron["subscribers"]["agent"];
    toml::node_view logs             = aeron["publishers"]["logs"];
    toml::node_view agent_publisher  = aeron["publishers"]["agent"];

    // получаем имя и инстанс
    config.exchange.name     = gate["exchange_name"].value_or("ftx");
    config.exchange.instance = gate["instance_name"].value_or(0);

    config.source            = configurator["source"].value_or("unknown");

    config.config_uri        = configurator["api"][0].value_or("unknown");
    config.config_target     = configurator["api"][1].value_or("unknown");

    // канал агента
    config.aeron_agent.subscribers.agent.channel   = agent_subscriber[0].value_or("");
    config.aeron_agent.subscribers.agent.stream_id = agent_subscriber[1].value_or(0);

    // канал логов
    config.aeron_agent.publishers.agent.channel    = logs[0].value_or("");
    config.aeron_agent.publishers.agent.stream_id  = logs[1].value_or(0);

    // канал агента
    config.aeron_agent.publishers.agent.channel   = agent_publisher[0].value_or("");
    config.aeron_agent.publishers.agent.stream_id = agent_publisher[1].value_or(0);
    // Сокращения для удобства доступа
    /*toml::node_view exchange       = tbl["exchange"];
    toml::node_view aeron          = tbl["aeron"];
    toml::node_view subscribers    = aeron["subscribers"];
    toml::node_view publishers     = aeron["publishers"];
    toml::node_view balance        = subscribers["balances"];
    toml::node_view orderbooks     = subscribers["orderbooks"];
    toml::node_view order_statuses = subscribers["order_statuses"];
    toml::node_view gateway        = publishers["gateway"];
    toml::node_view metrics        = publishers["metrics"];
    toml::node_view errors         = publishers["errors"];

    // Пороговые значения для инструментов
    config.exchange.btc_threshold = exchange["btc_threshold"].value_or(DEFAULT_BTC_THRESHOLD);
    config.exchange.usdt_threshold = exchange["usdt_threshold"].value_or(DEFAULT_USDT_THRESHOLD);

    // Коэффициенты для вычисления цены ордеров
    config.exchange.sell_ratio = exchange["sell_ratio"].value_or(DEFAULT_SELL_RATIO);
    config.exchange.buy_ratio = exchange["buy_ratio"].value_or(DEFAULT_BUY_RATIO);

    // Коэффициенты для вычисления границ удержания ордеров
    config.exchange.lower_bound_ratio = exchange["lower_bound_ratio"].value_or(DEFAULT_LOWER_BOUND_RATIO);
    config.exchange.upper_bound_ratio = exchange["upper_bound_ratio"].value_or(DEFAULT_UPPER_BOUND_RATIO);

    // шаг цены и объема
    config.exchange.price_increment   = exchange["price_increment"].value_or(1.0);
    config.exchange.size_increment    = exchange["size_increment"].value_or(1.0);

    // Продолжительность для стратегии ожидания Aeron в мс
    int idle_strategy_sleep_ms = subscribers["idle_strategy_sleep_ms"].value_or(DEFAULT_IDLE_STRATEGY_SLEEP_MS);
    config.aeron.subscribers.idle_strategy_sleep_ms = idle_strategy_sleep_ms;

    // Subscriber для приёма биржевого стакана
    toml::array* orderbooks_destinations = orderbooks["destinations"].as_array();
    config.aeron.subscribers.orderbooks.channel = orderbooks["channel"].value_or(DEFAULT_SUBSCRIBER_CHANNEL);
    config.aeron.subscribers.orderbooks.stream_id = orderbooks["stream_id"].value_or(DEFAULT_ORDERBOOKS_STREAM_ID);
    for (toml::node& destination: *orderbooks_destinations)
        config.aeron.subscribers.orderbooks.destinations.emplace_back(destination.value_or(""));

    // Subscriber для приёма баланса
    toml::array* balance_destinations = balance["destinations"].as_array();
    config.aeron.subscribers.balance.channel = balance["channel"].value_or(DEFAULT_SUBSCRIBER_CHANNEL);
    config.aeron.subscribers.balance.stream_id = balance["stream_id"].value_or(DEFAULT_BALANCES_STREAM_ID);
    for (toml::node& destination: *balance_destinations)
        config.aeron.subscribers.balance.destinations.emplace_back(destination.value_or(""));

    // Subscriber для приема статуса ордеров
    config.aeron.subscribers.order_statuses.channel = order_statuses["channel"].value_or(DEFAULT_SUBSCRIBER_CHANNEL);
    config.aeron.subscribers.order_statuses.stream_id = order_statuses["stream_id"].value_or(DEFAULT_ORDER_STATUSES_STREAM_ID);

    // Publisher для отправки ордеров
    config.aeron.publishers.gateway.channel = gateway["channel"].value_or(DEFAULT_PUBLISHER_CHANNEL);
    config.aeron.publishers.gateway.stream_id = gateway["stream_id"].value_or(DEFAULT_GATEWAY_STREAM_ID);
    config.aeron.publishers.gateway.buffer_size = gateway["buffer_size"].value_or(DEFAULT_BUFFER_SIZE);

    // Publisher для отправки метрик
    config.aeron.publishers.metrics.channel = metrics["channel"].value_or(DEFAULT_PUBLISHER_CHANNEL);
    config.aeron.publishers.metrics.stream_id = metrics["stream_id"].value_or(DEFAULT_METRICS_STREAM_ID);
    config.aeron.publishers.metrics.buffer_size = metrics["buffer_size"].value_or(DEFAULT_BUFFER_SIZE);

    // Publisher для отправки ошибок
    config.aeron.publishers.errors.channel = errors["channel"].value_or(DEFAULT_PUBLISHER_CHANNEL);
    config.aeron.publishers.errors.stream_id = errors["stream_id"].value_or(DEFAULT_ERRORS_STREAM_ID);
    config.aeron.publishers.errors.buffer_size = errors["buffer_size"].value_or(DEFAULT_BUFFER_SIZE);*/

    return config;
}
