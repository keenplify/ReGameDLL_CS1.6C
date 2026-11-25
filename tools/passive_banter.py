"""Read the free passive-chat catalog and address its shared PostgreSQL cache buckets."""
import hashlib
from itertools import combinations
import json
import os
from pathlib import Path
import random
from collections import deque


def buckets():
    catalog = json.loads(Path(__file__).with_name("passive_banter.json").read_text(encoding="utf-8"))
    for mode, lines in catalog["short"].items():
        yield mode, "short", mode, lines
    for topic in catalog["topics"]:
        for number, frame in enumerate(topic["frames"]):
            lines = [frame.format(a=a, b=b) for a, b in combinations(topic["options"], 2)]
            yield topic["mode"], topic["id"], str(number), lines


def cache_key(mode, topic, frame):
    return hashlib.sha256(f"regamedll/passive/v1/{mode}/{topic}/{frame}".encode()).hexdigest()


class PassiveBanter:
    def __init__(self):
        self.topics = {}
        for mode, topic, frame, lines in buckets():
            self.topics.setdefault((mode, topic), []).append((cache_key(mode, topic, frame), lines))
        self.decks = {}
        self.last = {}
        self.recent = deque(maxlen=128)
        self.turn = random.randrange(3)
        self.snapshot = os.environ.get("BOT_PASSIVE_SNAPSHOT")
        self.stamp = None
        self.shared = {}

    def refresh(self):
        if not self.snapshot:
            return
        from passive_sync import validate_groups
        path = Path(self.snapshot)
        try:
            stat = path.stat()
            stamp = (stat.st_ino, stat.st_mtime_ns, stat.st_size)
            if self.stamp == stamp or stat.st_size > 12_000_000:
                return
            with path.open("rb") as stream:
                data = stream.read(12_000_001)
            if len(data) > 12_000_000:
                return
            groups = json.loads(data)
            validate_groups(groups, [key for frames in self.topics.values() for key, _ in frames])
        except (OSError, ValueError, TypeError):
            return
        self.shared = groups
        self.stamp = stamp

    def draw(self, name, choices):
        deck = self.decks.setdefault(name, [])
        if not deck:
            deck.extend(choices)
            random.shuffle(deck)
            if len(deck) > 1 and deck[-1] == self.last.get(name):
                deck[0], deck[-1] = deck[-1], deck[0]
        self.last[name] = deck.pop()
        return self.last[name]

    def choose(self, winter, remote):
        mode = "winter" if winter else "classic"
        allowed = [pair for pair in self.topics if pair[0] in ("any", mode)]
        self.turn += 1
        if self.turn % 3 == 0:
            topic = self.draw(("short", mode), [pair for pair in allowed if pair[1] == "short"])
        else:
            topic = self.draw(("topics", mode), [pair for pair in allowed if pair[1] != "short"])
        groups = self.topics[topic]
        index = self.draw(("frames", topic), range(len(groups)))
        key, lines = groups[index]
        self.refresh()
        lines = self.shared.get(key, lines)
        selected = None if self.snapshot else remote.call(key)
        if selected not in lines or selected in self.recent:
            fresh = [line for line in lines if line not in self.recent]
            if fresh:
                selected = random.choice(fresh)
            else:
                last_seen = {line: index for index, line in enumerate(self.recent)}
                selected = min(lines, key=last_seen.get)
        self.recent.append(selected)
        return selected
