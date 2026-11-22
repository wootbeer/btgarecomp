# Netplay join codes

`worker.js` is a small Cloudflare Worker that gives a hosting game a six
letter join code and swaps the two players' addresses when the other player
types it. Game traffic never goes through it; once both games have the
other's addresses they connect to each other directly (UDP hole punching).
It runs on Cloudflare's free plan.

## Setting it up

1. Make a free account at https://dash.cloudflare.com. Under
   **Workers & Pages**, pick a `workers.dev` subdomain when it asks.
2. Install Node.js (the LTS version) from https://nodejs.org.
3. In PowerShell:
   ```
   cd C:\Users\Matt\source\repos\btgarecomp\btgarecomp\tools\matchmaker
   npx wrangler login
   npx wrangler deploy
   ```
   `login` opens the browser to let wrangler use the account. `deploy`
   prints the Worker's address, like
   `https://btga-match.<your-subdomain>.workers.dev`.
4. Open that address in a browser: it should say
   `BattleTanx GA netplay join codes`.
5. The game's builds use the address set in `kMatchmaker`
   (src/netplay/session.cpp), now https://btga-match.wootbeer.workers.dev. To try one first, set it for one launch:
   ```
   $env:BTGA_MATCHMAKER = "https://btga-match.<your-subdomain>.workers.dev"; .\BattleTanxGARecompiled.exe
   ```

`npx wrangler deploy` again after changing `worker.js`.

## What it does

| Request | Answer |
| --- | --- |
| `/host?addr=&local=&v=[&public=1&name=]` | `CODE TOKEN` |
| `/poll?code=&token=[&players=]` | `waiting`, or each joiner's `ADDR LOCAL SEEN`, a line each (up to three) |
| `/join?code=&addr=&local=&v=` | the host's `ADDR LOCAL SEEN`; 404 `no game`, 409 `version` |
| `/close?code=&token=` | `closed` |
| `/list?v=` | the public games on that build, `CODE<tab>PLAYERS<tab>NAME` a line each (full ones left out) |

`ADDR` is what the game's STUN request found (`-` if none), `LOCAL` its
address on its own network, `SEEN` the address the request came from with
the game's UDP port (which works when the host forwarded the port). Each
game is a Durable Object that deletes itself after 15 minutes, or when the
host's game starts or stops hosting. A game uses about one request a second
while the host waits, well inside the free plan's 100,000 a day.

## Trying it locally

```
npx wrangler dev --port 8787
```
and launch both games with `BTGA_MATCHMAKER=http://127.0.0.1:8787`.

## Public games

A host with Public on is listed under its lobby name (letters, digits,
spaces and a little punctuation, 24 characters at most, cleaned here and in
the game) in one more Durable Object, `Directory`. The host's polls refresh
its entry and player count (every 10 seconds at most); it goes when the
host's game starts or it stops hosting, or 30 seconds after the last poll.
The game's Join panel asks for the list every 15 seconds while it's open.
