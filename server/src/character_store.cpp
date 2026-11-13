#include <wok/character_store.hpp>

#include <sqlite3.h>

#include <cstring>
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

void CharacterStore::savePosition(uint64_t steamId, float x, float y, float z, float yaw) {
    Stmt s(db_, "UPDATE characters SET x = ?2, y = ?3, z = ?4, yaw = ?5 WHERE steam_id = ?1");
    s.id(1, steamId).real(2, x).real(3, y).real(4, z).real(5, yaw).step();
}

}  // namespace wok
