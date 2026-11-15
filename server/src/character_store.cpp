#include <wok/character_store.hpp>

#include <sqlite3.h>

#include <cstring>
#include <ctime>
#include <stdexcept>

namespace wok {

namespace {

constexpr const char* kSchema =
    "CREATE TABLE IF NOT EXISTS players ("
    "  steam_id INTEGER PRIMARY KEY,"
    "  token BLOB NOT NULL);"
    "CREATE TABLE IF NOT EXISTS characters ("
    "  steam_id INTEGER PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  race TEXT NOT NULL,"
    "  appearance BLOB NOT NULL,"
    "  x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, yaw REAL NOT NULL,"
    "  template TEXT NOT NULL DEFAULT '');";

// Databases created before the template column existed get it added on open (error ignored if present).
// Each runs on its own and fails harmlessly once applied.
constexpr const char* kMigrations[] = {
    "ALTER TABLE characters ADD COLUMN template TEXT NOT NULL DEFAULT '';",
    "ALTER TABLE characters ADD COLUMN stats BLOB NOT NULL DEFAULT x'';",
    "ALTER TABLE characters ADD COLUMN inventory BLOB NOT NULL DEFAULT x'';",
    "CREATE TABLE IF NOT EXISTS dead_characters (steam_id INTEGER NOT NULL, name TEXT NOT NULL, race TEXT NOT NULL,"
    " appearance BLOB NOT NULL, x REAL, y REAL, z REAL, template TEXT, stats BLOB, inventory BLOB,"
    " died_at INTEGER NOT NULL);",
    "CREATE TABLE IF NOT EXISTS npcs (id INTEGER PRIMARY KEY, template TEXT NOT NULL, name TEXT NOT NULL,"
    " x REAL NOT NULL, y REAL NOT NULL, z REAL NOT NULL, dead INTEGER NOT NULL, updated INTEGER NOT NULL);",
};

// Stats on disk: id (1 byte) + value (float, 4 bytes little-endian) per skill.
std::vector<uint8_t> packStats(const std::vector<StatValue>& stats) {
    std::vector<uint8_t> out;
    for (auto& s : stats) {
        out.push_back(s.id);
        uint8_t f[4];
        std::memcpy(f, &s.value, 4);
        out.insert(out.end(), f, f + 4);
    }
    return out;
}
std::vector<StatValue> unpackStats(const uint8_t* p, size_t n) {
    std::vector<StatValue> out;
    for (size_t i = 0; p && i + 5 <= n; i += 5) {
        StatValue s;
        s.id = p[i];
        std::memcpy(&s.value, p + i + 1, 4);
        out.push_back(s);
    }
    return out;
}

// RAII prepared statement.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("sqlite prepare: ") + sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& id(int i, uint64_t v) {
        sqlite3_bind_int64(s_, i, static_cast<sqlite3_int64>(v));
        return *this;
    }
    Stmt& real(int i, double v) {
        sqlite3_bind_double(s_, i, v);
        return *this;
    }
    Stmt& text(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& blob(int i, const void* p, size_t n) {
        sqlite3_bind_blob(s_, i, n ? p : "", static_cast<int>(n), SQLITE_TRANSIENT);
        return *this;
    }
    int step() { return sqlite3_step(s_); }
    sqlite3_stmt* get() { return s_; }

private:
    sqlite3_stmt* s_ = nullptr;
};

std::string columnText(sqlite3_stmt* s, int col) {
    auto p = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
    return p ? std::string(p, sqlite3_column_bytes(s, col)) : std::string();
}

}  // namespace

CharacterStore::CharacterStore(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("cannot open " + path + ": " + err);
    }
    char* err = nullptr;
    if (sqlite3_exec(db_, kSchema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "?";
        sqlite3_free(err);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("schema: " + msg);
    }
    for (const char* m : kMigrations) sqlite3_exec(db_, m, nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "CREATE TABLE IF NOT EXISTS world (key TEXT PRIMARY KEY, value REAL NOT NULL);", nullptr,
                 nullptr, nullptr);
    // Crash-safe and fast for frequent small writes (spec 7.7); ignored for ":memory:".
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
}

CharacterStore::~CharacterStore() { sqlite3_close(db_); }

std::optional<AuthToken> CharacterStore::tokenFor(uint64_t steamId) {
    Stmt s(db_, "SELECT token FROM players WHERE steam_id = ?1");
    s.id(1, steamId);
    if (s.step() != SQLITE_ROW || sqlite3_column_bytes(s.get(), 0) != static_cast<int>(sizeof(AuthToken)))
        return std::nullopt;
    AuthToken t{};
    std::memcpy(t.data(), sqlite3_column_blob(s.get(), 0), t.size());
    return t;
}

void CharacterStore::setToken(uint64_t steamId, const AuthToken& token) {
    Stmt s(db_, "INSERT INTO players(steam_id, token) VALUES(?1, ?2) "
                "ON CONFLICT(steam_id) DO UPDATE SET token = excluded.token");
    s.id(1, steamId).blob(2, token.data(), token.size()).step();
}

