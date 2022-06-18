#include <boost/json/src.hpp>
#include "core.hpp"

/**
 * Создает экземпляр торгового ядра
 *
 * @param config_file_path Путь к файлу конфигурации в формате TOML
 */
core::core(std::string config_file_path_)
    : idle_strategy(std::chrono::milliseconds(DEFAULT_IDLE_STRATEGY_SLEEP_MS)),
      _logs_logger(spdlog::get("logs")),
      _balances_logger(spdlog::get("balances")),
      _orders_logger(spdlog::get("orders")),
      _errors_logger(spdlog::get("errors")),
      _general_logger(spdlog::get("general")) {

//    _orders_for_sell["UGD/TRE"] = std::make_pair(0, false);
//    if (_orders_for_sell["UGD/TRE"].second == false )
//    {
//        auto&[id_sell_order, has_sell_orders] = _orders_for_sell["UGD/TRE"];
//                has_sell_orders = true;
//        std::cout << "hz" << std::endl;
//    }

    _general_logger->info("Core starting...");
    // Получение дефолтной конфигурации
    _default_config = parse_config(config_file_path_);
    _snap_time  = std::chrono::system_clock::now();
    _delay_time = std::chrono::system_clock::now();
}
//---------------------------------------------------------------
// подготавливает к запуску
//---------------------------------------------------------------
bool core::prepatation_for_launch(bss::error& error_) {
    // Сокращения для удобства доступа
    auto aeron          = _work_config.aeron;
    auto subscribers    = aeron.subscribers;
    auto publishers     = aeron.publishers;
    auto gateway        = publishers.gateway;
    auto log            = publishers.logs;

    // Инициализация каналов Aeron
    try {
        _orderbooks_channel = std::make_shared<Subscriber>(
            [&](std::string_view message_)
            { shared_from_this()->orderbooks_handler(message_); },
            subscribers.orderbooks.channel,
            subscribers.orderbooks.stream_id
        );
    } catch (const std::exception& err) {
        error_.describe(fmt::format("Канал для приема ордербуков не создан: {}", err.what()));
        _general_logger->error(error_.to_string());
        return false;
    }

    try {
        _balance_channel = std::make_shared<Subscriber>(
            [&](std::string_view message_)
            { shared_from_this()->balance_handler(message_); },
            subscribers.balances.channel,
            subscribers.balances.stream_id
        );
    } catch (const std::exception& err) {
        error_.describe(fmt::format("Канал для приема баланса не создан: {}", err.what()));
        _general_logger->error(error_.to_string());
        return false;
    }
    try {
        _order_status_channel = std::make_shared<Subscriber>(
            [&](std::string_view message_)
            { shared_from_this()->order_status_handler(message_); },
            subscribers.order_statuses.channel,
            subscribers.order_statuses.stream_id
        );
    } catch (const std::exception& err) {
        error_.describe(fmt::format("Канал для приема статуса ордеров не создан: {}", err.what()));
        _general_logger->error(error_.to_string());
        return false;
    }
    try {
        _gateway_channel = std::make_shared<Publisher>(gateway.channel, gateway.stream_id);
    } catch (const std::exception& err) {
        error_.describe(fmt::format("Канал для отправки команд не создан: {}", err.what()));
        _general_logger->error(error_.to_string());
        return false;
    }
    try {
        _log_channel = std::make_shared<Publisher>(log.channel, log.stream_id);
    } catch (const std::exception& err) {
        error_.describe(fmt::format("Канал для отправки информации на лог сервер не создан: {}", err.what()));
        _general_logger->error(error_.to_string());
        return false;
    }

    //metrics_channel = std::make_shared<Publisher>(metrics.channel, metrics.stream_id, metrics.buffer_size);
    //errors_channel = std::make_shared<Publisher>(errors.channel, errors.stream_id, errors.buffer_size);

    // отменяем все ордера
    send_cancel_all_orders_request();
    // запрашиваем баланс
    send_get_balances_request();
    // Подписка на Publisher'ов
    for (const std::string& channel: subscribers.orderbooks.destinations)
        _orderbooks_channel->add_destination(channel);
    for (const std::string& channel: subscribers.balances.destinations)
        _balance_channel->add_destination(channel);

    // Инициализация стратегии ожидания
    idle_strategy = aeron::SleepingIdleStrategy(std::chrono::milliseconds(subscribers.idle_strategy_sleep_ms));

    // Инициализация коэффициентов для вычисления цены ордеров
    _SELL_RATIO = dec_float(_work_config.sell_ratio);
    _BUY_RATIO  = dec_float(_work_config.buy_ratio);

    // Инициализация коэффициентов для вычисления границ удержания ордеров
    _LOWER_BOUND_RATIO = dec_float(_work_config.lower_bound_ratio);
    _UPPER_BOUND_RATIO = dec_float(_work_config.upper_bound_ratio);

    return true;
}
//---------------------------------------------------------------
// загружает конфиг из файла
//---------------------------------------------------------------
bool core::load_config_from_file(bss::error& error_) {
    // создаем парсер
    simdjson::dom::parser parser;
    // скажем парсеру, чтобы он подготовил буфер для своих внутренних нужд (если будет меньше 0x08, то будет ошибка)
    auto &&error_code = parser.allocate(0x1000, 0x12);
    // если буфер успешно выделен
    if (simdjson::SUCCESS == error_code) {
        auto load_result = simdjson::padded_string::load("config.json");
        // если файл загрузили
        if (simdjson::SUCCESS == load_result.error()) {
            auto result = parser.parse(load_result.value().data(), load_result.value().size(), false);
            if (simdjson::SUCCESS == result.error()) {
                // получим биржу
                if (auto exchange_element{result["exchange"].get_string()}; simdjson::SUCCESS == exchange_element.error()) {
                    _work_config.exchange.name = exchange_element.value();
                } else {}
                // получим instance
                if (auto instance_element{result["instance"].get_string()}; simdjson::SUCCESS == instance_element.error()) {
                    _work_config.exchange.instance = instance_element.value();
                } else {}
                // получим название алгоритма
                if (auto algo_element{result["algo"].get_string()}; simdjson::SUCCESS == algo_element.error()) {
                    _work_config.exchange.algo = algo_element.value();
                } else {}

                // получим рынки, с которыми предстоит работать
                if (auto markets_array{result["data"]["markets"].get_array()}; simdjson::SUCCESS == markets_array.error()){
                    for (auto market : markets_array) {
                        std::string common_symbol;
                        std::string base_asset;
                        std::string quote_asset;
                        double      limits;
                        double      price_increment;
                        double      amount_increment;
                        if (auto common_symbol_element{market["common_symbol"].get_string()}; simdjson::SUCCESS == common_symbol_element.error()) {
                            //_work_config._markets.push_back(common_symbol_element.value());
                            common_symbol = common_symbol_element.value();
                        } else {}
                        if (auto base_asset_element{market["base_asset"].get_string()}; simdjson::SUCCESS == base_asset_element.error()) {
                            base_asset = base_asset_element.value();
                        } else {}
                        if (auto quote_asset_element{market["quote_asset"].get_string()}; simdjson::SUCCESS == quote_asset_element.error()) {
                            quote_asset = quote_asset_element.value();
                        }
                        if (auto price_increment_element{market["price_increment"].get_double()}; simdjson::SUCCESS == price_increment_element.error()) {
                            price_increment = price_increment_element.value();
                        }
                        if (auto amount_increment_element{market["amount_increment"].get_double()}; simdjson::SUCCESS == amount_increment_element.error()) {
                            amount_increment = amount_increment_element.value();
                        }
                        // создаем словарь ордеров на продажу для текущего рынка
                        _orders_for_sell[common_symbol] = std::make_pair(0, false);
                        // создаем словарь ордеров на покупку для текущего рынка
                        _orders_for_buy[common_symbol] = std::make_pair(0, false);
                        _markets[common_symbol] = std::make_tuple(std::make_pair(base_asset, quote_asset),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  price_increment, amount_increment);
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден объект \"maktets\".");
                }
                _work_config._assets.clear();
                // получим ассеты, с которыми предстоит работать
                if (auto assets_array{result["data"]["assets_labels"].get_array()}; simdjson::SUCCESS == assets_array.error()) {
                    for (auto asset : assets_array) {
                        //_assets_from_config.push_back(std::string(asset.get_string().value()));
                        if (auto common_element{asset["common"].get_c_str()}; simdjson::SUCCESS == common_element.error()) {
                            _work_config._assets.push_back(common_element.value());
                        } else {
                            error_.describe("При загрузке конфигурации в теле json не найден объект [\"assets_labels\"][\"common\"].");
                        }
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден объект \"assets_labels\".");
                }
                // получим часть пути для сокращения полного пути до элементов
                if (auto cfg = result["data"]["configs"]["core_config"]; simdjson::SUCCESS == cfg.error()) {
                    // минимальные пороговые значения
                    if (auto min_deal_amounts_objects{cfg["simple_core"]["min_deal_amounts"].get_object()}; simdjson::SUCCESS == min_deal_amounts_objects.error()) {
                        std::string_view asset_value;
                        double min_deal;
                        for (auto asset : min_deal_amounts_objects) {
                            std::cout << asset.key << " " << asset.value << std::endl;
                            asset_value = asset.key;
                            //min_deal = asset.value.get_double();
                            _min_deal_amounts[std::string(asset_value)] = asset.value.get_double();
                        }

                    }
                    // коэффициенты
                    if (auto sell_ratio_element{cfg["simple_core"]["ratio"]["sell"].get_double()}; simdjson::SUCCESS == sell_ratio_element.error()) {
                        _work_config.sell_ratio = sell_ratio_element.value();
                    } else {
                        _work_config.sell_ratio = 1.0015;
                    }
                    if (auto buy_ratio_element{cfg["simple_core"]["ratio"]["buy"].get_double()}; simdjson::SUCCESS == buy_ratio_element.error()) {
                        _work_config.buy_ratio = buy_ratio_element.value();
                    } else {
                        _work_config.buy_ratio = 0.9985;
                    }
                    if (auto lower_bound_ratio_element{cfg["simple_core"]["bound_ratio"]["lower"].get_double()}; simdjson::SUCCESS == lower_bound_ratio_element.error()) {
                        _work_config.lower_bound_ratio = lower_bound_ratio_element.value();
                    } else {
                        _work_config.lower_bound_ratio = 0.9995;
                    }
                    if (auto upper_bound_ratio_element{cfg["simple_core"]["bound_ratio"]["upper"].get_double()}; simdjson::SUCCESS == upper_bound_ratio_element.error()) {
                        _work_config.upper_bound_ratio = upper_bound_ratio_element.value();
                    } else {
                        _work_config.upper_bound_ratio = 1.0005;
                    }
                    // получаем значения сброса для зависших ордеров
                    if (auto first_reset_element{cfg["simple_core"]["reset_client_id"]["first"].get_int64()}; simdjson::SUCCESS == first_reset_element.error()) {
                        _work_config.reset_first_time = first_reset_element.value();
                    } else {
                        _work_config.reset_first_time = 10;
                    }
                    if (auto second_reset_element{cfg["simple_core"]["reset_clietnt_id"]["second"].get_int64()}; simdjson::SUCCESS == second_reset_element.error()) {
                        _work_config.reset_second_time = second_reset_element.value();
                    } else {
                        _work_config.reset_second_time = 30;
                    }
                    // получаем данные для канала команд в гейт
                    if (auto gate_channel_element{cfg["aeron"]["publishers"]["gate"]["channel"].get_string()}; simdjson::SUCCESS == gate_channel_element.error()) {
                         _work_config.aeron.publishers.gateway.channel = gate_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][channel]\".");
                    }
                    if (auto gate_stream_element{cfg["aeron"]["publishers"]["gate"]["stream_id"].get_int64()}; simdjson::SUCCESS == gate_stream_element.error()) {
                        _work_config.aeron.publishers.gateway.stream_id = gate_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][stream_id]\".");
                    }
                    // получаем данные для канала логов
                    if (auto logs_channel_element{cfg["aeron"]["publishers"]["logs"]["channel"].get_string()}; simdjson::SUCCESS == logs_channel_element.error()) {
                        _work_config.aeron.publishers.logs.channel = logs_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][logs][channel]\".");
                    }
                    if (auto logs_stream_element{cfg["aeron"]["publishers"]["logs"]["stream_id"].get_int64()}; simdjson::SUCCESS == logs_stream_element.error()) {
                        _work_config.aeron.publishers.logs.stream_id = logs_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][logs][stream_id]\".");
                    }
                    // получаем данные для канала ордербуков
                    if (auto orderbook_channel_element{cfg["aeron"]["subscribers"]["orderbooks"]["channel"].get_string()}; simdjson::SUCCESS == orderbook_channel_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.channel = orderbook_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][channel]\".");
                    }
                    if (auto orderbooks_stream_id_element{cfg["aeron"]["subscribers"]["orderbooks"]["stream_id"].get_int64()}; simdjson::SUCCESS == orderbooks_stream_id_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.stream_id = orderbooks_stream_id_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][stream_id]\".");
                    }
                    if (auto orderbooks_destination_element{cfg["aeron"]["subscribers"]["orderbooks"]["destinations"].get_array()}; simdjson::SUCCESS == orderbooks_destination_element.error()) {
                        for (auto destination : orderbooks_destination_element) {
                            _work_config.aeron.subscribers.orderbooks.destinations.emplace_back(destination);
                        }
                    } else {}
                    // получаем данные для канала балансов
                    if (auto balances_channel_element{cfg["aeron"]["subscribers"]["balance"]["channel"].get_string()}; simdjson::SUCCESS == balances_channel_element.error()) {
                        _work_config.aeron.subscribers.balances.channel = balances_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][channel]\".");
                    }
                    if (auto balances_stream_element{cfg["aeron"]["subscribers"]["balance"]["stream_id"].get_int64()}; simdjson::SUCCESS == balances_stream_element.error()) {
                        _work_config.aeron.subscribers.balances.stream_id = balances_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][stream_id]\".");
                    }
                    if (auto balances_destination_element{cfg["aeron"]["subscribers"]["balance"]["destinations"].get_array()}; simdjson::SUCCESS == balances_destination_element.error()) {
                        for (auto destination : balances_destination_element) {
                            _work_config.aeron.subscribers.balances.destinations.emplace_back(destination);
                        }
                    }
                    // получаем данные для канала логов
        //            if (auto log_channel_element{cfg["aeron"]["publishers"]["logs"]["channel"].get_string()}; simdjson::SUCCESS == log_channel_element.error()) {
        //                _work_config.aeron_core.publishers.logs.channel = log_channel_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log channel\".");
        //            }
        //            if (auto log_stream_element{cfg["aeron"]["publishers"]["logs"]["stream_id"].get_int64()}; simdjson::SUCCESS == log_stream_element.error()) {
        //                _work_config.aeron_core.publishers.logs.stream_id = log_stream_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log stream_id\".");
        //            }
                    // получаем данные для канала статуса ордеров
                    if (auto order_statuses_channel_element{cfg["aeron"]["subscribers"]["orders_statuses"]["channel"].get_string()}; simdjson::SUCCESS == order_statuses_channel_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.channel = order_statuses_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][channel]\".");
                    }
                    if (auto order_statuses_stream_element{cfg["aeron"]["subscribers"]["orders_statuses"]["stream_id"].get_int64()}; simdjson::SUCCESS == order_statuses_stream_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.stream_id = order_statuses_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][stream_id]\".");
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден оъект [\"data\"][\"core_config\"].");
                }
            } else {
                error_.describe(fmt::format("Ошибка разбора json фрейма в процессе парсинга конфига. Код ошибки: {}.", result.error()));
            }
        } else {
            error_.describe("Ошибка загрузки файла конфигурации.");
        }
    } else {
        error_.describe("Ошибка инициализации парсера simdjson (внутренний буфер не выделился).");
    }
    if (not error_) {
        // если не было ошибок, то считаем, что конфиг получен
        _config_was_received = true;
        return true;
    }
    else
        return false;
}
//---------------------------------------------------------------
// загружает конфиг из json
//---------------------------------------------------------------
bool core::load_config_from_json(const std::string& message_, bss::error &error_) noexcept{
    // создаем парсер
    simdjson::dom::parser parser;
    // скажем парсеру, чтобы он подготовил буфер для своих внутренних нужд (если будет меньше 0x08, то будет ошибка)
    auto &&error_code = parser.allocate(0x1000, 0x12);
    // если буфер успешно выделен
    if (simdjson::SUCCESS == error_code) {
        //auto load_result = simdjson::padded_string::load("config.json");
        // если файл загрузили
        //if (simdjson::SUCCESS == load_result.error()) {
            auto result = parser.parse(message_.c_str(), message_.size(), false);
            if (simdjson::SUCCESS == result.error()) {
                // получим биржу
                if (auto exchange_element{result["exchange"].get_string()}; simdjson::SUCCESS == exchange_element.error()) {
                    _work_config.exchange.name = exchange_element.value();
                } else {}
                // получим instance
                if (auto instance_element{result["instance"].get_string()}; simdjson::SUCCESS == instance_element.error()) {
                    _work_config.exchange.instance = instance_element.value();
                } else {}
                // получим название алгоритма
                if (auto algo_element{result["algo"].get_string()}; simdjson::SUCCESS == algo_element.error()) {
                    _work_config.exchange.algo = algo_element.value();
                } else {}

                // получим рынки, с которыми предстоит работать
                if (auto markets_array{result["data"]["markets"].get_array()}; simdjson::SUCCESS == markets_array.error()){
                    for (auto market : markets_array) {
                        std::string common_symbol;
                        std::string base_asset;
                        std::string quote_asset;
                        double      limits;
                        double      price_increment;
                        double      amount_increment;
                        if (auto common_symbol_element{market["common_symbol"].get_string()}; simdjson::SUCCESS == common_symbol_element.error()) {
                            //_work_config._markets.push_back(common_symbol_element.value());
                            common_symbol = common_symbol_element.value();
                        } else {}
                        if (auto base_asset_element{market["base_asset"].get_string()}; simdjson::SUCCESS == base_asset_element.error()) {
                            base_asset = base_asset_element.value();
                        } else {}
                        if (auto quote_asset_element{market["quote_asset"].get_string()}; simdjson::SUCCESS == quote_asset_element.error()) {
                            quote_asset = quote_asset_element.value();
                        }
                        if (auto price_increment_element{market["price_increment"].get_double()}; simdjson::SUCCESS == price_increment_element.error()) {
                            price_increment = price_increment_element.value();
                        }
                        if (auto amount_increment_element{market["amount_increment"].get_double()}; simdjson::SUCCESS == amount_increment_element.error()) {
                            amount_increment = amount_increment_element.value();
                        }
                        // создаем словарь ордеров на продажу для текущего рынка
                        _orders_for_sell[common_symbol] = std::make_pair(0, false);
                        // создаем словарь ордеров на покурку для текущего рынка
                        _orders_for_buy[common_symbol] = std::make_pair(0, false);
                        _markets[common_symbol] = std::make_tuple(std::make_pair(base_asset, quote_asset),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  std::make_pair(dec_float(0), dec_float(0)),
                                                                  price_increment, amount_increment);
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден объект \"maktets\".");
                }
                _work_config._assets.clear();
                // получим ассеты, с которыми предстоит работать
                if (auto assets_array{result["data"]["assets_labels"].get_array()}; simdjson::SUCCESS == assets_array.error()) {
                    for (auto asset : assets_array) {
                        //_assets_from_config.push_back(std::string(asset.get_string().value()));
                        if (auto common_element{asset["common"].get_c_str()}; simdjson::SUCCESS == common_element.error()) {
                            _work_config._assets.push_back(common_element.value());
                        } else {
                            error_.describe("При загрузке конфигурации в теле json не найден объект [\"assets_labels\"][\"common\"].");
                        }
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден объект \"assets_labels\".");
                }

                // получим часть пути для сокращения полного пути до элементов
                if (auto cfg = result["data"]["configs"]["core_config"]; simdjson::SUCCESS == cfg.error()) {
                    // минимальные пороговые значения
                    if (auto min_deal_amounts_objects{cfg["simple_core"]["min_deal_amounts"].get_object()}; simdjson::SUCCESS == min_deal_amounts_objects.error()) {
                        std::string_view asset_value;
                        double min_deal;
                        for (auto asset : min_deal_amounts_objects) {
                            std::cout << asset.key << " " << asset.value << std::endl;
                            asset_value = asset.key;
                            //min_deal = asset.value.get_double();
                            _min_deal_amounts[std::string(asset_value)] = asset.value.get_double();
                        }
                    }
                    // коэффициенты
                    if (auto sell_ratio_element{cfg["simple_core"]["ratio"]["sell"].get_double()}; simdjson::SUCCESS == sell_ratio_element.error()) {
                        _work_config.sell_ratio = sell_ratio_element.value();
                    } else {
                        _work_config.sell_ratio = 1.0015;
                    }
                    if (auto buy_ratio_element{cfg["simple_core"]["ratio"]["buy"].get_double()}; simdjson::SUCCESS == buy_ratio_element.error()) {
                        _work_config.buy_ratio = buy_ratio_element.value();
                    } else {
                        _work_config.buy_ratio = 0.9985;
                    }
                    if (auto lower_bound_ratio_element{cfg["simple_core"]["bound_ratio"]["lower"].get_double()}; simdjson::SUCCESS == lower_bound_ratio_element.error()) {
                        _work_config.lower_bound_ratio = lower_bound_ratio_element.value();
                    } else {
                        _work_config.lower_bound_ratio = 0.9995;
                    }
                    if (auto upper_bound_ratio_element{cfg["simple_core"]["bound_ratio"]["upper"].get_double()}; simdjson::SUCCESS == upper_bound_ratio_element.error()) {
                        _work_config.upper_bound_ratio = upper_bound_ratio_element.value();
                    } else {
                        _work_config.upper_bound_ratio = 1.0005;
                    }
                    // получаем значения сброса для зависших ордеров
                    if (auto first_reset_element{cfg["simple_core"]["reset_client_id"]["first"].get_int64()}; simdjson::SUCCESS == first_reset_element.error()) {
                        _work_config.reset_first_time = first_reset_element.value();
                    } else {
                        _work_config.reset_first_time = 10;
                    }
                    if (auto second_reset_element{cfg["simple_core"]["reset_clietnt_id"]["second"].get_int64()}; simdjson::SUCCESS == second_reset_element.error()) {
                        _work_config.reset_second_time = second_reset_element.value();
                    } else {
                        _work_config.reset_second_time = 30;
                    }
                    // получаем данные для канала команд в гейт
                    if (auto gate_channel_element{cfg["aeron"]["publishers"]["gate"]["channel"].get_string()}; simdjson::SUCCESS == gate_channel_element.error()) {
                         _work_config.aeron.publishers.gateway.channel = gate_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][channel]\".");
                    }
                    if (auto gate_stream_element{cfg["aeron"]["publishers"]["gate"]["stream_id"].get_int64()}; simdjson::SUCCESS == gate_stream_element.error()) {
                        _work_config.aeron.publishers.gateway.stream_id = gate_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][gate][stream_id]\".");
                    }
                    // получаем данные для канала логов
                    if (auto logs_channel_element{cfg["aeron"]["publishers"]["logs"]["channel"].get_string()}; simdjson::SUCCESS == logs_channel_element.error()) {
                        _work_config.aeron.publishers.logs.channel = logs_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][logs][channel]\".");
                    }
                    if (auto logs_stream_element{cfg["aeron"]["publishers"]["logs"]["stream_id"].get_int64()}; simdjson::SUCCESS == logs_stream_element.error()) {
                        _work_config.aeron.publishers.logs.stream_id = logs_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][publishers][logs][stream_id]\".");
                    }
                    // получаем данные для канала ордербуков
                    if (auto orderbook_channel_element{cfg["aeron"]["subscribers"]["orderbooks"]["channel"].get_string()}; simdjson::SUCCESS == orderbook_channel_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.channel = orderbook_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][channel]\".");
                    }
                    if (auto orderbooks_stream_id_element{cfg["aeron"]["subscribers"]["orderbooks"]["stream_id"].get_int64()}; simdjson::SUCCESS == orderbooks_stream_id_element.error()) {
                        _work_config.aeron.subscribers.orderbooks.stream_id = orderbooks_stream_id_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден объект \"[aeron][subscribers][orderbooks][stream_id]\".");
                    }
                    if (auto orderbooks_destination_element{cfg["aeron"]["subscribers"]["orderbooks"]["destinations"].get_array()}; simdjson::SUCCESS == orderbooks_destination_element.error()) {
                        for (auto destination : orderbooks_destination_element) {
                            _work_config.aeron.subscribers.orderbooks.destinations.emplace_back(destination);
                        }
                    } else {}
                    // получаем данные для канала балансов
                    if (auto balances_channel_element{cfg["aeron"]["subscribers"]["balance"]["channel"].get_string()}; simdjson::SUCCESS == balances_channel_element.error()) {
                        _work_config.aeron.subscribers.balances.channel = balances_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][channel]\".");
                    }
                    if (auto balances_stream_element{cfg["aeron"]["subscribers"]["balance"]["stream_id"].get_int64()}; simdjson::SUCCESS == balances_stream_element.error()) {
                        _work_config.aeron.subscribers.balances.stream_id = balances_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][balance][stream_id]\".");
                    }
                    if (auto balances_destination_element{cfg["aeron"]["subscribers"]["balance"]["destinations"].get_array()}; simdjson::SUCCESS == balances_destination_element.error()) {
                        for (auto destination : balances_destination_element) {
                            _work_config.aeron.subscribers.balances.destinations.emplace_back(destination);
                        }
                    }
                    // получаем данные для канала логов
        //            if (auto log_channel_element{cfg["aeron"]["publishers"]["logs"]["channel"].get_string()}; simdjson::SUCCESS == log_channel_element.error()) {
        //                _work_config.aeron_core.publishers.logs.channel = log_channel_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log channel\".");
        //            }
        //            if (auto log_stream_element{cfg["aeron"]["publishers"]["logs"]["stream_id"].get_int64()}; simdjson::SUCCESS == log_stream_element.error()) {
        //                _work_config.aeron_core.publishers.logs.stream_id = log_stream_element.value();
        //            } else {
        //                error_.describe("При загрузке конфигурации в теле json не найден оъект \"log stream_id\".");
        //            }
                    // получаем данные для канала статуса ордеров
                    if (auto order_statuses_channel_element{cfg["aeron"]["subscribers"]["orders_statuses"]["channel"].get_string()}; simdjson::SUCCESS == order_statuses_channel_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.channel = order_statuses_channel_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][channel]\".");
                    }
                    if (auto order_statuses_stream_element{cfg["aeron"]["subscribers"]["orders_statuses"]["stream_id"].get_int64()}; simdjson::SUCCESS == order_statuses_stream_element.error()) {
                        _work_config.aeron.subscribers.order_statuses.stream_id = order_statuses_stream_element.value();
                    } else {
                        error_.describe("При загрузке конфигурации из файла в теле json не найден оъект \"[aeron][subscribers][orders_statuses][stream_id]\".");
                    }
                } else {
                    error_.describe("При загрузке конфигурации в теле json не найден оъект [\"data\"][\"core_config\"].");
                }
            } else {
                error_.describe(fmt::format("Ошибка разбора json фрейма в процессе парсинга конфига. Код ошибки: {}.", result.error()));
            }

    } else {
        error_.describe("Ошибка инициализации парсера simdjson (внутренний буфер не выделился).");
    }
    if (not error_) {
        // если не было ошибок, то считаем, что конфиг получен
        _config_was_received = true;
        return true;
    }
    else
        return false;
}
//---------------------------------------------------------------
// отправляет запрос на отмену всех ордеров
//---------------------------------------------------------------
void core::send_cancel_all_orders_request() {

    JSON cancel_orders_request;
    cancel_orders_request["event"]       = "command";
    cancel_orders_request["exchange"]    = _work_config.exchange.name;
    cancel_orders_request["node"]        = "core";
    cancel_orders_request["instance"]    = _work_config.exchange.instance;
    cancel_orders_request["action"]      = "cancel_all_orders";
    cancel_orders_request["message"]     = nullptr;
    cancel_orders_request["algo"]        = _work_config.exchange.algo;
    cancel_orders_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    cancel_orders_request["data"]        = nullptr;

    // логируем информацию
   _general_logger->info(cancel_orders_request.dump());
    // отправляем команду
    int64_t result = _gateway_channel->offer(cancel_orders_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки запроса на отмену всех ордеров: ", "none", result);
        if (result == aeron::ADMIN_ACTION) {
            result = _gateway_channel->offer(cancel_orders_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки запроса на отмену всех ордеров: ", "none", result);
            }
        }
    } else {
        _general_logger->info("Отправлен запрос на отмену всех ордеров.");
    }
}
//---------------------------------------------------------------
// отправляет запрос на получение статуса ордера по клиентскому идентификатору
//---------------------------------------------------------------
void core::send_get_order_status_by_client_id_request(const std::string &client_id_) {
    JSON get_order_status_request;
    get_order_status_request["event"]       = "command";
    get_order_status_request["exchange"]    = _work_config.exchange.name;
    get_order_status_request["node"]        = "core";
    get_order_status_request["instance"]    = _work_config.exchange.instance;
    get_order_status_request["action"]      = "order_status";
    get_order_status_request["message"]     = nullptr;
    get_order_status_request["algo"]        = _work_config.exchange.algo;
    get_order_status_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    JSON data;
    JSON order;
    order["client_id"]     = client_id_;
    //order["symbol"] = symbol_;
    data.push_back(order);
    get_order_status_request["data"] = data;

    int64_t result = _gateway_channel->offer(get_order_status_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки запроса на получение статуса ордера: ", "none", result);
        if (result == aeron::ADMIN_ACTION) {
            result = _gateway_channel->offer(get_order_status_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки запроса получение статуса ордера: ", "none", result);
            }
        }
    } else {
        _general_logger->info("Отправлен запрос на получение статуса ордера.");
    }
}
//---------------------------------------------------------------
// отправляет запрос на получение статуса ордера
//---------------------------------------------------------------
void core::send_get_order_status_request(const std::string &order_id_) {
    JSON get_order_status_request;
    get_order_status_request["event"]       = "command";
    get_order_status_request["exchange"]    = _work_config.exchange.name;
    get_order_status_request["node"]        = "core";
    get_order_status_request["instance"]    = _work_config.exchange.instance;
    get_order_status_request["action"]      = "order_status";
    get_order_status_request["message"]     = nullptr;
    get_order_status_request["algo"]        = _work_config.exchange.algo;
    get_order_status_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    JSON data;
    JSON order;
    order["id"]     = order_id_;
    //order["symbol"] = symbol_;
    data.push_back(order);
    get_order_status_request["data"] = data;

    int64_t result = _gateway_channel->offer(get_order_status_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки запроса на получение статуса ордера: ", "none", result);
        if (result == aeron::ADMIN_ACTION) {
            result = _gateway_channel->offer(get_order_status_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки запроса получение статуса ордера: ", "none", result);
            }
        }
    } else {
        _general_logger->info("Отправлен запрос на получение статуса ордера.");
    }
}
//---------------------------------------------------------------
// отправляет запрос на получение балансов
//---------------------------------------------------------------
void core::send_get_balances_request() {

    JSON get_balances_request;
    get_balances_request["event"]       = "command";
    get_balances_request["exchange"]    = _work_config.exchange.name;
    get_balances_request["node"]        = "core";
    get_balances_request["instance"]    = _work_config.exchange.instance;
    get_balances_request["action"]      = "get_balances";
    get_balances_request["message"]     = nullptr;
    get_balances_request["algo"]        = _work_config.exchange.algo;
    get_balances_request["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    //get_balances_request["data"]        = nullptr;
    JSON data;
    JSON assets;
    for (auto& asset : _work_config._assets) {
        assets.push_back(asset);
    }
    //assets.push_back("BTC");
    //assets.push_back("ETH");
    //assets.push_back("USDT");
    data["assets"] = assets;
    get_balances_request["data"]  = data;

    int64_t result = _gateway_channel->offer(get_balances_request.dump());
    if (result < 0) {
        processing_error("Ошибка отправки запроса на получение балансов: ", "none", result);
        if (result == aeron::ADMIN_ACTION) {
            result = _gateway_channel->offer(get_balances_request.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки запроса на получение балансов: ", "none", result);
            }
        }
    } else {
        _general_logger->info("Отправлен запрос на получение балансов.");
    }
}
//---------------------------------------------------------------
//
//---------------------------------------------------------------
void core::processing_error(std::string_view error_source_, const std::string& prev_messsage_, const std::int64_t& error_code_) {

    _errors_logger->info("Предыдущее успешно отправленное сообщение: {}.", prev_messsage_);
    _errors_logger->error(error_source_);
    if (error_code_ == aeron::BACK_PRESSURED) {
        _errors_logger->error(BACK_PRESSURED_DESCRIPTION);
    } else if (error_code_ == aeron::NOT_CONNECTED) {
        _errors_logger->error(NOT_CONNECTED_DESCRIPTION);
    } else if (error_code_ == aeron::ADMIN_ACTION) {
        _errors_logger->error(ADMIN_ACTION_DESCRIPTION);
    } else if (error_code_ == aeron::PUBLICATION_CLOSED) {
        _errors_logger->error(PUBLICATION_CLOSED_DESCRIPTION);
    } else {
        _errors_logger->error(UNKNOWN_DESCRIPTION);
    }
}
/**
 * Проверить каналы Aeron на наличие новых сообщений
 */
void core::poll() {
    // Опрос каналов
    int fragments_read_orderbooks     = _orderbooks_channel->poll();
    int fragments_read_balance        = _balance_channel->poll();
    int fragments_read_order_statuses = _order_status_channel->poll();

    // Выполнение стратегии ожидания
    int fragments_read = fragments_read_orderbooks + fragments_read_balance + fragments_read_order_statuses;
    idle_strategy.idle(fragments_read);
}

/**
 * Функция обратного вызова для обработки баланса
 *
 * @param message Баланс в формате JSON
 */
void core::balance_handler(std::string_view message_) {
    //std::cout << message_ << std::endl;
    _balances_logger->info(message_);

    // Создадим парсер.
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message_);
    // Получим данные.
    auto parse_result = parser.iterate(json);
    if (simdjson::SUCCESS == parse_result.error()) {
        // проверяем значение события
        if (auto event_element{parse_result["event"].get_string()}; simdjson::SUCCESS == event_element.error()) {
            if (event_element.value() == "data") {
                // проверяем значения action
                if (auto action_element{parse_result["action"].get_string()}; simdjson::SUCCESS == action_element.error()) {
                    if (action_element.value() == "balances") {
                        /*auto obj = parse_result["data"].get_object();
                        for (auto field : obj) {
                            std::cout << field.key() << std::endl;
                        }*/
                        if (auto data_element_objects{parse_result["data"].get_object()}; simdjson::SUCCESS == data_element_objects.error()) {
                            std::string ticker;
                            double free;
                            std::string_view asset_value;
                            for (auto asset : data_element_objects) {
                                asset_value = asset.unescaped_key();
                                if (auto asset_free_element_array{parse_result["data"][asset_value]["free"].get_double()}; simdjson::SUCCESS == asset_free_element_array.error()) {
                                    free = asset_free_element_array.value();
                                } else {

                                }
                                _balance[std::string(asset_value)] = free;
                            }
                            if (_balance.size() != 0)
                                _has_balance = true;
                        }

                    } else {
                        _errors_logger->error("(balance_handler) Поле \"action\" содержит значение: {}", action_element.value());
                    }
                } else {
                    _errors_logger->error("(balance_handler) Поле \"action\" отсутствует");
                }
            } else {
                _errors_logger->error("(balance_handler) Поле \"event\" содержит значение: {}", event_element.value());
            }
        } else {
            _errors_logger->error("(balance_handler) Поле \"event\" отсутствует");
        }
    }
}
/**
 * Функция обратного вызова для обработки статуса ордеров
 *
 * @param message Статус ордера в формате json
 */
