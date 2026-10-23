#!/usr/bin/env python3
"""Standalone DeepSeek sidecar with an optional 1.6 Competitive provider adapter."""
import argparse
import hashlib
import json
from urllib.request import Request, urlopen
import sqlite3
import importlib
import os
from pathlib import Path
import re
import random
from collections import deque
import sys
import tempfile
import time
from urllib.parse import urlparse
from urllib.error import HTTPError
import reply_templates as templates


def parse_request(raw):
    if len(raw) > 1024 or b"\0" in raw:
        raise ValueError("invalid request size")
    fields = raw.decode("utf-8").rstrip("\n").split("\t")
    if len(fields) not in (11, 15, 16):
        raise ValueError("invalid field count")
    version, token, bot, player, channel, mode, team, map_name, hp, money, text = fields[:11]
    kind, player_name, bot_name, subject_name = fields[11:15] if len(fields)>=15 else ("chat", "", "", "")
    if kind not in ("chat", "event", "team_flash", "team_damage"):
        raise ValueError("invalid event kind")
    if (version, len(fields)) not in (("1", 11), ("2", 15), ("3", 16)) or not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{8}-[0-9]+", token):
        raise ValueError("invalid protocol/token")
    if not (0 <= int(bot) < 32 and 0 <= int(player) < 32 and 1 <= int(team) <= 2):
        raise ValueError("invalid player")
    if channel not in ("team", "all") or mode not in ("teams", "ffa"):
        raise ValueError("invalid channel/mode")
    if not re.fullmatch(r"[a-zA-Z0-9_-]{1,64}", map_name) or not 0 < len(text.encode()) <= 96:
        raise ValueError("invalid map/text")
    if any(ord(c) < 32 or ord(c) == 127 for c in text):
        raise ValueError("invalid text")
    event = dict(kind=kind, player_name=player_name, bot_name=bot_name, subject_name=subject_name, bot_slot=int(bot), player_slot=int(player),
        channel=channel, mode=mode, team=int(team), map=map_name, text=text,
        bot_tags_on=True, phase="playing", place="unknown", weapon="unknown",
        hp=int(hp), money=int(money), friends_alive=0, enemies_alive=0,
        bomb_planted=False, round_seconds=0, visible_count=0, visible_slots=[])
    context = fields[15] if len(fields)==16 else ""
    if context and not re.fullmatch(r"[0-9]+-[0-9]+-[0-9]+-[01]-[01]",context):
        raise ValueError("invalid conversation fence")
    event["context"] = context
    templates.bindings(event)
    return token, event


def apply_roster_style(queue, event):
    path = queue.parent.parent/"amxmodx/data/16c_ai_bot_identity.txt"
    try:
        if path.stat().st_size > 4096:
            return
        for line in path.read_text(encoding="ascii").splitlines():
            match = re.fullmatch(r"(\d{1,2}) ([A-Za-z0-9_]{1,31}) (normal|careful|rusher) ([A-Z]{2}) (normal|talkative|rude|sweet|quiet|dry)", line)
            if match and int(match[1]) == event["bot_slot"] and match[2] == event["bot_name"]:
                event["chat_style"] = match[5]
                return
    except (OSError, UnicodeError):
        pass


def publish(directory, token, reply):
    if not isinstance(reply, str) or not 0 < len(reply.encode()) <= 90:
        raise ValueError("invalid reply length")
    if any(ord(c) < 32 or ord(c) == 127 or c in '\";\\%' for c in reply):
        raise ValueError("invalid reply characters")
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=directory,
                                     prefix=".reply-", delete=False) as f:
        temp = Path(f.name)
        f.write(f"{token}\t{reply}\n")
        f.flush()
        os.fsync(f.fileno())
    try:
        os.replace(temp, directory / "reply.tsv")
    finally:
        temp.unlink(missing_ok=True)


class RemoteCache:
    """Optional authenticated host cache; failures fall back to the private local cache."""
    def __init__(self):
        self.url = os.environ.get("BOT_TEMPLATE_CACHE_URL", "")
        self.token = os.environ.get("YAPB_CACHE_TOKEN", "")
        self.previous = {}
        url = urlparse(self.url)
        if self.url and not (url.scheme == "https" or url.scheme == "http" and url.hostname in ("127.0.0.1", "localhost", "::1")):
            raise ValueError("cache requires HTTPS or loopback HTTP")

    def call(self, key, template=None):
        if not self.url or not self.token:
            return None
        try:
            data = json.dumps(dict(key=key, template=template)).encode() if template is not None else None
            request = Request(self.url + ("?key="+key if data is None else ""), data=data,
                              headers={"Authorization": "Bearer "+self.token, "Content-Type": "application/json"})
            with urlopen(request, timeout=.4) as response:
                result = json.loads(response.read(32769))
            if not isinstance(result,dict):return None
            variants = result.get("variants", [])
            if not isinstance(variants,list):return None
            valid = [v for v in variants[:1000] if isinstance(v,str) and 0<len(v.encode())<=150]
            options = [v for v in valid if v != self.previous.get(key)] or valid
            selected = random.choice(options) if options else None
            self.previous[key] = selected
            return selected
        except (OSError, ValueError, TypeError):
            return None