std::optional<CharacterData> CharacterStore::load(uint64_t steamId) {
    Stmt s(db_, "SELECT name, race, appearance, x, y, z, yaw, template, stats, inventory FROM characters WHERE steam_id = ?1");
    s.id(1, steamId);
    if (s.step() != SQLITE_ROW) return std::nullopt;
    CharacterData c;
    c.name = columnText(s.get(), 0);
    c.race = columnText(s.get(), 1);
    auto blob = static_cast<const uint8_t*>(sqlite3_column_blob(s.get(), 2));
    c.appearance.assign(blob, blob + sqlite3_column_bytes(s.get(), 2));
    c.x = static_cast<float>(sqlite3_column_double(s.get(), 3));
    c.y = static_cast<float>(sqlite3_column_double(s.get(), 4));
    c.z = static_cast<float>(sqlite3_column_double(s.get(), 5));
    c.yaw = static_cast<float>(sqlite3_column_double(s.get(), 6));
    c.templateId = columnText(s.get(), 7);
    c.stats = unpackStats(static_cast<const uint8_t*>(sqlite3_column_blob(s.get(), 8)),
                          static_cast<size_t>(sqlite3_column_bytes(s.get(), 8)));
    auto inv = static_cast<const uint8_t*>(sqlite3_column_blob(s.get(), 9));
    if (inv) c.inventory.assign(inv, inv + sqlite3_column_bytes(s.get(), 9));
    return c;
}

bool CharacterStore::create(uint64_t steamId, const CharacterData& c) {
    Stmt s(db_, "INSERT OR IGNORE INTO characters(steam_id, name, race, appearance, x, y, z, yaw, template) "
                "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    s.id(1, steamId).text(2, c.name).text(3, c.race).blob(4, c.appearance.data(), c.appearance.size());
    s.real(5, c.x).real(6, c.y).real(7, c.z).real(8, c.yaw).text(9, c.templateId);
    return s.step() == SQLITE_DONE && sqlite3_changes(db_) == 1;
}

bool CharacterStore::retire(uint64_t steamId) {
    Stmt keep(db_, "INSERT INTO dead_characters(steam_id, name, race, appearance, x, y, z, template, stats, inventory, died_at) "
                   "SELECT steam_id, name, race, appearance, x, y, z, template, stats, inventory, strftime('%s','now') "
                   "FROM characters WHERE steam_id = ?1");
    keep.id(1, steamId);
    if (keep.step() != SQLITE_DONE || sqlite3_changes(db_) != 1) return false;
    Stmt del(db_, "DELETE FROM characters WHERE steam_id = ?1");
    del.id(1, steamId);
    return del.step() == SQLITE_DONE;
}

void CharacterStore::saveStats(uint64_t steamId, const std::vector<StatValue>& stats) {
    auto blob = packStats(stats);
    Stmt s(db_, "UPDATE characters SET stats = ?2 WHERE steam_id = ?1");
    s.id(1, steamId).blob(2, blob.data(), blob.size()).step();
}

void CharacterStore::saveInventory(uint64_t steamId, const std::vector<uint8_t>& inventory) {
    Stmt s(db_, "UPDATE characters SET inventory = ?2 WHERE steam_id = ?1");
    s.id(1, steamId).blob(2, inventory.data(), inventory.size()).step();
}

std::optional<double> CharacterStore::worldHours() {
    Stmt s(db_, "SELECT value FROM world WHERE key = 'game_hours'");
    if (s.step() != SQLITE_ROW) return std::nullopt;
    return sqlite3_column_double(s.get(), 0);
}

void CharacterStore::setWorldHours(double hours) {
    Stmt s(db_, "INSERT INTO world(key, value) VALUES('game_hours', ?1) "
                "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    s.real(1, hours).step();
}

std::vector<NpcRecord> CharacterStore::loadNpcs() {
    constexpr int64_t kDay = 24 * 3600;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    {
        Stmt forget(db_, "DELETE FROM npcs WHERE updated < ?1 OR (dead = 1 AND updated < ?2)");
        forget.id(1, static_cast<uint64_t>(now - 7 * kDay)).id(2, static_cast<uint64_t>(now - kDay)).step();
    }
    std::vector<NpcRecord> out;
    Stmt s(db_, "SELECT id, name, template, x, y, z, dead FROM npcs");
    while (s.step() == SQLITE_ROW) {
        NpcRecord r;
        r.id = static_cast<uint32_t>(sqlite3_column_int64(s.get(), 0));
        r.name = columnText(s.get(), 1);
        r.templateId = columnText(s.get(), 2);
        r.x = static_cast<float>(sqlite3_column_double(s.get(), 3));
        r.y = static_cast<float>(sqlite3_column_double(s.get(), 4));
        r.z = static_cast<float>(sqlite3_column_double(s.get(), 5));
        r.dead = sqlite3_column_int(s.get(), 6) != 0;
        out.push_back(std::move(r));
    }
    return out;
}

void CharacterStore::saveNpcs(const std::vector<NpcRecord>& records) {
    if (records.empty()) return;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    for (const NpcRecord& r : records) {
        Stmt s(db_, "INSERT INTO npcs(id, name, template, x, y, z, dead, updated) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
                    "ON CONFLICT(id) DO UPDATE SET name = excluded.name, template = excluded.template, x = excluded.x, "
                    "y = excluded.y, z = excluded.z, dead = excluded.dead, updated = excluded.updated");
        s.id(1, r.id).text(2, r.name).text(3, r.templateId).real(4, r.x).real(5, r.y).real(6, r.z)
            .id(7, r.dead ? 1 : 0).id(8, static_cast<uint64_t>(now)).step();
    }
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
}

void CharacterStore::forgetNpcs(const std::vector<uint32_t>& ids) {
    if (ids.empty()) return;
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    for (uint32_t id : ids) {
        Stmt s(db_, "DELETE FROM npcs WHERE id = ?1");
        s.id(1, id).step();
    }
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
}

void CharacterStore::savePosition(uint64_t steamId, float x, float y, float z, float yaw) {
    Stmt s(db_, "UPDATE characters SET x = ?2, y = ?3, z = ?4, yaw = ?5 WHERE steam_id = ?1");
    s.id(1, steamId).real(2, x).real(3, y).real(4, z).real(5, yaw).step();
}

}  // namespace wok
