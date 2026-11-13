// Netplay's entry points (include/btga_netplay.h).
#include <cstdio>

#include "ultramodern/ultramodern.hpp"

#include "btga_netplay.h"
#include "input_record.h"
#include "sync_test.h"

void btga::netplay::startup() {
    if (sync_test_requested()) {
        // Before the game starts, as the runtime's deterministic mode needs.
        ultramodern::set_deterministic(true);
        std::fprintf(stderr, "Sync test on: deterministic mode.\n");
    }
}

void btga::netplay::frame(uint8_t* rdram) {
    sync_test_frame(rdram);
}

bool btga::netplay::deterministic() {
    return ultramodern::is_deterministic();
}

bool btga::netplay::controller_read(int port, bool answered, uint16_t* buttons, float* x, float* y) {
    return input_record_read(port, answered, buttons, x, y);
}
