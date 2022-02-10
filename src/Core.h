#ifndef TRADE_CORE_CORE_H
#define TRADE_CORE_CORE_H


#include <functional>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/log/trivial.hpp>
#include <simdjson.h>
#include "Subscriber.h"
#include "Publisher.h"
#include "config.h"
#include "logging.h"

using boost::multiprecision::cpp_dec_float_50;

/**
 * Торговое ядро
 *
 * @note Использует протокол Aeron, медиа-драйвер которого заранее должен быть запущен
 */
class Core : public std::enable_shared_from_this<Core>
{
    // Каналы Aeron
    std::shared_ptr<Subscriber> orderbooks_channel;
    std::shared_ptr<Subscriber> balances_channel;
    std::shared_ptr<Publisher> gateway_channel;
    std::shared_ptr<Publisher> metrics_channel;
    std::shared_ptr<Publisher> errors_channel;

    // Стратегия ожидания Aeron
    aeron::SleepingIdleStrategy idle_strategy;

    // Последние данные о балансе и ордербуках
    std::map<std::string, cpp_dec_float_50> balance;
    std::map<std::string, std::map<std::string, std::pair<cpp_dec_float_50, cpp_dec_float_50>>> orderbooks;

    // Коэффициенты для выставления ордеров
    cpp_dec_float_50 SELL_COEFFICIENT;
    cpp_dec_float_50 BUY_COEFFICIENT;

    // Последние границы удержания ордеров
    std::pair<cpp_dec_float_50, cpp_dec_float_50> ask_bounds;
    std::pair<cpp_dec_float_50, cpp_dec_float_50> bid_bounds;

    // Флаги наличия ордеров
    bool has_sell_order;
    bool has_buy_order;

    // Логгеры
    logger_t orderbooks_logger;
    logger_t balance_logger;
    logger_t orders_logger;

    /**
     * Функция обратного вызова для обработки баланса
     *
     * @param message Баланс в формате JSON
     */
    void balances_handler(std::string_view message);

    /**
     * Функция обратного вызова для обработки биржевых стаканов
     *
     * @param message Биржевой стакан в формате JSON
     */
    void orderbooks_handler(std::string_view message);

    /**
     * Проверить условия для создания и отмены ордеров
     */
    void process_orders();

    /**
     * Рассчитать среднее арифметическое лучших ордеров для тикера
     *
     * @param ticker Тикер
     * @return Пара, содержащая цену покупки и продажи соответственно
     */
    std::pair<cpp_dec_float_50, cpp_dec_float_50> avg_orderbooks(const std::string& ticker);

    /**
     * Создать ордер
     *
     * @param side Тип ордера
     * @param price Цена
     * @param quantity Объём
     */
    void create_order(std::string_view side, const cpp_dec_float_50& price, const cpp_dec_float_50& quantity);

    /**
     * Отменить ордер
     *
     * @param side Тип ордера
     */
    void cancel_order(std::string_view side);

public:
    /**
     * Создать экземпляр торгового ядра и подключиться к каналам Aeron
     *
     * @param config_file_path Путь к файлу конфигурации в формате TOML
     */
    explicit Core(std::string_view config_file_path);

    /**
     * Проверить каналы Aeron на наличие новых сообщений
     */
    void poll();
};


#endif  // TRADE_CORE_CORE_H