class CostGuard:
    """Per-instance, restart-persistent paid-attempt budget and bounded reply cache."""
    def __init__(self, directory, hourly_limit=0, daily_limit=0, min_interval=0):
        self.hourly_limit = hourly_limit
        self.daily_limit = daily_limit
        self.min_interval = min_interval
        path = directory / "ai_cost.sqlite3"
        self.db = sqlite3.connect(path)
        if os.name == "posix":
            path.chmod(0o600)
        self.db.execute("CREATE TABLE IF NOT EXISTS attempts (at REAL NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS replies (key TEXT PRIMARY KEY, reply TEXT NOT NULL, at REAL NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS reply_variants (key TEXT, reply TEXT, at REAL, PRIMARY KEY(key,reply))")
        self.db.commit()
        self.previous = {}

    @staticmethod
    def key(event, settings):
        fields = ["speech-v5-independent-opinions", settings["api_url"], settings["model"], event["map"], event["mode"],
                  event["channel"], event["kind"], event.get("chat_style", "normal"), templates.canonical_question(event)]
        if event.get("dialogue_hash"):
            fields += ["bounded-dialogue-v1", event["dialogue_hash"]]
        return hashlib.sha256("\0".join(fields).encode()).hexdigest()

    def cached(self, key, now):
        variants = [row[0] for row in self.db.execute("SELECT reply FROM reply_variants WHERE key=? AND at>? ORDER BY at DESC LIMIT 8", (key,now-86400))]
        if not variants:
            row=self.db.execute("SELECT reply FROM replies WHERE key=? AND at>?",(key,now-86400)).fetchone()
            return row[0] if row else None
        choices=[v for v in variants if v != self.previous.get(key)] or variants
        selected=random.choice(choices)
        self.previous[key]=selected
        return selected

    def reserve(self, now):
        with self.db:
            self.db.execute("DELETE FROM attempts WHERE at<=?", (now-86400,))
            times = [row[0] for row in self.db.execute("SELECT at FROM attempts")]
            if (self.daily_limit and len(times) >= self.daily_limit) or (self.hourly_limit and sum(at>now-3600 for at in times) >= self.hourly_limit):
                return False
            if times and now-max(times) < self.min_interval:
                return False
            self.db.execute("INSERT INTO attempts VALUES (?)", (now,))
        return True

    def remember(self, key, reply, now):
        with self.db:
            self.db.execute("DELETE FROM reply_variants WHERE at<=?",(now-86400,))
            self.db.execute("INSERT OR REPLACE INTO reply_variants VALUES (?,?,?)",(key,reply,now))
            self.db.execute("DELETE FROM reply_variants WHERE key=? AND reply NOT IN (SELECT reply FROM reply_variants WHERE key=? ORDER BY at DESC LIMIT 8)",(key,key))
            self.db.execute("DELETE FROM reply_variants WHERE rowid NOT IN (SELECT rowid FROM reply_variants ORDER BY at DESC LIMIT 2048)")
            self.db.execute("DELETE FROM replies WHERE at<=?", (now-86400,))
            self.db.execute("INSERT OR REPLACE INTO replies VALUES (?,?,?)", (key, reply, now))
            self.db.execute("DELETE FROM replies WHERE key NOT IN (SELECT key FROM replies ORDER BY at DESC LIMIT 256)")


class Conversation:
    """Two short turns in memory only; scope includes native round/life/identity fence."""
    def __init__(self, turns=2):
        self.turns = turns
        self.rows = {}
    def key(self,event):
        return tuple(event.get(k,"") for k in ("map","mode","team","channel","bot_slot","player_slot","bot_name","player_name","context"))
    def dialogue(self,event,now):
        self.rows={k:v for k,v in self.rows.items() if now-v[0]<120}
        if not self.turns or not event.get("context") or event["kind"]!="chat":return []
        result=list(self.rows.get(self.key(event),(now,[]))[1])
        if result:
            canonical=[dict(role=m["role"],content=templates.replace_names(m["content"],event)) for m in result]
            event["dialogue_hash"]=hashlib.sha256(json.dumps(canonical,sort_keys=True).encode()).hexdigest()
        return result
    def remember(self,event,reply,now):
        if not self.turns or not event.get("context") or event["kind"]!="chat":return
        history=self.dialogue(event,now)
        history += [dict(role="user",content=event["text"][:60]),dict(role="assistant",content=reply[:60])]
        self.rows[self.key(event)]=(now,history[-self.turns*2:])
        while len(self.rows)>64:self.rows.pop(next(iter(self.rows)))


