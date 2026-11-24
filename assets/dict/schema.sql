-- schema_version 1
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
CREATE INDEX idx_words_surface ON words(surface);
CREATE INDEX idx_words_entry ON words(entry_id);
CREATE INDEX idx_glosses_entry ON glosses(entry_id, sense_order);
