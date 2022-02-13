#include <atomic>
#include <memory>
#include <sentry.h>
#include "Core.h"
#include "logging.h"

const char* SENTRY_DSN = "https://81fe26996acd4da08ed93398cbd23e91@o1134619.ingest.sentry.io/6182264";
const char* CONFIG_FILE_PATH = "config.toml";

std::atomic<bool> running(true);

/**
 * Обработчик прерывания SIGINT
 */
void sigint_handler(int);

int main()
{
    // Инициализация Sentry
    sentry_options_t* options = sentry_options_new();
    sentry_options_set_debug(options, true);
    sentry_options_set_dsn(options, SENTRY_DSN);
    sentry_options_set_symbolize_stacktraces(options, true);
    sentry_options_add_attachment(options, CONFIG_FILE_PATH);
    sentry_options_add_attachment(options, "logs/orderbooks.log");
    sentry_options_add_attachment(options, "logs/balance.log");
    sentry_options_add_attachment(options, "logs/orders.log");
    sentry_options_add_attachment(options, "logs/errors.log");
    sentry_init(options);

    // Инициализация Boost.Log
    init_logging();

    // Инициализация ядра
    std::shared_ptr<Core> core = std::make_shared<Core>(CONFIG_FILE_PATH);

    // Рабочий цикл
    signal(SIGINT, sigint_handler);
    while (running)
        core->poll();

    sentry_close();
    return EXIT_SUCCESS;
}

/**
 * Обработчик прерывания SIGINT
 */
void sigint_handler(int)
{
    running = false;
}