def error_message(error):
    if isinstance(error, HTTPError):
        return {
            401: "DeepSeek authentication failed (HTTP 401). Run --setup-key with a valid API key, then restart this sidecar.",
            402: "DeepSeek account has insufficient balance (HTTP 402). Check your provider billing.",
            403: "Provider access denied (HTTP 403). Check your API key permissions.",
            429: "Provider rate limit reached (HTTP 429). Wait before retrying.",
        }.get(error.code, "Provider HTTP error " + str(error.code) + ". Check provider availability/model settings.")
    if isinstance(error, TimeoutError):
        return "Provider timed out; the game will use its local fallback."
    reasons = {
        "invalid reply length": "AI reply exceeds the game limit of 90 UTF-8 bytes or is empty.",
        "invalid reply characters": "AI reply contains unsupported or unsafe characters.",
        "only speech is supported": "AI returned an action instead of speech; native bots own actions.",
        "missing reply text": "Provider returned no speech text.",
        "malformed reply JSON": "Provider returned incomplete JSON instead of speech.",
        "provider reply too large": "Provider response exceeds the bounded response size.",
    }
    if isinstance(error, ValueError) and str(error) in reasons:
        codes = getattr(error, "codepoints", ())
        detail = " (" + ", ".join("U+%04X" % code for code in codes) + ")" if codes else ""
        return reasons[str(error)] + detail + " The game will use its local fallback."
    if isinstance(error, (ValueError, UnicodeError, KeyError, TypeError)):
        return "AI request or provider reply failed validation; the game will use its local fallback."
    return "AI transport failed; check network access and queue directory permissions."


def exit_with_supervisor():
    parent = os.environ.get("YAPB_AI_PARENT_PID")
    if not parent:
        return
    if sys.platform == "linux":
        import ctypes, signal
        if ctypes.CDLL(None).prctl(1, signal.SIGTERM, 0, 0, 0) != 0:
            raise OSError("could not bind to supervisor")
    if os.getppid() != int(parent):
        raise SystemExit(0)


