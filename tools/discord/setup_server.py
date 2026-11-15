#!/usr/bin/env python3
"""Sets up the World of Kenshi Discord server (English): roles, categories, channels, permissions and
the welcome / rules / guide messages. Nothing is deleted: the default channels are renamed and moved.
Safe to run again: roles and channels with the same name are reused, messages are only posted once.

Run on the VPS:  set -a; . /etc/wok-bot.env; set +a; python3 setup_server.py
Needs DISCORD_BOT_TOKEN and GUILD_ID (and OWNER_ID: the owner gets the Staff role)."""
import json
import os
import time
import urllib.error
import urllib.request

TOKEN, GUILD = os.environ["DISCORD_BOT_TOKEN"], os.environ["GUILD_ID"]
OWNER = os.environ.get("OWNER_ID", "")
RELEASES = "https://github.com/Blast-Pop/WorldOfKenshi/releases/latest"

VIEW, SEND, HISTORY = 1 << 10, 1 << 11, 1 << 16
THREADS = (1 << 35) | (1 << 36) | (1 << 38)
ADD_REACTIONS, CONNECT, SPEAK = 1 << 6, 1 << 20, 1 << 21
TEXT, VOICE, CATEGORY, FORUM = 0, 2, 4, 15
SAND, RUST, GREEN, GREY = 0xC8A165, 0xB0452F, 0x6B8E4E, 0x7A7468


def api(method, path, body=None, reason="World of Kenshi server setup"):
    data = json.dumps(body).encode() if body is not None else None
    for _ in range(8):
        req = urllib.request.Request("https://discord.com/api/v10" + path, data=data, method=method,
                                     headers={"Authorization": "Bot " + TOKEN, "User-Agent": "WorldOfKenshiBot (1.0)",
                                              "Content-Type": "application/json", "X-Audit-Log-Reason": reason})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                raw = r.read().decode()
                return json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            txt = e.read().decode()
            if e.code == 429:
                time.sleep(float(json.loads(txt).get("retry_after", 1)) + 0.3)
                continue
            raise RuntimeError("%s %s -> %d %s" % (method, path, e.code, txt[:300]))
    raise RuntimeError("rate limited: " + path)


# ---------------------------------------------------------------- roles
ROLES = [("Staff", RUST, True), ("Tester", GREEN, True), ("Drifter", SAND, False)]  # name, colour, shown apart
roles = {r["name"]: r for r in api("GET", "/guilds/%s/roles" % GUILD)}
for name, colour, hoist in ROLES:
    if name not in roles:
        roles[name] = api("POST", "/guilds/%s/roles" % GUILD, {"name": name, "color": colour, "hoist": hoist,
                                                               "mentionable": False})
        print("role", name, "created")
EVERYONE, STAFF, TESTER, DRIFTER = GUILD, roles["Staff"]["id"], roles["Tester"]["id"], roles["Drifter"]["id"]
if OWNER:
    try:
        api("PUT", "/guilds/%s/members/%s/roles/%s" % (GUILD, OWNER, STAFF))
    except RuntimeError as e:
        print("could not give Staff to the owner:", e)


def ow(rid, allow=0, deny=0):
    return {"id": rid, "type": 0, "allow": str(allow), "deny": str(deny)}


# Newcomers (no Drifter role) only see #welcome and #rules; accepting the rules gives Drifter.
MEMBERS = [ow(EVERYONE, 0, VIEW), ow(DRIFTER, VIEW | SEND | HISTORY | THREADS | ADD_REACTIONS | CONNECT | SPEAK),
           ow(STAFF, VIEW | SEND)]
READ_ONLY = [ow(EVERYONE, 0, VIEW | SEND | THREADS), ow(DRIFTER, VIEW | HISTORY | ADD_REACTIONS, SEND | THREADS),
             ow(STAFF, VIEW | SEND)]
OPEN_READ_ONLY = [ow(EVERYONE, VIEW | HISTORY, SEND | THREADS | ADD_REACTIONS), ow(STAFF, VIEW | SEND)]
STAFF_ONLY = [ow(EVERYONE, 0, VIEW), ow(STAFF, VIEW | SEND | HISTORY)]

