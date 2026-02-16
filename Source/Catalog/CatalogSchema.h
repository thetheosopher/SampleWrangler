#pragma once

#include <string>

struct sqlite3; // forward declare

namespace sw
{

    /// Manages SQLite schema creation and migrations.
    class CatalogSchema
    {
    public:
        /// Create all tables (idempotent — uses IF NOT EXISTS).
        static bool createAll(sqlite3 *db);

    private:
        static bool exec(sqlite3 *db, const char *sql);
    };

} // namespace sw
