#ifndef __BTGA_NETPLAY_INPUT_RECORD_H__
#define __BTGA_NETPLAY_INPUT_RECORD_H__

#include <cstdint>

// Input record and replay (input_record.cpp): BTGA_INPUTREC, BTGA_INPUTPLAY.
namespace btga::netplay {
    // One port's controller read: recorded, or replaced by the recording's.
    bool input_record_read(int port, bool answered, uint16_t* buttons, float* x, float* y);
}

#endif
