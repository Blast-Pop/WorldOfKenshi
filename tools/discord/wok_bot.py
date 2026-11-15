#!/usr/bin/env python3
"""WOK Bot: the World of Kenshi community's Discord bot (English).

- Rules gate: the "I accept the rules" button in #rules gives the Drifter role.
- /download, /servers, /bug: answers only the asking player sees.
- #server-status: one message listing the live servers (from the master list), updated every minute.
- #announcements: each new GitHub release is posted once.
- Bug reports: the game's "Report a bug" button sends the player's text and logs here (HTTP POST
  /report); the bot reads the logs, picks out what matters and opens a post in the #bug-reports forum
  with everything zipped (10 MB at most). No IP address is stored or logged.

Configuration: environment (see wok-bot.env.example). Run by systemd (wok-bot.service)."""
import asyncio
import io
import json
import logging
import os
import re
import time
import zipfile

import aiohttp
import discord
from aiohttp import web
from discord import app_commands

TOKEN = os.environ["DISCORD_BOT_TOKEN"]
GUILD_ID = int(os.environ["GUILD_ID"])
MASTER_URL = os.environ.get("MASTER_URL", "http://127.0.0.1:27801").rstrip("/")
REPORT_PORT = int(os.environ.get("REPORT_PORT", "27810"))
STATE_DIR = os.environ.get("STATE_DIR", "/var/lib/wok-bot")
REPO = "Blast-Pop/WorldOfKenshi"
RELEASES_URL = "https://github.com/%s/releases" % REPO

MAX_REPORT_BYTES = 10 * 1024 * 1024        # what reaches Discord, zipped (Discord's upload limit)
MAX_UPLOAD_BYTES = 40 * 1024 * 1024        # what the game may send before zipping (logs compress well)
REPORTS_PER_ADDRESS = 3                    # per address and per 10 minutes (kept in memory only)
SAND, RUST, GREEN, GREY = 0xC8A165, 0xB0452F, 0x6B8E4E, 0x7A7468

log = logging.getLogger("wok-bot")