void core::order_status_handler(std::string_view message_) {

    //std::cout << "<<<--------<<<---------<<<"<< std::endl;
    _general_logger->info("<<<--------<<<---------<<<");
    _general_logger->info(message_);
    // Создадим парсер.
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message_);
    // Получим данные.
    auto parse_result = parser.iterate(json);
    if (simdjson::SUCCESS == parse_result.error()) {
        // проверяем значение события
        if (auto event_element{parse_result["event"].get_string()}; simdjson::SUCCESS == event_element.error()) {
            if (event_element.value() == "data") {
                std::string side;
                std::string symbol;
                int64_t     id;
                std::string client_id;
                // проверяем значения action
                if (auto action_element{parse_result["action"].get_string()}; simdjson::SUCCESS == action_element.error()) {
                    if (action_element.value() == "order_created") {
                        // был создан ордер
                        if (auto side_element{parse_result["data"]["side"].get_string()}; simdjson::SUCCESS == side_element.error()) {
                            side = side_element.value();
                        } else {}
                        if (auto symbol_element{parse_result["data"]["symbol"].get_string()}; simdjson::SUCCESS == symbol_element.error()) {
                            symbol = symbol_element.value();
                        } else {}
                        if (auto id_element{parse_result["data"]["id"].get_int64()}; simdjson::SUCCESS == id_element.error()) {
                            id = id_element.value();
                        } else {}
                        if (auto client_id_element{parse_result["data"]["client_id"].get_string()}; simdjson::SUCCESS == client_id_element.error()) {
                            client_id = client_id_element.value();
                        }  else {}
                        if (side == "sell") {
                            if (_orders_for_sell[symbol].second == false) {
                                _general_logger->error("По какой-то причине флаг наличия ордера на продажу установлен в false");
                            } else {}
                            _orders_for_sell[symbol].first = id;
                            _general_logger->info("запомнили в _sell_orders id: {}. Содержит {} элементов.", id, _orders_for_sell.size());
                        } else if (side == "buy") {
                            if (_orders_for_buy[symbol].second == false) {
                                _general_logger->error("По какой-то причине флаг наличия ордера на покупку установлен в false");
                            } else {}
                            _orders_for_buy[symbol].first = id;
                            _general_logger->info("запомнили в _buy_orders id: {}. Содержит {} элементов.", id, _orders_for_buy.size());
                        }
                        // независимо от side удаляем из словаря по client_id
                        auto find_client_id = _clients_id.find(client_id);
                        if (find_client_id != _clients_id.end()) {
                            _clients_id.erase(find_client_id);
                   //         std::cout << "удалили из _clients_id: " << client_id << std::endl;
                            _general_logger->info("удалили из _clients_id: {}", client_id);
                        }
                    } else if (action_element.value() == "order_cancel") {
                        // был отменен ордер
                        if (auto side_element{parse_result["data"]["side"].get_string()}; simdjson::SUCCESS == side_element.error()) {
                            side = side_element.value();
                        } else {}
                        if (auto symbol_element{parse_result["data"]["symbol"].get_string()}; simdjson::SUCCESS == symbol_element.error()) {
                            symbol = symbol_element.value();
                        } else {}
                        if (auto id_element{parse_result["data"]["id"].get_int64()}; simdjson::SUCCESS == id_element.error()) {
                            id = id_element.value();
                        } else {}
                        if (auto client_id_element{parse_result["data"]["client_id"].get_string()}; simdjson::SUCCESS == client_id_element.error()) {
                            client_id = client_id_element.value();
                        }  else {}
                        // найдем в словаре клиентский идентификатор
                        //auto find_client_id = _clients_id.find(client_id);
                        if (side == "sell") {
                            // если такой клиентский идентификатор найден (его может не быть, если cancel_order приходит
                            // после перезапуска и команды cancel_all_orders, а во время перезапуска были открытые ордера)
                            //if (find_client_id != _clients_id.end()) {
                                // сбрасываем идентфикатор ордера
                                _orders_for_sell[symbol].first = 0;
                                // если ордер выполнен, то флаг сброса надо опустить здесь
                                _orders_for_sell[symbol].second = false;
                                //std::cout << "обнулили в _sell_orders id: " << id << ". Содержит " << _orders_for_sell.size() << std::endl;
                                _general_logger->info("{} обнулили в _orders_for_sell id: {}. Содержит {}",
                                                      symbol, id, _orders_for_sell.size());
                            /*} else {
                                _general_logger->info("В json client_id = {}. В словаре такого не нашлось.", client_id);
                            }*/


                        } else if (side == "buy") {
                            // если такой клиентский идентификатор найден (его может не быть, если cancel_order приходит
                            // после перезапуска и команды cancel_all_orders, а во время перезапуска были открытые ордера)
                            //if (find_client_id != _clients_id.end()) {
                                // сбрасываем идентфикатор ордера
                                _orders_for_buy[symbol].first = 0;
                                // если ордер выполнен, то флаг сброса надо опустить здесь
                                _orders_for_buy[symbol].second = false;
                                //std::cout << "обнулили в _orders_for_buy id: " << id << ". Содержит " << _orders_for_buy.size() << std::endl;
                                _general_logger->info("{} обнулили в _orders_for_buy id: {}. Содержит {}",
                                                      symbol, id, _orders_for_buy.size() );
                            /*}
                            else {
                                _general_logger->info("В json client_id = {}. В словаре такого не нашлось.", client_id);
                            }*/
                        }
                        // независимо от side удаляем из словаря по client_id
                        /*auto find_client_id = _clients_id.find(client_id);
                        if (find_client_id != _clients_id.end()) {
                            _clients_id.erase(find_client_id);
                            //std::cout << "удалили из _clients_id: " << client_id << std::endl;
                            _general_logger->info("удалили из _clients_id: {}", client_id);
                        }*/
                        auto find_id = _cancel_id.find(id);
                        if (find_id != _cancel_id.end()) {
                            _cancel_id.erase(find_id);
                            _general_logger->info("удалили из _cancel_id: {}", client_id);
                        }
                    } else {
                        _errors_logger->error("(order_status_handler) Поле \"action\" содержит значение: {}", action_element.value());
                    }
                } else {
                   _errors_logger->error("(order_status_handler) Поле \"action\" отсутствует");
                }
            } else {
                _errors_logger->error("(order_status_handler) Поле \"event\" содержит значение: {}", event_element.value());
            }
        } else {
            _errors_logger->error("(order_status_handler) Поле \"event\" отсутствует");
        }
    }
}
/**
 * Функция обратного вызова для обработки биржевых стаканов
 *
 * @param message Биржевой стакан в формате JSON
 */
