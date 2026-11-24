#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
"""Build the jmdict.db SQLite database used by tap-to-lookup.

Input is the EDRDG JMdict_e file (XML, gzip or plain). The DTD is embedded in
the file, so no network access is needed after downloading the XML.

Example:
    curl -O https://www.edrdg.org/pub/Nihongo/JMdict_e.gz
    python3 build_jmdict.py --input JMdict_e.gz --output assets/dict/jmdict.db

JMdict is copyright the Electronic Dictionary Research and Development Group
and is distributed under CC BY-SA 4.0. See assets/dict/README.md.
"""

from __future__ import annotations

import argparse
import gzip
import os
import sqlite3
import sys
import time
import xml.etree.ElementTree as ET

SCHEMA = """
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE entries (
    id  INTEGER PRIMARY KEY,
    seq INTEGER NOT NULL UNIQUE
);
CREATE TABLE words (
    id       INTEGER PRIMARY KEY,
    entry_id INTEGER NOT NULL REFERENCES entries(id),
    surface  TEXT NOT NULL,
    reading  TEXT NOT NULL DEFAULT '',
    common   INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE glosses (
    entry_id    INTEGER NOT NULL REFERENCES entries(id),
    sense_order INTEGER NOT NULL DEFAULT 0,
    pos         TEXT NOT NULL DEFAULT '',
    text        TEXT NOT NULL,
    lang        TEXT NOT NULL DEFAULT 'eng'
);
"""

INDEXES = """
CREATE INDEX idx_words_surface ON words(surface);
CREATE INDEX idx_words_entry ON words(entry_id);
CREATE INDEX idx_glosses_entry ON glosses(entry_id, sense_order);
"""

XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"


def open_xml(path: str):
    if path.endswith(".gz"):
        return gzip.open(path, "rb")
    return open(path, "rb")


def build(input_path: str, output_path: str, limit: int | None) -> None:
    started = time.time()
    if os.path.exists(output_path):
        os.remove(output_path)

    connection = sqlite3.connect(output_path)
    connection.executescript(SCHEMA)
    connection.execute("PRAGMA journal_mode=OFF")
    connection.execute("PRAGMA synchronous=OFF")

    entries = 0
    words = 0
    glosses = 0
    pending_words = []
    pending_glosses = []

    stream = open_xml(input_path)
    try:
        context = ET.iterparse(stream, events=("end",))
        for _, element in context:
            if element.tag != "entry":
                continue

            ent_seq = element.findtext("ent_seq") or ""
            kanji = []
            readings = []
            common = 0
            for k_ele in element.findall("k_ele"):
                keb = (k_ele.findtext("keb") or "").strip()
                if keb:
                    kanji.append(keb)
                if k_ele.find("ke_pri") is not None:
                    common = 1
            for r_ele in element.findall("r_ele"):
                reb = (r_ele.findtext("reb") or "").strip()
                if reb:
                    readings.append(reb)
                if r_ele.find("re_pri") is not None:
                    common = 1

            senses = []
            for sense_order, sense in enumerate(element.findall("sense")):
                pos = ",".join(
                    (pos_element.text or "").strip()
                    for pos_element in sense.findall("pos")
                    if (pos_element.text or "").strip()
                )
                sense_glosses = []
                for gloss in sense.findall("gloss"):
                    text = (gloss.text or "").strip()
                    if not text:
                        continue
                    sense_glosses.append((pos, text, gloss.get(XML_LANG, "eng")))
                if sense_glosses:
                    senses.append((sense_order, sense_glosses))

            if not readings:
                element.clear()
                continue

            entry_id = entries + 1
            first_reading = readings[0]
            seen = set()
            for surface in kanji:
                key = (surface, first_reading)
                if key in seen:
                    continue
                seen.add(key)
                pending_words.append((entry_id, surface, first_reading, common))
                words += 1
            for reading in readings:
                key = (reading, reading)
                if key in seen:
                    continue
                seen.add(key)
                pending_words.append((entry_id, reading, reading, common))
                words += 1

            for sense_order, sense_glosses in senses:
                for pos, text, lang in sense_glosses:
                    pending_glosses.append((entry_id, sense_order, pos, text, lang))
                    glosses += 1

            connection.execute("INSERT INTO entries(id, seq) VALUES (?, ?)",
                               (entry_id, int(ent_seq) if ent_seq.isdigit() else entry_id))
            entries += 1
            if len(pending_words) >= 20000:
                connection.executemany(
                    "INSERT INTO words(entry_id, surface, reading, common) VALUES (?, ?, ?, ?)",
                    pending_words)
                pending_words.clear()
            if len(pending_glosses) >= 20000:
                connection.executemany(
                    "INSERT INTO glosses(entry_id, sense_order, pos, text, lang) VALUES (?, ?, ?, ?, ?)",
                    pending_glosses)
                pending_glosses.clear()

            element.clear()
            if limit is not None and entries >= limit:
                break
    finally:
        stream.close()

    if pending_words:
        connection.executemany(
            "INSERT INTO words(entry_id, surface, reading, common) VALUES (?, ?, ?, ?)",
            pending_words)
    if pending_glosses:
        connection.executemany(
            "INSERT INTO glosses(entry_id, sense_order, pos, text, lang) VALUES (?, ?, ?, ?, ?)",
            pending_glosses)

    connection.executescript(INDEXES)
    connection.executemany("INSERT INTO meta(key, value) VALUES (?, ?)", [
        ("schema_version", "1"),
        ("source", "JMdict_e"),
        ("license", "CC BY-SA 4.0 (EDRDG)"),
        ("generated_at", time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())),
        ("entry_count", str(entries)),
    ])
    connection.commit()
    connection.execute("VACUUM")
    connection.close()

    size_mb = os.path.getsize(output_path) / (1024 * 1024)
    print(f"entries={entries} words={words} glosses={glosses} "
          f"size={size_mb:.1f}MB time={time.time() - started:.1f}s -> {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="JMdict_e.gz or JMdict_e XML")
    parser.add_argument("--output", required=True, help="output SQLite database")
    parser.add_argument("--limit", type=int, default=None, help="entry limit for a sample DB")
    args = parser.parse_args()
    build(args.input, args.output, args.limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