# category, overwrites, [(channel, type, topic, own overwrites or None = the category's)]
LAYOUT = [
    ("📜 INFORMATION", READ_ONLY, [
        ("welcome", TEXT, "Start here.", OPEN_READ_ONLY),
        ("rules", TEXT, "Read and accept the rules to see the whole server.", OPEN_READ_ONLY),
        ("announcements", TEXT, "News and new versions of World of Kenshi.", None),
        ("server-status", TEXT, "Live list of World of Kenshi servers.", None),
        ("coming-soon", TEXT, "What is planned: server hosting, and where the money goes.", None),
    ]),
    ("🗡️ WORLD OF KENSHI", MEMBERS, [
        ("get-started", TEXT, "How to install and play. Type /download for the latest version.", READ_ONLY),
        ("help", TEXT, "Ask for help with installing or playing.", None),
        ("bug-reports", FORUM, "One post per bug. The fastest way: the Report a bug button in the game's Multiplayer window.", None),
        ("suggestions", TEXT, "Ideas to make World of Kenshi better.", None),
        ("hosting", TEXT, "Run your own World of Kenshi server.", None),
    ]),
    ("💬 COMMUNITY", MEMBERS, [
        ("general", TEXT, "Talk about anything.", None),
        ("screenshots", TEXT, "Show your squad, your base, your worst death.", None),
        ("looking-for-group", TEXT, "Find people to play with.", None),
    ]),
    ("🔊 VOICE", MEMBERS, [
        ("General", VOICE, None, None),
        ("Squad", VOICE, None, None),
    ]),
    ("🔒 STAFF", STAFF_ONLY, [
        ("staff", TEXT, "Staff only.", None),
        ("bot-log", TEXT, "What the WOK Bot does.", None),
    ]),
]
DEFAULTS = {"Text Channels": "💬 COMMUNITY", "Salons textuels": "💬 COMMUNITY", "Voice Channels": "🔊 VOICE",
            "Salons vocaux": "🔊 VOICE", "général": "general", "Général": "General"}

# Forum channels need a Community server (which also needs a rules channel): made after the channels.
guild = api("GET", "/guilds/%s" % GUILD)
community = "COMMUNITY" in guild.get("features", [])

existing = api("GET", "/guilds/%s/channels" % GUILD)
by_name = {}
for c in existing:
    by_name.setdefault((DEFAULTS.get(c["name"], c["name"]), c["type"] == CATEGORY), c)

ids = {}
for pos, (cat_name, cat_ow, channels) in enumerate(LAYOUT):
    cat = by_name.get((cat_name, True))
    body = {"name": cat_name, "position": pos, "permission_overwrites": cat_ow}
    cat = api("PATCH", "/channels/%s" % cat["id"], body) if cat else api("POST", "/guilds/%s/channels" % GUILD,
                                                                          dict(body, type=CATEGORY))
    print("category", cat_name)
    for cpos, (name, ctype, topic, own) in enumerate(channels):
        if ctype == FORUM and not community:
            continue  # made once the server is a Community server, below
        ch = by_name.get((name, False))
        body = {"name": name, "parent_id": cat["id"], "position": cpos,
                "permission_overwrites": own if own is not None else cat_ow}
        if topic and ctype != VOICE:
            body["topic"] = topic
        if ctype == FORUM and not ch:
            body["available_tags"] = [{"name": n} for n in ("Bug", "Crash", "Fixed", "Duplicate")]
        ch = api("PATCH", "/channels/%s" % ch["id"], body) if ch else api("POST", "/guilds/%s/channels" % GUILD,
                                                                          dict(body, type=ctype))
        ids[name] = ch["id"]
        print("   #" + name)

if not community:  # turn on Community (forums), then run the layout once more for #bug-reports
    api("PATCH", "/guilds/%s" % GUILD, {"verification_level": 1, "explicit_content_filter": 2,
                                        "features": guild.get("features", []) + ["COMMUNITY"],
                                        "rules_channel_id": ids["rules"], "public_updates_channel_id": ids["staff"]})
    print("Community enabled: run this script again to make the #bug-reports forum")
else:
    api("PATCH", "/guilds/%s" % GUILD, {"system_channel_id": ids["welcome"], "rules_channel_id": ids["rules"],
                                        "public_updates_channel_id": ids["staff"]})


def post(channel, embed, components=None, pin=False):
    title = embed["title"]
    for m in api("GET", "/channels/%s/messages?limit=30" % ids[channel]) or []:
        if m["author"].get("bot") and any(e.get("title") == title for e in m.get("embeds", [])):
            print("   (already posted in #%s: %s)" % (channel, title))
            return
    body = {"embeds": [embed], "allowed_mentions": {"parse": []}}
    if components:
        body["components"] = components
    m = api("POST", "/channels/%s/messages" % ids[channel], body)
    if pin:
        api("PUT", "/channels/%s/pins/%s" % (ids[channel], m["id"]))
    print("   posted in #%s: %s" % (channel, title))


