// BattleTanx GA netplay join codes: a Cloudflare Worker that swaps the two
// players' addresses (src/main/netplay.cpp). No game traffic comes here:
// once each side has the other's addresses they talk to each other directly.
//
//   /host?addr=IP:PORT&local=IP:PORT&v=BUILD[&public=1&name=NAME] -> "CODE TOKEN"
//   /poll?code=CODE&token=TOKEN[&players=N]   -> "waiting", or each joiner's "ADDR LOCAL SEEN", a line each
//   /list?v=BUILD                             -> the public games on that build, "CODE<tab>PLAYERS<tab>NAME" a line each
//   /join?code=CODE&addr=IP:PORT&local=IP:PORT&v=BUILD -> the host's "ADDR LOCAL SEEN"; 409 "version NAME" (the host's), 409 "full"
//   /close?code=CODE&token=TOKEN              -> "closed"
//
// ADDR is what the game's STUN request found (or "-"), LOCAL its address on
// its own network, SEEN the address this Worker saw the request from with
// the game's UDP port (for a forwarded port when STUN fails). Each game is
// one Durable Object, gone 15 minutes after hosting. A public game is also
// listed in one Directory object: refreshed by the host's polls (every 10 s
// at most), dropped when the host closes its code (its game started, or it
// stopped hosting) or after 30 s without a poll.

const ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"; // no 0/O, 1/I/L
const CODE_LENGTH = 6;
const LIFETIME_MS = 15 * 60 * 1000;
const LIST_REFRESH_MS = 10 * 1000;
const LIST_STALE_MS = 30 * 1000;
const LIST_MAX = 20;
const MAX_PLAYERS = 4;
const LIST_CAP = 200;              // public games at most
const LISTINGS_PER_IP = 3;         // new public games from one address per LISTING_WINDOW_MS
const LISTING_WINDOW_MS = 10 * 60 * 1000;

function text(body, status = 200) {
  return new Response(body, { status, headers: { "content-type": "text/plain", "cache-control": "no-store" } });
}

// IPv4 address and port, or "-".
function address(value) {
  if (value === "-") return value;
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3}):(\d{1,5})$/.exec(value || "");
  if (!m) return null;
  if (m.slice(1, 5).some((n) => Number(n) > 255) || Number(m[5]) < 1 || Number(m[5]) > 65535) return null;
  return value;
}

// An address on a home network (10/8, 172.16/12, 192.168/16).
function privateAddress(value) {
  const m = /^(\d+)\.(\d+)\./.exec(value || "");
  if (!m) return false;
  const a = Number(m[1]), b = Number(m[2]);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
}

function clientIp(request) {
  const ip = request.headers.get("cf-connecting-ip") || "";
  return /^\d{1,3}(\.\d{1,3}){3}$/.test(ip) ? ip : "";
}

function seen(request, local) {
  const ip = request.headers.get("cf-connecting-ip") || "";
  const port = local === "-" ? "" : local.split(":")[1];
  return /^\d{1,3}(\.\d{1,3}){3}$/.test(ip) && port ? ip + ":" + port : "-";
}

