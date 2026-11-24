// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/DictLookup.hpp>

#include <sqlite3.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace trans;

namespace {

void Exec(sqlite3 *db, const char *sql)
{
	char *error = nullptr;
	if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK)
	{
		const std::string message = error ? error : "unknown sqlite error";
		sqlite3_free(error);
		throw ::tst::Failure("sqlite exec failed: " + message);
	}
}

std::string BuildFixtureDatabase()
{
	const std::string path = "/tmp/translator_core_jmdict_fixture.db";
	std::remove(path.c_str());

	sqlite3 *db = nullptr;
	if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
		throw ::tst::Failure("could not create fixture database");

	Exec(db,
		"CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
		"CREATE TABLE entries(id INTEGER PRIMARY KEY, seq INTEGER NOT NULL);"
		"CREATE TABLE words(id INTEGER PRIMARY KEY, entry_id INTEGER NOT NULL,"
		" surface TEXT NOT NULL, reading TEXT NOT NULL DEFAULT '',"
		" common INTEGER NOT NULL DEFAULT 0);"
		"CREATE INDEX idx_words_surface ON words(surface);"
		"CREATE TABLE glosses(entry_id INTEGER NOT NULL, sense_order INTEGER NOT NULL DEFAULT 0,"
		" pos TEXT NOT NULL DEFAULT '', text TEXT NOT NULL,"
		" lang TEXT NOT NULL DEFAULT 'eng');"
		"CREATE INDEX idx_glosses_entry ON glosses(entry_id);");

	Exec(db,
		"INSERT INTO meta(key,value) VALUES('schema_version','1');"
		"INSERT INTO meta(key,value) VALUES('source','test-fixture');"
		"INSERT INTO entries(id,seq) VALUES(1,100);"
		"INSERT INTO words(entry_id,surface,reading,common) VALUES(1,'食べる','たべる',1);"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(1,0,'v1','to eat','eng');"
		"INSERT INTO entries(id,seq) VALUES(2,200);"
		"INSERT INTO words(entry_id,surface,reading,common) VALUES(2,'食物','しょくもつ',0);"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(2,0,'n','food','eng');"
		"INSERT INTO entries(id,seq) VALUES(3,300);"
		"INSERT INTO words(entry_id,surface,reading,common) VALUES(3,'私','わたし',1);"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(3,0,'pn','I; me','eng');"
		"INSERT INTO entries(id,seq) VALUES(4,400);"
		"INSERT INTO words(entry_id,surface,reading,common) VALUES(4,'は','は',1);"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(4,0,'prt','topic marker','eng');"
		"INSERT INTO entries(id,seq) VALUES(5,500);"
		"INSERT INTO words(entry_id,surface,reading,common) VALUES(5,'ゲーム','ゲーム',1);"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(5,0,'n','game','eng');"
		"INSERT INTO entries(id,seq) VALUES(6,600);"
		"INSERT INTO words(entry_id,surface,reading,common) VALUES(6,'行く','いく',1);"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(6,0,'v5k-s','to go','eng');"
		"INSERT INTO glosses(entry_id,sense_order,pos,text,lang) VALUES(6,1,'v5k-s','to continue','eng');");

	sqlite3_close(db);
	return path;
}

} // namespace

TEST(dict_tokenizer_splits_japanese_scripts_with_byte_offsets)
{
	const std::vector<JpToken> tokens = tokenizeJapanese("私はゲームを食べる");
	ASSERT_EQ(tokens.size(), static_cast<size_t>(6));
	ASSERT_EQ(tokens[0].surface, "私");
	ASSERT_EQ(tokens[0].begin, static_cast<size_t>(0));
	ASSERT_EQ(tokens[0].end, static_cast<size_t>(3));
	ASSERT_EQ(tokens[1].surface, "は");
	ASSERT_EQ(tokens[2].surface, "ゲーム");
	ASSERT_EQ(tokens[2].begin, static_cast<size_t>(6));
	ASSERT_EQ(tokens[2].end, static_cast<size_t>(15));
	ASSERT_EQ(tokens[4].surface, "食");
	ASSERT_EQ(tokens[5].surface, "べる");
	ASSERT_EQ(tokens[5].end, static_cast<size_t>(27));
}

TEST(dict_tokenizer_keeps_latin_and_digits_together)
{
	const std::vector<JpToken> tokens = tokenizeJapanese("PS5で遊ぼう");
	ASSERT_EQ(tokens.size(), static_cast<size_t>(5));
	ASSERT_EQ(tokens[0].surface, "PS");
	ASSERT_EQ(tokens[1].surface, "5");
	ASSERT_EQ(tokens[2].surface, "で");
	ASSERT_EQ(tokens[3].surface, "遊");
	ASSERT_EQ(tokens[4].surface, "ぼう");
}