if "get-started" in ids:
    c = lambda n: "<#%s>" % ids[n] if n in ids else "#" + n  # noqa: E731
    post("welcome", {"title": "Welcome to World of Kenshi", "color": SAND, "description":
        "**World of Kenshi** is a mod that turns Kenshi into an online game: everyone plays their own character "
        "in the same persistent world, on dedicated servers anyone can host.\n\n"
        "**New here?**\n"
        "1. Read %s and press **I accept the rules**\n"
        "2. Follow %s to install the mod\n"
        "3. Pick a server in %s, or in the game's **Multiplayer** window\n"
        "4. Stuck? Ask in %s" % (c("rules"), c("get-started"), c("server-status"), c("help"))})
    post("rules", {"title": "Rules", "color": RUST, "description":
        "1. Be respectful. No harassment, hate speech or personal attacks.\n"
        "2. No spam, advertising or self-promotion without staff approval.\n"
        "3. No cheating, exploits or griefing on public servers. Report exploits to the staff instead.\n"
        "4. Never share anyone's personal information.\n"
        "5. Keep each channel on topic. English in public channels.\n"
        "6. Report bugs with the **Report a bug** button in the game, or one post per bug in %s.\n"
        "7. World of Kenshi is an unofficial fan project, not made or supported by Lo-Fi Games.\n"
        "8. Staff decisions are final.\n\n"
        "Press the button below to accept the rules and see the whole server." % c("bug-reports")},
         components=[{"type": 1, "components": [{"type": 2, "style": 3, "label": "I accept the rules",
                                                 "custom_id": "wok:accept_rules"}]}])
    post("get-started", {"title": "How to install", "color": SAND, "description":
        "**You need** Kenshi 1.0.68 on Steam (your own copy).\n\n"
        "**1. Download** the latest `WorldOfKenshi-alpha-*.zip`: type `/download` or open %s\n"
        "**2. Install** — close Kenshi, extract the zip anywhere and run `install.bat`\n"
        "**3. Play** — start Kenshi from Steam and click **Multiplayer** on the title screen. Pick a server "
        "and press **Connect**. Steam signs you in, no account to make.\n\n"
        "**Updates** install themselves when Kenshi starts (Options > Multiplayer to turn it off).\n"
        "**Something broke?** Press **Report a bug** in the Multiplayer window: your logs reach the "
        "developer directly." % RELEASES}, pin=True)
    post("hosting", {"title": "Host a server", "color": GREEN, "description":
        "Anyone can run a World of Kenshi server, there is no official central server.\n\n"
        "**Windows** — download `WorldOfKenshi-server-windows-alpha-*.zip` (`/download`), set your server's "
        "name in `start_server.bat` and run it. Open **UDP 27800** and **TCP 27801** on your router.\n"
        "**Linux** — build it from the source on GitHub (`tools/vps/setup.sh` does everything).\n\n"
        "`PRIVATE=1` in `start_server.bat` keeps your server out of the public list: players join it with "
        "**Direct connect**.\n"
        "Game rates (production, building, research) default to **1.25x**: `--production-speed`, "
        "`--building-speed`, `--research-speed`."}, pin=True)
    post("coming-soon", {"title": "Coming soon: World of Kenshi server hosting", "color": SAND, "description":
        "Once World of Kenshi is ready, we plan to offer **server hosting**: a server of your own, running "
        "24/7, set up for you.

"
        "**What you would pay for: the hosting, and only the hosting.**
"
        "• The **mod is free** and stays free, for everyone, forever. Its source code is public on GitHub.
"
        "• We **do not sell Kenshi** or any copy of it. Every player needs their own copy of Kenshi on "
        "Steam, and servers check it.
"
        "• We do not sell anything that belongs to Lo-Fi Games. Kenshi is theirs; World of Kenshi is an "
        "unofficial fan project, not made or supported by them.
"
        "• Hosting your own server yourself stays **free**: the server program is free to download.

"
        "**Where the money goes: 100% back into the project.**
"
        "• Paying for the official servers' hosting.
"
        "• Development costs when the mod's development needs them (tools, test servers, anything that "
        "directly moves World of Kenshi forward).

"
        "Nothing is for sale yet. We will announce it in %s when it opens." % c("announcements")}, pin=True)
    print("done")
