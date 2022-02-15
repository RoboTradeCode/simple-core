#include <boost/json/src.hpp>
#include "Core.h"

/**
 * Создать экземпляр торгового ядра и подключиться к каналам Aeron
 *
 * @param config_file_path Путь к файлу конфигурации в формате TOML
 */
Core::Core(std::string_view config_file_path)
    : idle_strategy(std::chrono::milliseconds(DEFAULT_IDLE_STRATEGY_SLEEP_MS)),
      has_sell_order(false),
      has_buy_order(false),
      orderbooks_logger(spdlog::get("orderbooks")),
      balance_logger(spdlog::get("balance")),
      orders_logger(spdlog::get("orders")),
      errors_logger(spdlog::get("errors"))
{
    // Получение конфигурации
    core_config config = parse_config(config_file_path);

    // Сокращения для удобства доступа
    auto exchange = config.exchange;
    auto aeron = config.aeron;
    auto subscribers = aeron.subscribers;
    auto publishers = aeron.publishers;
    auto gateway = publishers.gateway;
    auto metrics = publishers.metrics;
    auto errors = publishers.errors;

    // Инициализация каналов Aeron
    orderbooks_channel = std::make_shared<Subscriber>(
        [&](std::string_view message)
        { shared_from_this()->orderbooks_handler(message); },
        subscribers.orderbooks.channel,
        subscribers.orderbooks.stream_id
    );
    balance_channel = std::make_shared<Subscriber>(
        [&](std::string_view message)
        { shared_from_this()->balance_handler(message); },
        subscribers.balance.channel,
        subscribers.balance.stream_id
    );
    gateway_channel = std::make_shared<Publisher>(gateway.channel, gateway.stream_id, gateway.buffer_size);
    metrics_channel = std::make_shared<Publisher>(metrics.channel, metrics.stream_id, metrics.buffer_size);
    errors_channel = std::make_shared<Publisher>(errors.channel, errors.stream_id, errors.buffer_size);

    // Подписка на Publisher'ов
    for (const std::string& channel: subscribers.orderbooks.destinations)
        orderbooks_channel->add_destination(channel);
    for (const std::string& channel: subscribers.balance.destinations)
        balance_channel->add_destination(channel);

    // Инициализация стратегии ожидания
    idle_strategy = aeron::SleepingIdleStrategy(std::chrono::milliseconds(subscribers.idle_strategy_sleep_ms));

    // Инициализация пороговых значений для инструментов
    BTC_THRESHOLD = dec_float(exchange.btc_threshold);
    USDT_THRESHOLD = dec_float(exchange.usdt_threshold);

    // Инициализация коэффициентов для вычисления цены ордеров
    SELL_RATIO = dec_float(exchange.sell_ratio);
    BUY_RATIO = dec_float(exchange.buy_ratio);

    // Инициализация коэффициентов для вычисления границ удержания ордеров
    LOWER_BOUND_RATIO = dec_float(exchange.lower_bound_ratio);
    UPPER_BOUND_RATIO = dec_float(exchange.upper_bound_ratio);
}

/**
 * Проверить каналы Aeron на наличие новых сообщений
 */
void Core::poll()
{
    // Опрос каналов
    int fragments_read_orderbooks = orderbooks_channel->poll();
    int fragments_read_balance = balance_channel->poll();

    // Выполнение стратегии ожидания
    int fragments_read = fragments_read_orderbooks + fragments_read_balance;
    idle_strategy.idle(fragments_read);
}

/**
 * Функция обратного вызова для обработки баланса
 *
 * @param message Баланс в формате JSON
 */
