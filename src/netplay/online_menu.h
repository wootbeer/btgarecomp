#ifndef __BTGA_NETPLAY_ONLINE_MENU_H__
#define __BTGA_NETPLAY_ONLINE_MENU_H__

// The launcher's Netplay screen and the menu's Netplay tab (online_menu.cpp),
// inside the netplay folder (the rest is in include/btga_netplay.h).
namespace btga::netplay {
    // The Netplay tab's Leave Game: the game quits, and starts again on the
    // Netplay screen (quitting()).
    bool leaving_to_netplay_screen();
}

#endif
