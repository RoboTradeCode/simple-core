#include <boost/json/src.hpp>
#include "Core.h"

Core::Core(std::string_view config_file_path)
    : idle_strategy(std::chrono::milliseconds(0)),
      has_sell_order(false),
      has_buy_order(false),
      orderbooks_logger(keywords::channel = "orderbooks"),
      balance_logger(keywords::channel = "balances"),
      orders_logger(keywords::channel = "orders")
{
    // Получение конфигурации
    core_config config = parse_config(config_file_path);

    // Инициализация канала Aeron для биржевых стаканов
    orderbooks_channel = std::make_shared<Subscriber>(
        [&](std::string_view message)
        {
            shared_from_this()->orderbooks_handler(message);
        },
        config.aeron.orderbooks.channel,
        config.aeron.orderbooks.stream_id
    );
    for (const std::string& channel: config.aeron.orderbooks.destinations)
    {
        orderbooks_channel->addDestination(channel);
    }

    // Инициализация канала Aeron для баланса
    balances_channel = std::make_shared<Subscriber>(
        [&](std::string_view message)
        {
            shared_from_this()->balances_handler(message);
        },
        config.aeron.balances.channel,
        config.aeron.balances.stream_id
    );
    for (const std::string& channel: config.aeron.balances.destinations)
    {
        balances_channel->addDestination(channel);
    }

    // Инициализация канала Aeron для шлюза
    gateway_channel = std::make_shared<Publisher>(
        config.aeron.gateway.channel,
        config.aeron.gateway.stream_id,
        config.aeron.gateway.buffer_size
    );

    // Инициализация канала Aeron для метрик
    metrics_channel = std::make_shared<Publisher>(
        config.aeron.metrics.channel,
        config.aeron.metrics.stream_id,
        config.aeron.metrics.buffer_size
    );

    // Инициализация канала Aeron для ошибок
    errors_channel = std::make_shared<Publisher>(
        config.aeron.errors.channel,
        config.aeron.errors.stream_id,
        config.aeron.errors.buffer_size
    );

    // Инициализация стратегии ожидания
    idle_strategy = aeron::SleepingIdleStrategy(std::chrono::milliseconds(config.aeron.idle_strategy_sleep_ms));

    // Инициализация коэффициентов для выставления ордеров
    SELL_COEFFICIENT = dec_float(config.exchange.sell_coefficient);
    BUY_COEFFICIENT = dec_float(config.exchange.buy_coefficient);
}

void Core::poll()
{
    // Опрос каналов
    int fragments_read_orderbooks = orderbooks_channel->poll();
    int fragments_read_balances = balances_channel->poll();

    // Выполнение стратегии ожидания
    int fragments_read = fragments_read_orderbooks + fragments_read_balances;
    idle_strategy.idle(fragments_read);
}

void Core::balances_handler(std::string_view message)
{
    BOOST_LOG_SEV(balance_logger, logging::trivial::info) << message;

    // Инициализация итератора
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message);
    simdjson::ondemand::document doc = parser.iterate(json);
    simdjson::ondemand::object obj = doc.get_object();

    // Обновление баланса
    for (auto field: obj["B"])
    {
        std::string ticker((std::string_view(field["a"])));
        dec_float free((std::string_view(field["f"])));
        balance[ticker] = free;
    }
}

void Core::orderbooks_handler(std::string_view message)
{
    BOOST_LOG_SEV(orderbooks_logger, logging::trivial::info) << message;

    // Инициализация итератора
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message);
    simdjson::ondemand::document doc = parser.iterate(json);
    simdjson::ondemand::object obj = doc.get_object();

    // Извлечение нужных полей
    std::string exchange((std::string_view(obj["exchange"])));
    std::string ticker((std::string_view(obj["s"])));
    dec_float best_ask((std::string_view(obj["a"])));
    dec_float best_bid((std::string_view(obj["b"])));

    // Обновление сохранённого ордербука
    orderbooks[exchange][ticker] = std::make_pair(best_ask, best_bid);

    // Проверка условий для создания и отмены ордеров
    process_orders();
}