void Core::balance_handler(std::string_view message)
{
    balance_logger->info(message);

    try
    {
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
    catch (simdjson::simdjson_error& e)
    {
        sentry_value_t exc = sentry_value_new_exception("simdjson::simdjson_error", e.what());
        sentry_value_t event = sentry_value_new_event();
        sentry_event_add_exception(event, exc);
        sentry_capture_event(event);

        errors_logger->error(e.what());
        errors_channel->offer(e.what());
    }
}

/**
 * Функция обратного вызова для обработки биржевых стаканов
 *
 * @param message Биржевой стакан в формате JSON
 */
void Core::orderbooks_handler(std::string_view message)
{
    orderbooks_logger->info(message);

    try
    {
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
    catch (simdjson::simdjson_error& e)
    {
        sentry_value_t exc = sentry_value_new_exception("simdjson::simdjson_error", e.what());
        sentry_value_t event = sentry_value_new_event();
        sentry_event_add_exception(event, exc);
        sentry_capture_event(event);

        errors_logger->error(e.what());
        errors_channel->offer(e.what());
    }
}

/**
 * Проверить условия для создания и отмены ордеров
 */
void Core::process_orders()
{
    // Получение среднего арифметического ордербуков BTC-USDT
    std::pair<dec_float, dec_float> avg = avg_orderbooks("BTC-USDT");
    dec_float avg_ask = avg.first;
    dec_float avg_bid = avg.second;

    // Расчёт возможных ордеров
    dec_float sell_price = avg_ask * SELL_RATIO;
    dec_float buy_price = avg_bid * BUY_RATIO;
    dec_float sell_quantity = balance["BTC"];
    dec_float buy_quantity = balance["USDT"] / sell_price;
    dec_float min_ask = avg_ask * LOWER_BOUND_RATIO;
    dec_float max_ask = avg_ask * UPPER_BOUND_RATIO;
    dec_float min_bid = avg_bid * LOWER_BOUND_RATIO;
    dec_float max_bid = avg_bid * UPPER_BOUND_RATIO;

    // Если нет ордера на продажу, но есть BTC — создать ордер на продажу
    if (!has_sell_order && balance["BTC"] > BTC_THRESHOLD)
    {
        create_order("SELL", sell_price, sell_quantity);
        sell_bounds = std::make_pair(min_ask, max_ask);
        has_sell_order = true;
    }

    // Если нет ордера на покупку, но есть USDT — создать ордер на покупку
    if (!has_buy_order && balance["USDT"] > USDT_THRESHOLD)
    {
        create_order("BUY", buy_price, buy_quantity);
        buy_bounds = std::make_pair(min_bid, max_bid);
        has_buy_order = true;
    }

    // Если есть ордер на продажу, но усреднённое лучшее предложение за пределами удержания — отменить ордер
    if (has_sell_order && !(sell_bounds.first < avg_ask && avg_ask < sell_bounds.second))
    {
        cancel_order("SELL");
        has_sell_order = false;
    }

    // Если есть ордер на покупку, но усреднённое лучшее предложение за пределами удержания — отменить ордер
    if (has_buy_order && !(buy_bounds.first < avg_bid && avg_bid < buy_bounds.second))
    {
        cancel_order("BUY");
        has_buy_order = false;
    }
}

/**
 * Рассчитать среднее арифметическое лучших ордеров для тикера
 *
 * @param ticker Тикер
 * @return Пара, содержащая цену покупки и продажи соответственно
 */
std::pair<dec_float, dec_float> Core::avg_orderbooks(const std::string& ticker)
{
    // Суммы лучших предложений
    dec_float sum_ask(0);
    dec_float sum_bid(0);
    for (auto const&[exchange, exchange_orderbooks]: orderbooks)
    {
        auto[ask, bid] = exchange_orderbooks.at(ticker);
        sum_ask += ask;
        sum_bid += bid;
    }

    // Количество лучших предложений
    dec_float size(orderbooks.size());

    // Среднее арифметическое лучших предложений
    dec_float avg_ask(sum_ask / size);
    dec_float avg_bid(sum_bid / size);

    return std::make_pair(avg_ask, avg_bid);
}

/**
 * Создать ордер
 *
 * @param side Тип ордера
 * @param price Цена
 * @param quantity Объём
 */
void Core::create_order(std::string_view side, const dec_float& price, const dec_float& quantity)
{
    // Формирование сообщения в формате JSON
    std::string message(boost::json::serialize(boost::json::value{
        { "a", "+" },
        { "S", "BTC-USDT" },
        { "s", side },
        { "t", "LIMIT" },
        { "p", price.str() },
        { "q", quantity.str() }
    }));

    orders_logger->info(message);
    gateway_channel->offer(message);
    metrics_channel->offer(message);
}

/**
 * Отменить ордер
 *
 * @param side Тип ордера
 */
void Core::cancel_order(std::string_view side)
{
    // Формирование сообщения в формате JSON
    std::string message(boost::json::serialize(boost::json::value{
        { "a", "-" },
        { "S", "BTC-USDT" },
        { "s", side },
    }));

    orders_logger->info(message);
    gateway_channel->offer(message);
    metrics_channel->offer(message);
}