# ---------------------------------------------------------------- state (a small JSON file)
def load_state():
    try:
        with open(os.path.join(STATE_DIR, "state.json"), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def save_state(state):
    os.makedirs(STATE_DIR, exist_ok=True)
    tmp = os.path.join(STATE_DIR, "state.json.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(state, f)
    os.replace(tmp, os.path.join(STATE_DIR, "state.json"))


# ---------------------------------------------------------------- log analysis
IMPORTANT = re.compile(r"FAILED|NOT |error|cannot|unexpected|crash|DIFFERENT|rejected|disconnected|timed out|"
                       r"exception|refused", re.IGNORECASE)
NOISE = re.compile(r"ui: hover|perf:|stats: .* sent|inventory: sent|tags:")


def analyse(files):
    """What a developer wants first from the game's logs: version, fps, the lines that went wrong."""
    facts = {"version": None, "protocol": None, "fps_last": None, "fps_min": None, "lines": [], "crash": False}
    for name, data in files:
        lower = name.lower()
        if lower.endswith(".zip") or lower.endswith(".dmp"):
            facts["crash"] = facts["crash"] or "crash" in lower
            continue
        if not lower.startswith("wok_client") or "prev" in lower:
            continue
        text = data.decode("utf-8", "replace")
        for line in text.splitlines():
            m = re.search(r"client loaded \(protocol (\d+)\)", line)
            if m:
                facts["protocol"] = m.group(1)
            m = re.search(r"update: version ([0-9.]+)", line)
            if m:
                facts["version"] = m.group(1)
            m = re.search(r"perf: ([0-9.]+) fps", line)
            if m:
                fps = float(m.group(1))
                facts["fps_last"] = fps
                facts["fps_min"] = fps if facts["fps_min"] is None else min(facts["fps_min"], fps)
            if IMPORTANT.search(line) and not NOISE.search(line):
                facts["lines"].append(line[:180])
    seen, unique = set(), []
    for line in reversed(facts["lines"]):  # newest first, without repeats
        key = re.sub(r"^\[[0-9:.]+\] ", "", line)
        if key not in seen:
            seen.add(key)
            unique.append(line)
    facts["lines"] = list(reversed(unique[:12]))
    return facts


def zip_reports(files, limit=MAX_REPORT_BYTES):
    """All files in one zip of at most `limit` bytes: the logs lose their oldest lines if needed."""
    files = [(n, d) for n, d in files]
    for _ in range(12):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name, data in files:
                z.writestr(name, data)
        if buf.tell() <= limit:
            return buf.getvalue()
        # Too big: cut the biggest text log to its newest half (crash dumps stay whole if possible).
        i = max(range(len(files)), key=lambda k: (not files[k][0].endswith(".zip"), len(files[k][1])))
        name, data = files[i]
        files[i] = (name, b"[... older lines removed to fit Discord's 10 MB limit ...]\n" + data[len(data) // 2:])
    return None


async def health(_request):
    return web.Response(text="ok\n")


def safe_name(name):
    name = os.path.basename(name or "file")
    return re.sub(r"[^A-Za-z0-9._-]", "_", name)[:80] or "file"


# ---------------------------------------------------------------- the bot
class WokBot(discord.Client):
    def __init__(self):
        intents = discord.Intents.none()
        intents.guilds = True
        super().__init__(intents=intents)
        self.tree = app_commands.CommandTree(self)
        self.state = load_state()
        self.http_session = None
        self.recent_reports = {}  # address -> [times], memory only

    # ---- helpers
    def guild(self):
        return self.get_guild(GUILD_ID)

    def channel(self, name):
        g = self.guild()
        return discord.utils.get(g.channels, name=name) if g else None

    async def bot_log(self, text):
        ch = self.channel("bot-log")
        if ch:
            try:
                await ch.send(text[:1900], allowed_mentions=discord.AllowedMentions.none())
            except discord.HTTPException:
                pass

    async def fetch_json(self, url, headers=None):
        async with self.http_session.get(url, headers=headers or {}, timeout=aiohttp.ClientTimeout(total=15)) as r:
            if r.status != 200:
                raise RuntimeError("%s -> %d" % (url, r.status))
            return await r.json(content_type=None)

    async def servers(self):
        return await self.fetch_json(MASTER_URL + "/master/servers.json")

    async def releases(self):
        return await self.fetch_json("https://api.github.com/repos/%s/releases?per_page=5" % REPO,
                                     {"Accept": "application/vnd.github+json", "User-Agent": "WorldOfKenshiBot"})

    def servers_embed(self, servers):
        e = discord.Embed(title="World of Kenshi servers", color=SAND)
        if not servers:
            e.description = "No server is online right now."
        lines = []
        for s in sorted(servers, key=lambda s: -s.get("players", 0)):
            name = s.get("name") or "World of Kenshi server"
            count = "%d/%d players" % (s["players"], s["maxPlayers"]) if "players" in s else "players unknown"
            lines.append("**%s**\n`%s` · %s" % (discord.utils.escape_markdown(name), s["address"], count))
        if lines:
            e.description = "\n\n".join(lines)[:4000]
        total = sum(s.get("players", 0) for s in servers)
        e.set_footer(text="%d server(s), %d player(s) online · updated every minute" % (len(servers), total))
        e.timestamp = discord.utils.utcnow()
        return e

    # ---- start
    async def setup_hook(self):
        self.http_session = aiohttp.ClientSession()
        register_commands(self)
        guild = discord.Object(id=GUILD_ID)
        self.tree.copy_global_to(guild=guild)
        await self.tree.sync(guild=guild)
        app = web.Application(client_max_size=MAX_UPLOAD_BYTES)
        app.router.add_post("/report", self.handle_report)
        app.router.add_get("/health", health)
        runner = web.AppRunner(app, access_log=None)  # no access log: no addresses kept
        await runner.setup()
        await web.TCPSite(runner, "0.0.0.0", REPORT_PORT).start()
        self.loop.create_task(self.status_loop())
        self.loop.create_task(self.release_loop())

    async def on_ready(self):
        log.info("logged in as %s", self.user)

    async def on_interaction(self, interaction: discord.Interaction):
        if interaction.type != discord.InteractionType.component:
            return
        if (interaction.data or {}).get("custom_id") != "wok:accept_rules":
            return
        role = discord.utils.get(interaction.guild.roles, name="Drifter") if interaction.guild else None
        if role is None:
            await interaction.response.send_message("The Drifter role is missing: please tell the staff.",
                                                    ephemeral=True)
            return
        if role in getattr(interaction.user, "roles", []):
            await interaction.response.send_message("You already accepted the rules. Welcome back!",
                                                    ephemeral=True)
            return
        await interaction.user.add_roles(role, reason="accepted the rules")
        get_started = self.channel("get-started")
        await interaction.response.send_message(
            "Welcome to World of Kenshi! Start with %s." % (get_started.mention if get_started else "#get-started"),
            ephemeral=True)

    # ---- #server-status
    async def status_loop(self):
        await self.wait_until_ready()
        while not self.is_closed():
            try:
                await self.update_status()
            except Exception as e:  # keep going: the next minute may work
                log.warning("status: %s", e)
            await asyncio.sleep(60)

    async def update_status(self):
        ch = self.channel("server-status")
        if ch is None:
            return
        embed = self.servers_embed(await self.servers())
        mid = self.state.get("status_message")
        if mid:
            try:
                msg = await ch.fetch_message(int(mid))
                await msg.edit(embed=embed)
                return
            except discord.NotFound:
                pass
        msg = await ch.send(embed=embed)
        self.state["status_message"] = str(msg.id)
        save_state(self.state)

    # ---- #announcements
    async def release_loop(self):
        await self.wait_until_ready()
        while not self.is_closed():
            try:
                await self.check_releases()
            except Exception as e:
                log.warning("releases: %s", e)
            await asyncio.sleep(300)

    async def check_releases(self):
        releases = [r for r in await self.releases() if not r.get("draft")]
        known = set(self.state.get("releases", []))
        if not self.state.get("releases"):  # first run: remember what exists, post nothing old
            self.state["releases"] = [str(r["id"]) for r in releases]
            save_state(self.state)
            return
        ch = self.channel("announcements")
        for r in reversed(releases):
            if str(r["id"]) in known or ch is None:
                continue
            e = discord.Embed(title=r.get("name") or r["tag_name"], url=r["html_url"], color=SAND,
                              description=(r.get("body") or "")[:3800])
            e.set_footer(text="Updates install themselves when Kenshi starts.")
            await ch.send(embed=e)
            self.state.setdefault("releases", []).append(str(r["id"]))
            save_state(self.state)
            await self.bot_log("Announced release %s" % r["tag_name"])

    # ---- bug reports from the game
    def allow_report(self, address):
        now = time.time()
        times = [t for t in self.recent_reports.get(address, []) if now - t < 600]
        if len(times) >= REPORTS_PER_ADDRESS:
            self.recent_reports[address] = times
            return False
        self.recent_reports[address] = times + [now]
        if len(self.recent_reports) > 5000:
            self.recent_reports.clear()
        return True

    async def handle_report(self, request: web.Request):
        if not self.allow_report(request.remote or "?"):
            return web.Response(status=429, text="Too many reports, please wait a few minutes.\n")
        fields, files = {}, []
        reader = await request.multipart()
        async for part in reader:
            if part.filename:
                data = await part.read(decode=False)
                files.append((safe_name(part.filename), bytes(data)))
            elif part.name:
                fields[part.name] = (await part.text())[:4000]
        title = (fields.get("title") or "").strip()[:90]
        text = (fields.get("description") or "").strip()[:3500]
        if not title or not text:
            return web.Response(status=400, text="A title and a description are needed.\n")
        try:
            url = await self.post_report(title, text, fields, files)
        except Exception as e:
            log.exception("report")
            return web.Response(status=500, text="The report could not be posted: %s\n" % e)
        return web.Response(text=url + "\n")

    async def post_report(self, title, text, fields, files):
        forum = self.channel("bug-reports")
        facts = analyse(files)
        e = discord.Embed(title=title, color=RUST if facts["crash"] else SAND)
        e.add_field(name="Version", value=fields.get("version") or facts["version"] or "?", inline=True)
        e.add_field(name="Server", value=fields.get("server") or "not connected", inline=True)
        e.add_field(name="Character", value=fields.get("character") or "?", inline=True)
        if facts["fps_last"] is not None:
            e.add_field(name="FPS", value="last %.0f, lowest %.0f" % (facts["fps_last"], facts["fps_min"]), inline=True)
        e.add_field(name="Crash dump", value="attached" if facts["crash"] else "none", inline=True)
        if fields.get("contact"):
            e.add_field(name="Contact", value=fields["contact"][:100], inline=True)
        if facts["lines"]:
            body = "\n".join(facts["lines"])
            e.add_field(name="What went wrong in the log", value="```\n%s\n```" % body[-1000:], inline=False)
        e.timestamp = discord.utils.utcnow()
        attachment = None
        if files:
            data = zip_reports(files)
            if data:
                attachment = discord.File(io.BytesIO(data), filename="report-%d.zip" % int(time.time()))
        content = "%s\n\n*Sent from the game's Report a bug button.*" % text
        if isinstance(forum, discord.ForumChannel):
            tag = discord.utils.get(forum.available_tags, name="Crash" if facts["crash"] else "Bug")
            kwargs = {"name": title, "content": content[:2000], "embed": e,
                      "applied_tags": [tag] if tag else [], "allowed_mentions": discord.AllowedMentions.none()}
            if attachment:
                kwargs["file"] = attachment
            created = await forum.create_thread(**kwargs)
            url = created.thread.jump_url
        elif forum is not None:
            extra = {"file": attachment} if attachment else {}
            msg = await forum.send(content=content[:2000], embed=e, allowed_mentions=discord.AllowedMentions.none(),
                                   **extra)
            url = msg.jump_url
        else:
            raise RuntimeError("no #bug-reports channel")
        await self.bot_log("Bug report: %s (%s)" % (title, url))
        return url


def register_commands(bot: WokBot):
    @bot.tree.command(name="download", description="Get the latest World of Kenshi version")
    async def download(interaction: discord.Interaction):
        try:
            latest = next(r for r in await bot.releases() if not r.get("draft"))
        except Exception:
            await interaction.response.send_message("GitHub did not answer, here is the page: " + RELEASES_URL,
                                                    ephemeral=True)
            return
        e = discord.Embed(title=latest.get("name") or latest["tag_name"], url=latest["html_url"], color=SAND)
        links = []
        for a in latest.get("assets", []):
            if a["name"].endswith(".zip"):
                kind = "Server (Windows)" if "server" in a["name"] else "Game mod"
                links.append("**%s**: [%s](%s)" % (kind, a["name"], a["browser_download_url"]))
        e.description = "\n".join(links) + "\n\nClose Kenshi, extract the zip and run `install.bat`."
        await interaction.response.send_message(embed=e, ephemeral=True)

    @bot.tree.command(name="servers", description="World of Kenshi servers online right now")
    async def servers(interaction: discord.Interaction):
        try:
            embed = bot.servers_embed(await bot.servers())
        except Exception:
            await interaction.response.send_message("The server list is not reachable right now.", ephemeral=True)
            return
        await interaction.response.send_message(embed=embed, ephemeral=True)

    @bot.tree.command(name="bug", description="How to report a bug")
    async def bug(interaction: discord.Interaction):
        forum = bot.channel("bug-reports")
        await interaction.response.send_message(
            "The fastest way: press **Report a bug** in the game's **Multiplayer** window. Your description and "
            "logs reach %s directly.\nWithout the game: open a post in %s with what you did, what happened and "
            "your `Kenshi\\WorldOfKenshi\\wok_client.log`." % (
                forum.mention if forum else "#bug-reports", forum.mention if forum else "#bug-reports"),
            ephemeral=True)


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    logging.getLogger("discord").setLevel(logging.WARNING)
    logging.getLogger("aiohttp.access").setLevel(logging.WARNING)
    WokBot().run(TOKEN, log_handler=None)


if __name__ == "__main__":
    main()