def main():
    exit_with_supervisor()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history-turns",type=int,choices=range(0,3),default=2,help="bounded in-memory conversation turns (0..2)")
    parser.add_argument("--backend-tools", type=Path, help="optional 16competitive/tools/yapb-ai directory")
    parser.add_argument("--queue-dir", type=Path)
    parser.add_argument("--setup-key", action="store_true", help="privately prompt for your API key and exit")
    parser.add_argument("--env-file", type=Path)
    parser.add_argument("--hourly-limit", type=int, default=0, help="paid attempts/hour; 0 means uncapped")
    parser.add_argument("--daily-limit", type=int, default=0, help="paid attempts/day; 0 means uncapped")
    parser.add_argument("--min-interval", type=float, default=0, help="optional paid-request spacing in seconds")
    args = parser.parse_args()
    if args.hourly_limit < 0 or args.daily_limit < 0 or not 0 <= args.min_interval <= 86400:
        parser.error("limits must be non-negative; interval must be at most 86400 seconds")
    credentials = args.env_file or Path.home() / ".config/regamedll/deepseek.env"
    if args.setup_key:
        import getpass
        key = getpass.getpass("DeepSeek API key (hidden): ").strip()
        if not key or any(ord(c) < 33 or ord(c) > 126 or c in "\\\"'" for c in key):
            parser.error("invalid API key")
        credentials.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", dir=credentials.parent, delete=False) as f:
            temporary = Path(f.name)
            f.write("DEEPSEEK_API_KEY=" + key + "\n")
            f.flush()
            os.fsync(f.fileno())
        os.replace(temporary, credentials)
        print("Saved credentials to", credentials)
        return
    if not args.queue_dir:
        parser.error("--queue-dir is required unless using --setup-key")
    if args.backend_tools:
        sys.path.insert(0, str(args.backend_tools.resolve()))
        provider = importlib.import_module("ai_sidecar")
    else:
        provider = importlib.import_module("standalone_ai_provider")
    # Native speech uses the native 90-byte validator even with a backend adapter.
    from standalone_ai_provider import validate_model_choice
    # Use the same private-file parser in either mode; environment takes precedence.
    from standalone_ai_provider import get_settings, ask_provider as request_speech, local_scope_reply
    settings = get_settings(credentials, args.env_file is not None)
    url = urlparse(settings["api_url"])
    if not settings["api_key"]:
        parser.error("set DEEPSEEK_API_KEY or YAPB_AI_API_KEY in the bridge environment")
    if not url.hostname or url.username or url.password or not (
        url.scheme == "https" or url.scheme == "http" and url.hostname in ("localhost", "127.0.0.1", "::1")):
        parser.error("provider requires HTTPS or loopback HTTP")
    if url.hostname == "api.deepseek.com":
        settings["model"] = "deepseek-flash"  # Cheapest documented DeepSeek tier; no paid upgrade fallback.
    queue = args.queue_dir.resolve()
    queue.mkdir(parents=True, mode=0o700, exist_ok=True)
    if os.name == "posix":
        queue.chmod(0o700)
    try:
        lock = provider.acquire_instance_lock(queue)
    except BlockingIOError:
        raise SystemExit(75)  # Another worker owns this mailbox; retry quietly later.
    seen = None
    costs = CostGuard(queue, args.hourly_limit, args.daily_limit, args.min_interval)
    remote = RemoteCache()
    conversations = Conversation(args.history_turns)
    last_error = None
    if not (args.hourly_limit or args.daily_limit or args.min_interval):
        print("[ReGameDLL AI] ready; requests uncapped, Flash model, short replies, cached replies free.", flush=True)
    else:
        print(f"[ReGameDLL AI] ready; optional limits: {args.hourly_limit or 'unlimited'}/hour, "
              f"{args.daily_limit or 'unlimited'}/day, {args.min_interval:g}s spacing.", flush=True)
    while True:
        try:
            request = queue / "request.tsv"
            if request.is_symlink() or time.time() - request.stat().st_mtime > 7:
                time.sleep(0.1)
                continue
            with request.open("rb") as f:
                raw = f.read(1025)
            token, event = parse_request(raw)
            if token == seen:
                time.sleep(0.1)
                continue
            seen = token
            apply_roster_style(queue, event)
            now = time.time()
            dialogue = conversations.dialogue(event,now)
            event["dialogue"] = dialogue
            cache_key = costs.key(event, settings)
            # The database is shared across matches; private SQLite is the offline fallback.
            remote_template = remote.call(cache_key)
            cached_template = remote_template or costs.cached(cache_key, now)
            if remote_template is None and cached_template is not None:
                remote.call(cache_key, cached_template)
            cached = templates.render(cached_template, event) if cached_template is not None else None
            fixed = templates.event_template(event)
            if fixed:
                for variant in templates.event_variants(event):
                    costs.remember(cache_key,variant,now)
                    if remote_template is None:remote.call(cache_key,variant)
                fixed = costs.cached(cache_key,now)
            local = templates.render(fixed, event) if fixed else local_scope_reply(event)
            if local is not None:
                choice = {"reply": local, "action": "none"}
            elif cached is not None:
                choice = {"reply": cached, "action": "none"}
            else:
                if not costs.reserve(now):
                    message = "AI economy limit reached; using local fallback (no API call)."
                    if last_error != message:
                        print(message, flush=True)
                        last_error = message
                    continue
                choice = request_speech(event, settings["api_key"], settings["api_url"], settings["model"], 6)
            validate_model_choice(choice, event)
            if cached is None and local is None:
                template = templates.to_template(choice["reply"], event)
                costs.remember(cache_key, template, now)
                remote.call(cache_key, template)
            # AI only supplies speech. Native parser owns tactical actions.
            if request.read_bytes() != raw or time.time() - request.stat().st_mtime > 7:
                continue
            publish(queue, token, choice["reply"])
            conversations.remember(event,choice["reply"],now)
            last_error = None
            print("AI reply delivered to game queue (local; no API call)." if local is not None else "AI reply delivered to game queue (cached; no API call)." if cached is not None else "AI reply delivered to game queue (paid request).", flush=True)
        except FileNotFoundError:
            time.sleep(0.2)
        except (UnicodeError, ValueError, OSError, KeyError, TypeError, sqlite3.Error) as error:
            # Only fixed messages/status codes: never credentials, bodies, or chat.
            message = error_message(error)
            if message != last_error:
                print(message, file=sys.stderr, flush=True)
                last_error = message
            time.sleep(0.2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