void Core::process_orders()
{
    // Получение среднего арифметического ордербуков BTC-USDT
    std::pair<dec_float, dec_float> avg = avg_orderbooks("BTC-USDT");
    dec_float avg_ask = avg.first;
    dec_float avg_bid = avg.second;

    // Расчёт возможных ордеров
    dec_float sell_price = avg_ask * SELL_COEFFICIENT;
    dec_float buy_price = avg_bid * BUY_COEFFICIENT;
    dec_float sell_quantity = balance["BTC"];
    dec_float buy_quantity = balance["USDT"] / sell_price;
    dec_float min_ask = avg_ask * 0.9995;
    dec_float max_ask = avg_ask * 1.0005;
    dec_float min_bid = avg_bid * 0.9995;
    dec_float max_bid = avg_bid * 1.0005;

    // Если нет ордера на покупку, но есть BTC — создать ордер на покупку
    if (!has_sell_order && balance["BTC"] > 0.0008)
    {
        create_order("SELL", sell_price, sell_quantity);
        ask_bounds = std::make_pair(min_ask, max_ask);
        has_sell_order = true;
    }

    // Если нет ордера на покупку, но есть USDT — создать ордер
    if (!has_buy_order && balance["USDT"] > 40)
    {
        create_order("BUY", buy_price, buy_quantity);
        bid_bounds = std::make_pair(min_bid, max_bid);
        has_buy_order = true;
    }

    // Если есть ордер на продажу, но усреднённое лучшее предложение за пределами удержания — отменить ордер
    if (has_sell_order && !(ask_bounds.first < avg_ask && avg_ask < ask_bounds.second))
    {
        cancel_order("SELL");
        has_sell_order = false;
    }

    // Если есть ордер на покупку, но усреднённое лучшее предложение за пределами удержания — отменить ордер
    if (has_buy_order && !(bid_bounds.first < avg_bid && avg_bid < bid_bounds.second))
    {
        cancel_order("BUY");
        has_buy_order = false;
    }
}

std::pair<dec_float, dec_float> Core::avg_orderbooks(const std::string& ticker)
{
    // Суммы лучших предложений
    dec_float sum_ask(0);
    dec_float sum_bid(0);
    for (auto const&[exchange, exchange_orderbooks]: orderbooks)
    {
        std::pair<dec_float, dec_float> orderbook = exchange_orderbooks.at(ticker);
        sum_ask += orderbook.first;
        sum_bid += orderbook.second;
    }

    // Количество лучших предложений
    dec_float size(orderbooks.size());

    // Среднее арифметическое лучших предложений
    dec_float avg_ask(sum_ask / size);
    dec_float avg_bid(sum_bid / size);

    return std::make_pair(avg_ask, avg_bid);
}

void Core::create_order(std::string_view side, const dec_float& price, const dec_float& quantity)
{
    // Формирование сообщения в формате JSON
    std::string message(boost::json::serialize(boost::json::value{
        { "a", "+" },
        { "S", "BTCUSDT" },
        { "s", side },
        { "t", "LIMIT" },
        { "p", price.str() },
        { "q", quantity.str() }
    }));

    BOOST_LOG_SEV(orders_logger, logging::trivial::info) << message;

    // Отправка сообщения в шлюз
    gateway_channel->offer(message);
}

void Core::cancel_order(std::string_view side)
{
    // Формирование сообщения в формате JSON
    std::string message(boost::json::serialize(boost::json::value{
        { "a", "-" },
        { "S", "BTCUSDT" },
        { "s", side },
    }));

    BOOST_LOG_SEV(orders_logger, logging::trivial::info) << message;

    // Отправка сообщения в шлюз
    gateway_channel->offer(message);
}