void core::orderbooks_handler(std::string_view message_) {
    //std::cout << "--------------------------------------" << std::endl;
    //std::cout << "orderbooks_handler " << message_ << std::endl;
    //std::cout << "--------------------------------------" << std::endl;

    // Создадим парсер.
    simdjson::ondemand::parser parser;
    simdjson::padded_string json(message_);
    // Получим данные.
    auto parse_result = parser.iterate(json);
    if (simdjson::SUCCESS == parse_result.error()) {
        // проверяем значение события
        if (auto event_element{parse_result["event"].get_string()}; simdjson::SUCCESS == event_element.error()) {
            if (event_element.value() == "data") {
                // проверяем значения action
                if (auto action_element{parse_result["action"].get_string()}; simdjson::SUCCESS == action_element.error()) {
                    if (action_element.value() == "orderbook") {
                        std::string exchange;
                        std::string ticker;
                        double best_ask;
                        double best_bid;
                        if (auto exchange_element{parse_result["exchange"].get_string()}; simdjson::SUCCESS == exchange_element.error()) {
                            exchange = exchange_element.value();
                        } else {}
                        if (auto data_element_array{parse_result["data"]["asks"].get_array()}; simdjson::SUCCESS == data_element_array.error()) {
                            for (auto data_element : data_element_array)  {
                                best_ask = data_element.get_array().at(0).get_double();
                            }
                        } else {
                            std::cout << "Ошибка получения массива поля [\"data\"][\"asks\"]: " << data_element_array << std::endl;
                        }
                        if (auto data_element_array{parse_result["data"]["bids"].get_array()}; simdjson::SUCCESS == data_element_array.error()) {
                            for (auto data_element : data_element_array)  {
                                best_bid = data_element.get_array().at(0).get_double();
                            }
                        } else {
                            std::cout << "Ошибка получения массива поля [\"data\"][\"bids\"]: " << data_element_array << std::endl;
                        }
                        if (auto symbol_element{parse_result["data"]["symbol"].get_string()}; simdjson::SUCCESS == symbol_element.error()) {
                            ticker = symbol_element.value();
                        } else {}
                        // Обновление сохранённого ордербука
                        _orderbooks[exchange][ticker] = std::make_pair(best_ask, best_bid);

                        // Проверка условий для создания и отмены ордеров (при запуске у нас может не быть данных о балансе,
                        // поэтому функцию нет смысла вызывать)
                        if (_has_balance)
                            process_orders();
                    }
                }
            }
        }
    }
}

