# -*- coding: utf-8 -*-
# Generator data/pl_items.json: auto-podsumowanie PL ze statow/tagow
# + oczyszczone EN efektow; pole "pl" doklejane ze slownika tlumaczen.
#
# Regeneracja po patchu (tlumaczenia przenosza sie ze starego pliku;
# nowe/zmienione efekty dostana puste "pl" => program pokaze "en"):
#   curl -s https://omeda.city/items.json -o /tmp/items.json
#   python3 tools/gen_pl_items.py /tmp/items.json data/pl_items.json data/pl_items.json
import json, re, sys, html

STAT_PL = {
    "physical_power": ("moc fizyczna", "+{v:g}"),
    "magical_power": ("moc magiczna", "+{v:g}"),
    "physical_penetration": ("przebicie pancerza", "+{v:g}"),
    "magical_penetration": ("przebicie odpornosci mag.", "+{v:g}"),
    "attack_speed": ("szybkosc ataku", "+{v:g}%"),
    "critical_chance": ("szansa na kryt", "+{v:g}%"),
    "ability_haste": ("przyspieszenie umiejetnosci", "+{v:g}"),
    "lifesteal": ("kradziez zycia", "+{v:g}%"),
    "omnivamp": ("omniwamp", "+{v:g}%"),
    "magical_lifesteal": ("magiczna kradziez zycia", "+{v:g}%"),
    "heal_shield_increase": ("moc leczenia/tarcz", "+{v:g}%"),
    "max_health": ("zdrowie", "+{v:g}"),
    "physical_armor": ("pancerz", "+{v:g}"),
    "magical_armor": ("odpornosc magiczna", "+{v:g}"),
    "max_mana": ("mana", "+{v:g}"),
    "mana_regeneration": ("regeneracja many", "+{v:g}%"),
    "health_regeneration": ("regeneracja zdrowia", "+{v:g}%"),
    "gold_per_second": ("zloto/s", "+{v:g}"),
    "tenacity": ("nieustepliwosc", "+{v:g}%"),
    "movement_speed": ("szybkosc ruchu", "+{v:g}"),
    "cooldown_reduction": ("redukcja odnowien", "+{v:g}%"),
}
TAG_PL = {
    "AntiHeal": "ogranicza leczenie wrogow",
    "AntiTank": "przebija pancernych",
    "AntiMagic": "chroni przed magia",
    "AntiCrit": "oslabia krytyki",
    "AntiBurst": "chroni przed burstem",
    "PhysicalShred": "kruszy pancerz",
    "Burst": "wzmacnia burst",
    "Sustain": "daje sustain",
    "Mobility": "daje mobilnosc",
    "Offense": "ofensywny",
    "Defense": "defensywny",
    "SpellShield": "tarcza na umiejetnosc",
    "MultiKill": "nagradza multi-kille",
    "Utility": "uzytkowy",
}
SLOT_PL = {"Passive": "pasywny", "Active": "aktywny", "Crest": "crest", "Trinket": "trinket"}

def clean(t):
    if not t: return ""
    t = html.unescape(t)
    t = re.sub(r"<br\s*/?>", " ", t)
    t = re.sub(r"<img[^>]*>", "", t)
    t = re.sub(r"<[^>]+>", "", t)
    t = t.replace("•", "-")
    t = re.sub(r"\s+", " ", t).strip()
    return t

def summary(it):
    parts = []
    slot = SLOT_PL.get(it.get("slot_type") or "", "")
    tag = TAG_PL.get(it.get("aggression_type") or "", "")
    head = "Item " + slot if slot else "Item"
    if tag: head += f" ({tag})"
    parts.append(head)
    stats = it.get("stats") or {}
    frag = []
    for k, v in stats.items():
        if not isinstance(v, (int, float)) or v == 0: continue
        name, fmt = STAT_PL.get(k, (k.replace("_", " "), "+{v:g}"))
        frag.append(fmt.format(v=v) + " " + name)
    if frag: parts.append(", ".join(frag))
    hc = it.get("hero_class")
    if hc and hc not in ("None",): parts.append(f"tylko klasa {hc}")
    return "; ".join(parts) + "."

def load_translations(path):
    # Dwa formaty: goly slownik {slug: {efekt: pl}} albo poprzedni
    # pl_items.json (klucz "items") — wtedy wyciagamy istniejace "pl".
    d = json.load(open(path))
    if "items" not in d:
        return d
    tr = {}
    for slug, e in d["items"].items():
        for ef in e.get("effects", []):
            if ef.get("pl"):
                tr.setdefault(slug, {})[ef.get("name", "")] = ef["pl"]
    return tr

def main(items_path, translations_path, out_path):
    items = json.load(open(items_path))
    tr = load_translations(translations_path) if translations_path else {}
    out = {"_comment": "Spolszczenie itemow (generowane + tlumaczenia PL). "
                       "Edytuj pole 'pl'; puste 'pl' => program pokaze 'en'. "
                       "Regeneracja po patchu: tools/gen_pl_items.py.",
           "items": {}}
    n_tr = 0
    for it in sorted(items, key=lambda x: x.get("slug") or ""):
        if (it.get("total_price") or 0) <= 0: continue
        slug = it.get("slug") or str(it.get("id"))
        entry = {"name": it.get("display_name") or "", "summary_pl": summary(it), "effects": []}
        for e in (it.get("effects") or []):
            name = e.get("name") or ""
            en = clean((e.get("condition") or "") + " " + (e.get("menu_description") or ""))
            pl = tr.get(slug, {}).get(name, "")
            if pl: n_tr += 1
            eff = {"name": name, "pl": pl, "en": en}
            if e.get("active"): eff["active"] = True
            if e.get("cooldown"): eff["cooldown"] = e["cooldown"]
            entry["effects"].append(eff)
        out["items"][slug] = entry
    json.dump(out, open(out_path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    total_eff = sum(len(v["effects"]) for v in out["items"].values())
    print(f"items: {len(out['items'])}, effects: {total_eff}, translated: {n_tr}")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 3 else None, sys.argv[-1])
