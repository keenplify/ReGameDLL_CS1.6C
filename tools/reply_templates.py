"""Name-aware cache templates; placeholders bind only to explicit game context."""
import re

VERSION = "speech-v6-bounded-dialogue-variants"
TOKEN = re.compile(r"\{\{([a-z]+)\}\}")

def bindings(event):
    values = {"s": event.get("subject_name") or event.get("player_name", ""),
              "p": event.get("player_name", ""), "b": event.get("bot_name", "")}
    for value in values.values():
        if len(value.encode()) > 63 or any(ord(c) < 32 or ord(c) == 127 or c in '\";\\%{}' for c in value):
            raise ValueError("invalid template name")
    return values


def replace_names(text, event):
    values = bindings(event)
    # Prefer subject over interlocutor when they are the same person.
    names = {}
    for key in sorted(values, key=lambda k: len(values[k]), reverse=True):
        name = values[key]
        if name:
            names.setdefault(name.casefold(), key)
    if not names:
        return text
    pattern = r"(?<!\w)(?:" + "|".join(re.escape(name) for name in names) + r")(?!\w)"
    return re.sub(pattern, lambda match: "{{"+names[match[0].casefold()]+"}}", text, flags=re.IGNORECASE)



def canonical_question(event):
    return " ".join(replace_names(event["text"], event).casefold().split())


def to_template(reply, event):
    if "{{" in reply or "}}" in reply:
        raise ValueError("provider cannot inject cache placeholders")
    return replace_names(reply, event)


def render(template, event):
    values = bindings(event)
    def substitute(match):
        key = match[1]
        if key not in values or not values[key]:
            raise ValueError("unbound template subject")
        return values[key]
    result = TOKEN.sub(substitute, template)
    if "{" in result or "}" in result:
        raise ValueError("invalid template placeholder")
    return result


def event_template(event):
    variants=event_variants(event)
    if not variants:return None
    import random
    return random.choice(variants)


def event_variants(event):
    if event.get("kind")=="team_flash":
        return ["hey {{s}}, please stop blinding us","{{s}}, watch the flashes please","careful with the flashes, {{s}}"]
    if event.get("kind")=="team_damage":
        return ["{{s}}, watch your fire please","careful, {{s}}, same team","{{s}}, check your fire"]
    if event.get("kind")=="event":
        groups=[("reload",["reloading, cover me","cover, changing mag","need a second to reload"]),
                ("backup",["need backup","could use some help here","need another gun here"]),
                ("support",["coming to help","moving up to support","on my way to you"]),
                ("lost",["lost sight, watching the corner","lost contact","they moved out of sight"]),
                ("rotat",["rotating","on the rotate","switching positions"])]
        text=event["text"].casefold()
        for trigger,variants in groups:
            if text.strip(".! ") in {"reloading cover me", "reloading, cover me", "need backup", "coming to help", "lost contact", "lost sight", "rotating", "moving up to support"} and trigger in text:return variants
        return [to_template(event["text"],event)]
    return []