/**
 * Проверить условия для создания и отмены ордеров
 */
void core::process_orders() {

    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - _delay_time).count() < 10) {
        // даем 10 секунд на отмену ранее выставленных ордеров
        return;
    }
    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - _snap_time).count() > 60) {
        _snap_time = std::chrono::system_clock::now();
        for (auto &[symbol, markets_tuple]:_markets) {

            auto&[id_sell_order, has_sell_orders] = _orders_for_sell[symbol];
            auto&[id_buy_order, has_buy_orders]   = _orders_for_buy[symbol];

            _general_logger->info("symbol = {}, id_sell_order = {}, has_sell_orders = {}", symbol, id_sell_order, has_sell_orders);
            _general_logger->info("symbol = {}, id_buy_order = {}, has_buy_orders = {}", symbol, id_buy_order, has_buy_orders);
        }
    }
    // проходим по всем рынкам, которые получили из конфига
    for (auto &[symbol, markets_tuple]:_markets) {
        // Получаем среднееарифметическое ордербуков по валютной паре (от всех подписанных бирж)
        std::pair<dec_float, dec_float> avg = avg_orderbooks(symbol);
        // запоминаем лучшие предложения
        dec_float avg_ask = avg.first;
        dec_float avg_bid = avg.second;

        // Расчитываем стоимость возможных ордеров
        dec_float sell_price    = avg_ask * _SELL_RATIO;
        dec_float buy_price     = avg_bid * _BUY_RATIO;

        // Расчитываем объем возможных ордеров исходя из баланса и стоимости
        dec_float sell_quantity = _balance[std::get<0>(markets_tuple).first];
        dec_float buy_quantity  = _balance[std::get<0>(markets_tuple).second] / sell_price;

        dec_float min_ask = avg_ask * _LOWER_BOUND_RATIO;
        dec_float max_ask = avg_ask * _UPPER_BOUND_RATIO;
        dec_float min_bid = avg_bid * _LOWER_BOUND_RATIO;
        dec_float max_bid = avg_bid * _UPPER_BOUND_RATIO;

        auto&[id_sell_order, has_sell_orders] = _orders_for_sell[symbol];
        auto&[id_buy_order, has_buy_orders]   = _orders_for_buy[symbol];

        // Вычислим висячие ордера (Когда приходит информация о статусе ордера, т.е. он обработан гейтом, то в
        // callback функции такой ордер удаляется из словаря. Если по какому-то ордеру ничего не было в callback
        // функции, то удаляем из словаря тут)
        for (auto pos = _clients_id.begin(); pos != _clients_id.end();) {
            // после 10 секунд посылаем запрос на статус (проверяем , что валютная пара из словаря соответсвует текущей и
            // флаг того что запрос мы не еще посылали и сразу выставляем поднимаем его)
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::get<2>(pos->second)).count() > _work_config.reset_first_time &&
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::get<2>(pos->second)).count() < _work_config.reset_second_time &&
                    symbol == std::get<0>(pos->second) &&
                    false   == std::get<3>(pos->second)) {

                send_get_order_status_by_client_id_request(pos->first);
                // выставим флаг отправки запроса статуса ордера
                std::get<3>(pos->second) = true;
                _general_logger->info("{} висит более {} секунд. Запрашиваем статус. {}.", pos->first, _work_config.reset_first_time, symbol);
                 ++pos;
            }
            // если время обработки ордера истекло и валютная пара из словаря соответсвует текущей,
            // то обнуляем сделку
            else if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::get<2>(pos->second)).count() >= _work_config.reset_second_time && symbol == std::get<0>(pos->second)) {
                if (std::get<1>(pos->second) == "sell") {
                    has_sell_orders = false;
                    _general_logger->info("{} висит {} секунд и будет сброшен (has_sell_orders = false). {} sell.", pos->first, _work_config.reset_second_time,symbol);
                } else if (std::get<1>(pos->second) == "buy") {
                    has_buy_orders = false;
                    _general_logger->info("{} висит {} секунд и будет сброшен (has_buy_orders = false). {} buy.", pos->first, _work_config.reset_second_time, symbol);
                }
                pos = _clients_id.erase(pos);
             } else {
                ++pos;
            }
        }
        // проверяем зависшие ордера на отмену
        for (auto pos = _cancel_id.begin(); pos != _cancel_id.end();) {
            // после 10 секунд посылаем запрос на статус (проверяем , что валютная пара из словаря соответсвует текущей и
            // флаг того что запрос мы не еще посылали и сразу выставляем поднимаем его)
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::get<2>(pos->second)).count() > _work_config.reset_first_time &&
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::get<2>(pos->second)).count() < _work_config.reset_second_time &&
                    symbol == std::get<0>(pos->second) &&
                    false   == std::get<3>(pos->second)) {

                send_get_order_status_request(std::to_string(pos->first));
                // выставим флаг отправки запроса статуса ордера
                std::get<3>(pos->second) = true;
                _general_logger->info("{} ордер на отмену висит более {} секунд. Запрашиваем статус. {}.", pos->first, _work_config.reset_first_time, symbol);
                 ++pos;
            }
            // если время обработки ордера истекло и валютная пара из словаря соответсвует текущей,
            // то обнуляем сделку
            else if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - std::get<2>(pos->second)).count() >= _work_config.reset_second_time && symbol == std::get<0>(pos->second)) {
                if (std::get<1>(pos->second) == "sell") {
                    has_sell_orders = false;
                    _general_logger->info("{} ордер на отмену висит {} секунд и будет сброшен (has_sell_orders = false). {} sell.", pos->first, _work_config.reset_second_time,symbol);
                } else if (std::get<1>(pos->second) == "buy") {
                    has_buy_orders = false;
                    _general_logger->info("{} ордер на отмену висит {} секунд и будет сброшен (has_buy_orders = false). {} buy.", pos->first, _work_config.reset_second_time, symbol);
                }
                pos = _cancel_id.erase(pos);
             } else {
                ++pos;
            }
        }

        // получаем минимальный порог ,зависящий от amount increment для текущего ассета,
        //double threshold = std::get<4>(markets_tuple);
        double base_threshold = _min_deal_amounts[std::get<0>(markets_tuple).first];
        // если ордера на продажу нет и баланс продаваемого ассета больше чем минимальный порог (т.е. можно продать)
        if (!has_sell_orders && (_balance[std::get<0>(markets_tuple).first] > base_threshold)) {
            _general_logger->info("В наличии есть {} {}. Можно продать {} {}. Это больше чем {}.",
                                  std::get<0>(markets_tuple).first, _balance[std::get<0>(markets_tuple).first],
                                  sell_quantity.convert_to<double>(), std::get<0>(markets_tuple).second, base_threshold);
            std::string client_id = create_order("sell", symbol, sell_price, sell_quantity, std::get<3>(markets_tuple), std::get<4>(markets_tuple));
            // запоминаем ордер
            _clients_id[client_id] = std::make_tuple(symbol, "sell", std::chrono::system_clock::now(), false);
            // запоминаем границы
            std::get<1>(markets_tuple) = std::make_pair(min_ask, max_ask);
            has_sell_orders = true;
        }
        double quote_threshold = _min_deal_amounts[std::get<0>(markets_tuple).second];
        // если ордера на покупку нет и баланс ассета больше 0 (т.е. можно потратить)
        if (!has_buy_orders && (_balance[std::get<0>(markets_tuple).second] > quote_threshold) && (buy_quantity > std::get<4>(markets_tuple))) {
            _general_logger->info("В наличии есть {} {}. Можно купить {} {}. Это больше чем > {}",
                                  std::get<0>(markets_tuple).second, _balance[std::get<0>(markets_tuple).second],
                                  buy_quantity.convert_to<double>(), std::get<0>(markets_tuple).first, quote_threshold);
            std::string client_id = create_order("buy", symbol, buy_price, buy_quantity, std::get<3>(markets_tuple), std::get<4>(markets_tuple));
            // запоминаем ордер
            _clients_id[client_id] = std::make_tuple(symbol, "buy", std::chrono::system_clock::now(), false);
            // запоминаем границы
            std::get<2>(markets_tuple) = std::make_pair(min_bid, max_bid);
            has_buy_orders = true;
        }
        // Если есть ордер на продажу, но усреднённое лучшее предложение за пределами удержания — отменить ордер
        if (has_sell_orders && !((std::get<1>(markets_tuple).first < avg_ask) && (avg_ask < std::get<1>(markets_tuple).second))) {
            // если идентификатор не нулевой
            if (id_sell_order != 0) {
                // отменяем
                cancel_order("sell", symbol, id_sell_order);
                //has_sell_orders = false;
                // запомним идентификатор ордера на отмену
                _cancel_id[id_sell_order] = std::make_tuple(symbol, "sell", std::chrono::system_clock::now(), false);
                id_sell_order = 0;
            }
        }

        // Если есть ордер на покупку, но усреднённое лучшее предложение за пределами удержания — отменить ордер
        if (has_buy_orders && !((std::get<2>(markets_tuple).first < avg_bid) && (avg_bid < std::get<2>(markets_tuple).second))) {
            // если идентификатор не нулевой
            if (id_buy_order != 0) {
                // отменяем
                cancel_order("buy", symbol, id_buy_order);
                //has_buy_orders = false;
                // запомним идентификатор ордера на отмену
                _cancel_id[id_buy_order] = std::make_tuple(symbol, "buy", std::chrono::system_clock::now(), false);

                id_buy_order = 0;
            }
        }
    }
}