TEST(dict_rejects_missing_database)
{
	JmDict dictionary;
	std::string error;
	ASSERT_FALSE(dictionary.open("/tmp/translator_core_no_such_dict.db", &error));
	ASSERT_FALSE(error.empty());
	ASSERT_FALSE(dictionary.ready());
}

TEST(dict_selects_senses_in_order)
{
	const std::string path = BuildFixtureDatabase();
	JmDict dictionary;
	std::string error;
	ASSERT_TRUE(dictionary.open(path, &error));
	ASSERT_TRUE(dictionary.ready());

	DictResult result = dictionary.lookupExact("行く");
	ASSERT_TRUE(result.found);
	ASSERT_EQ(result.entries.size(), static_cast<size_t>(1));
	ASSERT_EQ(result.entries[0].glosses.size(), static_cast<size_t>(2));
	ASSERT_EQ(result.entries[0].glosses[0].text, "to go");
	ASSERT_EQ(result.entries[0].glosses[1].text, "to continue");
	ASSERT_EQ(result.entries[0].reading, "いく");
}

TEST(dict_lookup_at_finds_word_containing_tapped_kanji)
{
	const std::string path = BuildFixtureDatabase();
	JmDict dictionary;
	std::string error;
	ASSERT_TRUE(dictionary.open(path, &error));

	const std::string line = "私は食べる";
	const size_t tap = line.find("食");
	ASSERT_TRUE(tap != std::string::npos);

	const std::vector<DictEntry> entries = dictionary.lookupAt(line, tap);
	ASSERT_FALSE(entries.empty());
	bool found = false;
	for (const DictEntry &entry : entries)
		found = found || entry.surface == "食べる";
	ASSERT_TRUE(found);
}

TEST(dict_lookup_at_deinflects_past_tense)
{
	const std::string path = BuildFixtureDatabase();
	JmDict dictionary;
	std::string error;
	ASSERT_TRUE(dictionary.open(path, &error));

	const std::string line = "昨日は食べた";
	const size_t tap = line.find("食");
	const std::vector<DictEntry> entries = dictionary.lookupAt(line, tap);
	ASSERT_FALSE(entries.empty());
	bool found = false;
	for (const DictEntry &entry : entries)
		found = found || entry.surface == "食べる";
	ASSERT_TRUE(found);
}

TEST(dict_prefix_lookup_returns_shorter_matches)
{
	const std::string path = BuildFixtureDatabase();
	JmDict dictionary;
	std::string error;
	ASSERT_TRUE(dictionary.open(path, &error));

	const std::vector<DictEntry> entries = dictionary.lookupPrefix("食", 8);
	ASSERT_TRUE(entries.size() >= 2);
	bool has_eat = false;
	bool has_food = false;
	for (const DictEntry &entry : entries)
	{
		has_eat = has_eat || entry.surface == "食べる";
		has_food = has_food || entry.surface == "食物";
	}
	ASSERT_TRUE(has_eat);
	ASSERT_TRUE(has_food);
}

TEST(dict_unknown_word_returns_no_entries)
{
	const std::string path = BuildFixtureDatabase();
	JmDict dictionary;
	std::string error;
	ASSERT_TRUE(dictionary.open(path, &error));
	ASSERT_FALSE(dictionary.lookupExact("存在しない単語xyz").found);
	ASSERT_TRUE(dictionary.lookupAt("存在しない単語xyz", 3).empty());
}

TEST(dict_metadata_is_exposed)
{
	const std::string path = BuildFixtureDatabase();
	JmDict dictionary;
	std::string error;
	ASSERT_TRUE(dictionary.open(path, &error));
	ASSERT_EQ(dictionary.schemaVersion(), "1");
}

TEST(dict_deinflection_generates_dictionary_forms)
{
	const std::vector<std::string> candidates = deinflectionCandidates("食べた");
	bool found = false;
	for (const std::string &candidate : candidates)
		found = found || candidate == "食べる";
	ASSERT_TRUE(found);

	const std::vector<std::string> polite = deinflectionCandidates("食べます");
	found = false;
	for (const std::string &candidate : polite)
		found = found || candidate == "食べる";
	ASSERT_TRUE(found);

	const std::vector<std::string> adjective = deinflectionCandidates("高かった");
	found = false;
	for (const std::string &candidate : adjective)
		found = found || candidate == "高い";
	ASSERT_TRUE(found);
}