// A lobby's name: letters, digits, spaces and a little punctuation, at most
// 24 characters.
function lobbyName(value) {
  const name = (value || "").replace(/[^A-Za-z0-9 '!?.,:()_-]/g, "").replace(/\s+/g, " ").trim().slice(0, 24);
  return name || "BattleTanx game";
}

function randomText(length) {
  const bytes = crypto.getRandomValues(new Uint8Array(length));
  return Array.from(bytes, (b) => ALPHABET[b % ALPHABET.length]).join("");
}

export class Game {
  constructor(state, env) {
    this.state = state;
    this.env = env;
  }

  directory() {
    return this.env.LOBBIES.get(this.env.LOBBIES.idFromName("directory"));
  }

  async list(game, code, players) {
    game.listed = Date.now();
    game.players = players;
    const q = new URLSearchParams({ code, name: game.name, v: game.version, players: String(players), ip: game.ip || "" });
    await this.directory().fetch("https://directory/put?" + q.toString());
  }

  async unlist(code) {
    await this.directory().fetch("https://directory/remove?code=" + code);
  }

  async fetch(request) {
    const url = new URL(request.url);
    const p = url.searchParams;
    const storage = this.state.storage;
    const game = await storage.get("game");
    const live = game && Date.now() - game.created < LIFETIME_MS;

    if (url.pathname === "/host") {
      if (live) return text("taken", 409);
      const entry = {
        created: Date.now(),
        token: randomText(16),
        version: p.get("v") || "",
        versionName: p.get("vn") || "",
        host: [p.get("addr"), p.get("local"), p.get("seen")],
        joiners: [], // up to three, the latest kept
        public: p.get("public") === "1",
        ip: p.get("ip") || "",
        name: p.get("name") || "",
        code: p.get("code"),
        players: 1,
        listed: 0,
      };
      if (entry.public) {
        await this.list(entry, entry.code, 1);
      }
      await storage.put("game", entry);
      await storage.setAlarm(Date.now() + LIFETIME_MS);
      return text(entry.token);
    }
    if (!live) return text("no game", 404);
    if (url.pathname === "/join") {
      // The host's version by name, for the joiner to be told who's behind.
      if (game.version && p.get("v") && game.version !== p.get("v")) return text("version " + (game.versionName || ""), 409);
      const joiner = [p.get("addr"), p.get("local"), p.get("seen")];
      const joiners = game.joiners || [];
      if (!joiners.some((j) => j.join(" ") === joiner.join(" "))) {
        if (joiners.length >= MAX_PLAYERS - 1) return text("full", 409); // nobody already in is pushed out
        joiners.push(joiner);
      }
      game.joiners = joiners;
      await storage.put("game", game);
      return text(game.host.join(" "));
    }
    if (p.get("token") !== game.token) return text("no game", 404);
    if (url.pathname === "/poll") {
      const players = Number(p.get("players"));
      if (game.public && players >= 1 && players <= MAX_PLAYERS &&
          (players !== game.players || Date.now() - game.listed >= LIST_REFRESH_MS)) {
        await this.list(game, game.code, players);
        await storage.put("game", game);
      }
      const joiners = game.joiners || [];
      return text(joiners.length ? joiners.map((j) => j.join(" ")).join("\n") : "waiting");
    }
    if (url.pathname === "/close") {
      if (game.public) {
        await this.unlist(game.code);
      }
      await storage.deleteAll();
      return text("closed");
    }
    return text("not found", 404);
  }

  async alarm() {
    const game = await this.state.storage.get("game");
    if (game && game.public) {
      await this.unlist(game.code);
    }
    await this.state.storage.deleteAll();
  }
}

// The public games: one object for all of them, each game under its own key
// in its storage (and kept in memory, as it may be evicted between
// requests). At most LIST_CAP, and LISTINGS_PER_IP new ones from one address
// in LISTING_WINDOW_MS, so nobody can fill the list.
export class Directory {
  constructor(state) {
    this.state = state;
    this.recent = new Map(); // address -> times it listed a new game
  }

  async games() {
    if (!this.cache) {
      this.cache = {};
      const stored = await this.state.storage.list({ prefix: "g:" });
      for (const [key, g] of stored) {
        this.cache[key.slice(2)] = g;
      }
    }
    const now = Date.now();
    for (const [code, g] of Object.entries(this.cache)) {
      if (now - g.updated > LIST_STALE_MS) {
        delete this.cache[code];
        await this.state.storage.delete("g:" + code);
      }
    }
    return this.cache;
  }

  allowed(ip) {
    const now = Date.now();
    const times = (this.recent.get(ip) || []).filter((t) => now - t < LISTING_WINDOW_MS);
    if (times.length >= LISTINGS_PER_IP) return false;
    times.push(now);
    this.recent.set(ip, times);
    return true;
  }

  async fetch(request) {
    const url = new URL(request.url);
    const p = url.searchParams;
    const games = await this.games();
    const code = p.get("code");
    if (url.pathname === "/put") {
      if (!games[code]) {
        if (Object.keys(games).length >= LIST_CAP || !this.allowed(p.get("ip") || "")) return text("full", 429);
      }
      const g = { name: p.get("name"), version: p.get("v"), players: Number(p.get("players")) || 1, updated: Date.now() };
      games[code] = g;
      await this.state.storage.put("g:" + code, g);
      return text("ok");
    }
    if (url.pathname === "/remove") {
      delete games[code];
      await this.state.storage.delete("g:" + code);
      return text("ok");
    }
    if (url.pathname === "/list") {
      const lines = Object.entries(games)
        .filter(([, g]) => g.version === p.get("v") && g.players < MAX_PLAYERS)
        .sort((a, b) => b[1].updated - a[1].updated)
        .slice(0, LIST_MAX)
        .map(([c, g]) => c + "\t" + g.players + "\t" + g.name);
      return text(lines.join("\n"));
    }
    return text("not found", 404);
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const p = url.searchParams;
    const route = url.pathname;
    if (!["/host", "/join", "/poll", "/close", "/list"].includes(route)) {
      return text("BattleTanx GA netplay join codes");
    }
    if (route === "/list") {
      const v = (p.get("v") || "").slice(0, 32);
      return env.LOBBIES.get(env.LOBBIES.idFromName("directory")).fetch("https://directory/list?v=" + encodeURIComponent(v));
    }
    // Checked here, so only clean values reach a game's object.
    const forward = new URL("https://game" + route);
    if (route === "/host" || route === "/join") {
      let addr = address(p.get("addr"));
      let local = address(p.get("local"));
      const v = (p.get("v") || "").slice(0, 32);
      if (addr === null || local === null) return text("bad address", 400);
      if (!v) return text("version", 409);
      // Only addresses that are the caller's: its outside address as seen
      // here, and one on a home network; others are dropped, so nobody can
      // have a game send to someone else.
      const ip = clientIp(request);
      const seenHere = seen(request, local); // the game's own port, at the address the request came from
      if (addr !== "-" && ip && addr.split(":")[0] !== ip) addr = "-";
      if (local !== "-" && !privateAddress(local)) local = "-";
      forward.searchParams.set("addr", addr);
      forward.searchParams.set("local", local);
      forward.searchParams.set("seen", seenHere);
      forward.searchParams.set("v", v);
      forward.searchParams.set("vn", (p.get("vn") || "").replace(/[^A-Za-z0-9 .()-]/g, "").slice(0, 31));
      forward.searchParams.set("ip", ip);
    }
    if (route === "/host" && p.get("public") === "1") {
      forward.searchParams.set("public", "1");
      forward.searchParams.set("name", lobbyName(p.get("name")));
    }
    if (route === "/poll") {
      const players = Number(p.get("players"));
      if (players >= 1 && players <= MAX_PLAYERS) {
        forward.searchParams.set("players", String(players));
      }
    }
    let code;
    if (route === "/host") {
      // A fresh code; a few tries in case one is in use.
      for (let i = 0; i < 5; i++) {
        code = randomText(CODE_LENGTH);
        forward.searchParams.set("code", code);
        const answer = await env.GAMES.get(env.GAMES.idFromName(code)).fetch(forward.toString());
        if (answer.status === 200) return text(code + " " + (await answer.text()));
      }
      return text("busy", 503);
    }
    code = (p.get("code") || "").toUpperCase();
    if (!new RegExp("^[" + ALPHABET + "]{" + CODE_LENGTH + "}$").test(code)) return text("no game", 404);
    if (route !== "/join") forward.searchParams.set("token", (p.get("token") || "").slice(0, 32));
    return env.GAMES.get(env.GAMES.idFromName(code)).fetch(forward.toString());
  },
};