/**
 * Рассчитать среднее арифметическое лучших ордеров для тикера
 *
 * @param ticker Тикер
 * @return Пара, содержащая цену покупки и продажи соответственно
 */
std::pair<dec_float, dec_float> core::avg_orderbooks(const std::string& ticker) {

    // Суммы лучших предложений
    dec_float sum_ask(0);
    dec_float sum_bid(0);
    for (auto const&[exchange, exchange_orderbooks]: _orderbooks) {
        auto find_ticker = exchange_orderbooks.find(ticker);
        // если ордербук содержит такой тикер
        if (find_ticker != exchange_orderbooks.end()) {
            auto[ask, bid] = exchange_orderbooks.at(ticker);
            sum_ask += ask;
            sum_bid += bid;
        }
    }

    // Количество лучших предложений
    dec_float size(_orderbooks.size());

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
std::string core::create_order(std::string_view side_, const std::string& symbol_, const dec_float& price_, const dec_float& quantity_, double price_precission_, double amount_precission_) {
    // создаем уникальный клиентский идентификатор
    std::string client_id = create_client_id();
    double price = utils::digit_trim(price_.convert_to<double>(), utils::get_exponent(price_precission_));
    double amount = utils::digit_trim(quantity_.convert_to<double>(), utils::get_exponent(amount_precission_));

    JSON create_order_root;
    create_order_root["event"]     = "command";
    create_order_root["exchange"]  = _work_config.exchange.name;
    create_order_root["node"]      = "core";
    create_order_root["instance"]  = _work_config.exchange.instance;
    create_order_root["action"]    = "create_order";
    create_order_root["message"]   = nullptr;
    create_order_root["algo"]      = _work_config.exchange.algo;
    create_order_root["timestamp"] = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    JSON data;
    for (int i = 0; i < 1; ++i) {
        JSON order;
        order["symbol"]  = symbol_;
        order["type"]    = "limit";
        order["side"]    = side_;
        order["price"]  = price;
        order["amount"] = amount;
        order["client_id"] = client_id;
        data.push_back(order);
    }
    create_order_root["data"] = data;

    // логируем информацию
    _general_logger->info(">>>-------->>>--------->>>");
    _general_logger->info(create_order_root.dump());
    // отправляем команду в гейт
    std::int64_t result = _gateway_channel->offer(create_order_root.dump());
    if (result < 0) {
        processing_error("Ошибка отправки команды создания ордера в гейт: ", _prev_command_message, result);
        if (result == -3) {
            result = _gateway_channel->offer(create_order_root.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки команды создания ордера в гейт: ", _prev_command_message, result);
            }
        }
    } else {
        // запомним предудущее сообщение
        _prev_command_message = fmt::format("Результат: {}. Сообщение: {}", result, create_order_root.dump());
    }
    // теперь отправим все это дело в лог
    result = _log_channel->offer(create_order_root.dump());
    if (result < 0) {
        // чтобы не переполнять лог файл ошибок не будем в первый раз логировать ошибку с кодом -3
        if (result != -3)
            processing_error("Ошибка отправки команды create_order в лог: ", _prev_create_order_log_message, result);
        if (result == -3) {
            // пробуем отправить еще раз
            result = _log_channel->offer(create_order_root.dump());
            if (result < 0) {
                processing_error("Повторная ошибка отправки информации о статусе ордера в лог: ", _prev_create_order_log_message, result);
            }
        }
    } else {
        // продублируем информацию в файл лога
        _logs_logger->info(create_order_root.dump());
        _prev_create_order_log_message = fmt::format("Результат: {}. Сообщение: {}", result, create_order_root.dump());
    }
    return client_id;
}
/**
 * Отменить ордера
 *
 * @param side Тип ордера
 */
void core::cancel_order(std::string_view side_, std::string symbol_, int64_t id_) {

    JSON cancel_orders;
    cancel_orders["event"]       = "command";
    cancel_orders["exchange"]    = _work_config.exchange.name;
    cancel_orders["node"]        = "core";
    cancel_orders["instance"]    = _work_config.exchange.instance;
    cancel_orders["action"]      = "cancel_order";
    cancel_orders["message"]     = nullptr;
    cancel_orders["algo"]        = _work_config.exchange.algo;
    cancel_orders["timestamp"]   = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    //cancel_orders["data"]        = nullptr;

    JSON data;
    JSON order;
    order["id"]     = std::to_string(id_);
    order["symbol"] = symbol_;
    data.push_back(order);

    cancel_orders["data"] = data;
    std::cout << ">>>-------->>>--------->>>"<< std::endl;
    // логируем информацию
   _general_logger->info(cancel_orders.dump());
   // отправляем команду в гейт
   std::int64_t result = _gateway_channel->offer(cancel_orders.dump());
   if (result < 0) {
       processing_error("Ошибка отправки команды отмены ордеров в гейт: ", _prev_command_message, result);
       if (result == -3) {
           result = _gateway_channel->offer(cancel_orders.dump());
           if (result < 0) {
               processing_error("Повторная ошибка отправки команды отмены ордеров в гейт: ", _prev_command_message, result);
           }
       }
   } else {
       // запомним предудущее сообщение
       _prev_command_message = fmt::format("Результат: {}. Сообщение: {}", result, cancel_orders.dump());
   }
   // теперь отправим все это дело в лог
   result = _log_channel->offer(cancel_orders.dump());
   if (result < 0) {
       // чтобы не переполнять лог файл ошибок не будем в первый раз логировать ошибку с кодом -3
       if (result != -3)
           processing_error("Ошибка отправки команды create_order в лог: ", _prev_cancel_order_log_message, result);
       if (result == -3) {
           // пробуем отправить еще раз
           result = _log_channel->offer(cancel_orders.dump());
           if (result < 0) {
               processing_error("Повторная ошибка отправки информации о статусе ордера в лог: ", _prev_cancel_order_log_message, result);
           }
       }
   } else {
       // продублируем информацию в файл лога
       _logs_logger->info(cancel_orders.dump());
       _prev_cancel_order_log_message = fmt::format("Результат: {}. Сообщение: {}", result, cancel_orders.dump());
   }

}
//---------------------------------------------
// возвращает источник конфига
//---------------------------------------------
std::string core::get_config_source() {
    return _default_config.source;
}
//---------------------------------------------------------------
// посылает ошибку в лог и в консоль
//---------------------------------------------------------------
void core::send_error(std::string_view error_) {
    _general_logger->error(error_);
    _errors_logger->error(error_);
}
//---------------------------------------------------------------
// посылает сообщение в лог и в консоль
//---------------------------------------------------------------
void core::send_message(std::string_view message_) {
    _general_logger->info(message_);
}
//---------------------------------------------------------------
// проверяет, получен ли конфиг
//---------------------------------------------------------------
bool core::has_config() {
    return _config_was_received;
}
//---------------------------------------------------------------
// возвращает конфиг с сервера конфигуратора
//---------------------------------------------------------------
bool core::get_config_from_api(bss::error& error_) {
    // делаем get запрос (параметры уже должны быть получены)
    http::response<http::string_body> response = http_session.get_config(_default_config.config_uri, _default_config.config_target);
    return load_config_from_json(response.body(), error_);
}
//--------------------------------------------------------------
// генерирует клиентский идентификатор ордера
//--------------------------------------------------------------
std::string core::create_client_id() {
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    return boost::uuids::to_string(uuid);
}

