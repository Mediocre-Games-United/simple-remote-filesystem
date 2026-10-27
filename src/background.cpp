#include "background.hpp"
#include "parser_with_cache.hpp"
#include <chrono>
#include <thread>
#include <atomic>

static std::thread bg;
static std::atomic<bool> should_quit = false;
static size_t check_countdown;

static void thread_func() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::duration<F64>(0.1));

        if (should_quit) return;

        if (check_countdown <= 0) {
            check_countdown = 100;
            background_checks();
        }
        check_countdown -= 1;
    }
}


void start_background() {
    bg = std::thread(thread_func);
}
void end_background() {
    should_quit = true;
    bg.join();
}
