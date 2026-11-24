# JMdict database

`jmdict.db` is a SQLite conversion of **JMdict**, the Japanese-Multilingual
Dictionary maintained by the Electronic Dictionary Research and Development
Group (EDRDG).

- Source: <https://www.edrdg.org/pub/Nihongo/JMdict_e.gz>
- License: **Creative Commons Attribution-ShareAlike 4.0 International**
  (CC BY-SA 4.0). See <https://www.edrdg.org/edrdg/licence.html>.
- The converted database is a derivative work and is distributed under the same
  license. It is not covered by the AGPL-3.0 license of chiaki-ng/translator.

The repository contains only `jmdict.sample.db` (the first 2000 entries, ~1 MB)
for tests and development. Build the full database before packaging:

```bash
curl -O https://www.edrdg.org/pub/Nihongo/JMdict_e.gz
python3 translator_core/tools/build_jmdict.py \
  --input JMdict_e.gz --output assets/dict/jmdict.db
```

Schema: `schema.sql`. Rows: `entries` (one per JMdict entry), `words` (kanji and
kana surface forms with readings), `glosses` (senses, POS, English glosses).

The lookup layer uses `words.surface` for exact and prefix matching, and a small
deinflection table for common verb/adjective endings (食べた → 食べる).

`jmdict.sample.db` SHA-256:
`2f870cbb2a04887d4fa1cb059fd0299139e6a9483d0c0bbd2494304087683ce0`
