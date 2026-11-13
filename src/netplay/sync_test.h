#ifndef __BTGA_NETPLAY_SYNC_TEST_H__
#define __BTGA_NETPLAY_SYNC_TEST_H__

#include <cstdint>

// The sync test (sync_test.cpp), inside the netplay folder.
namespace btga::netplay {
    // BTGA_SYNCTEST is set (and its file could be opened).
    bool sync_test_requested();
    // Each game frame: the hashes, when the test is on.
    void sync_test_frame(uint8_t* rdram);
    // The hash of the game's memory, as the test hashes it (0 before the
    // first frame): for netplay's check that both games are the same.
    uint64_t memory_hash();
}

#endif
