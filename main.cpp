// CineSeat - Movie Cinema Booking & Theater Seat Allocation System
// Language: C++ (Console UI) + SQLite Database

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cctype>
#include "sqlite3.h"
#include "Trie.h"

using namespace std;

// ---------------- Constants ----------------
const int ROWS = 8, COLS = 10;
const vector<string> SLOTS = {
    "10:00", "13:00", "16:00", "19:00", "22:00"
};
const int MAX_ADVANCE_DAYS = 30;

// ---------------- Pricing Strategy ----------------
class PricingStrategy {
public:
    virtual ~PricingStrategy() {}
    virtual int price(int row) const = 0;
};

class RowTierPricing : public PricingStrategy {
public:
    int price(int row) const override {
        return row < 2 ? 300 : (row < 6 ? 200 : 150);
    }

    static const char* tier(int row) {
        return row < 2 ? "Premium" :
               (row < 6 ? "Gold" : "Silver");
    }
};

// ---------------- Ticket ----------------
struct Ticket {
    string pnr, name, movie, date, time, seats;
    int total = 0;
};

// ---------------- Helper Functions ----------------
static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");

    return a == string::npos
        ? ""
        : s.substr(a, b - a + 1);
}

static string upper(string s) {
    for (auto& c : s)
        c = (char)toupper((unsigned char)c);

    return s;
}

static bool readLine(const string& prompt, string& out) {
    cout << prompt;

    if (!getline(cin, out))
        return false;

    out = trim(out);
    return true;
}

static string seatName(int r, int c) {
    return string(1, char('A' + r)) + to_string(c + 1);
}

static bool validName(const string& n) {
    if (n.size() < 2 || n.size() > 40)
        return false;

    for (char c : n) {
        if (!isalpha((unsigned char)c) &&
            c != ' ' && c != '.')
            return false;
    }

    return true;
}

static bool leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) ||
           y % 400 == 0;
}

static bool validDate(const string& s, tm& out) {
    if (s.size() != 10 ||
        s[4] != '-' || s[7] != '-')
        return false;

    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) {
        if (!isdigit((unsigned char)s[i]))
            return false;
    }

    int y = stoi(s.substr(0, 4));
    int m = stoi(s.substr(5, 2));
    int d = stoi(s.substr(8, 2));

    int dim[] = {
        31, leap(y) ? 29 : 28, 31, 30,
        31, 30, 31, 31, 30, 31, 30, 31
    };

    if (m < 1 || m > 12 ||
        d < 1 || d > dim[m - 1])
        return false;

    out = {};
    out.tm_year = y - 1900;
    out.tm_mon = m - 1;
    out.tm_mday = d;
    out.tm_hour = 12;
    out.tm_isdst = -1;

    return true;
}

// Returns an empty string if the date is valid.
static string checkShowDate(const string& s) {
    tm t;

    if (!validDate(s, t))
        return "Invalid date. Use YYYY-MM-DD format "
               "(e.g. 2026-10-25)";

    time_t now = time(nullptr);
    tm n = *localtime(&now);

    tm today = {};
    today.tm_year = n.tm_year;
    today.tm_mon = n.tm_mon;
    today.tm_mday = n.tm_mday;
    today.tm_hour = 12;
    today.tm_isdst = -1;

    double days =
        difftime(mktime(&t), mktime(&today)) / 86400.0;

    if (days < -0.5)
        return "Bookings are not allowed for past dates.";

    if (days > MAX_ADVANCE_DAYS + 0.5)
        return "Bookings are allowed only for the next " +
               to_string(MAX_ADVANCE_DAYS) + " days.";

    return "";
}

static bool slotPassedToday(
    const string& date, const string& slot
) {
    time_t now = time(nullptr);
    tm n = *localtime(&now);

    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &n);

    if (date != buf)
        return false;

    int h = stoi(slot.substr(0, 2));
    int m = stoi(slot.substr(3, 2));

    return (n.tm_hour * 60 + n.tm_min) >= (h * 60 + m);
}

// ---------------- Database Layer ----------------
class Database {
    sqlite3* db = nullptr;

public:
    bool open(const char* file) {
        if (sqlite3_open(file, &db) != SQLITE_OK)
            return false;

        const char* schema =
            "PRAGMA foreign_keys=ON;"

            "CREATE TABLE IF NOT EXISTS bookings("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "pnr TEXT UNIQUE,"
            "name TEXT NOT NULL,"
            "movie TEXT NOT NULL,"
            "show_date TEXT NOT NULL,"
            "show_time TEXT NOT NULL,"
            "total INTEGER NOT NULL);"

            "CREATE TABLE IF NOT EXISTS seats("
            "booking_id INTEGER NOT NULL "
            "REFERENCES bookings(id) ON DELETE CASCADE,"
            "movie TEXT NOT NULL,"
            "show_date TEXT NOT NULL,"
            "show_time TEXT NOT NULL,"
            "seat_row INTEGER NOT NULL,"
            "seat_col INTEGER NOT NULL,"
            "UNIQUE(movie, show_date, show_time, "
            "seat_row, seat_col));";

        char* err = nullptr;

        if (sqlite3_exec(
                db, schema, nullptr, nullptr, &err
            ) != SQLITE_OK) {

            cerr << "Database error: "
                 << (err ? err : "?") << "\n";

            sqlite3_free(err);
            return false;
        }

        return true;
    }

    ~Database() {
        if (db)
            sqlite3_close(db);
    }

    bool exec(const string& sql) {
        return sqlite3_exec(
            db, sql.c_str(), nullptr, nullptr, nullptr
        ) == SQLITE_OK;
    }

    // Load the seat map: true means booked.
    void loadSeatMap(
        const string& movie,
        const string& date,
        const string& time,
        vector<vector<bool>>& g
    ) {
        g.assign(ROWS, vector<bool>(COLS, false));

        sqlite3_stmt* st;

        sqlite3_prepare_v2(
            db,
            "SELECT seat_row, seat_col FROM seats "
            "WHERE movie=? AND show_date=? AND show_time=?",
            -1, &st, nullptr
        );

        sqlite3_bind_text(
            st, 1, movie.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_text(
            st, 2, date.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_text(
            st, 3, time.c_str(), -1, SQLITE_TRANSIENT
        );

        while (sqlite3_step(st) == SQLITE_ROW) {
            g[sqlite3_column_int(st, 0)]
             [sqlite3_column_int(st, 1)] = true;
        }

        sqlite3_finalize(st);
    }

    // Save a complete booking or roll it back.
    string book(
        const string& name,
        const string& movie,
        const string& date,
        const string& time,
        const vector<pair<int, int>>& seats,
        int total
    ) {
        if (!exec("BEGIN IMMEDIATE"))
            return "";

        sqlite3_stmt* st;

        sqlite3_prepare_v2(
            db,
            "INSERT INTO bookings"
            "(name,movie,show_date,show_time,total) "
            "VALUES(?,?,?,?,?)",
            -1, &st, nullptr
        );

        sqlite3_bind_text(
            st, 1, name.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_text(
            st, 2, movie.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_text(
            st, 3, date.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_text(
            st, 4, time.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_int(st, 5, total);

        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);

        if (!ok) {
            exec("ROLLBACK");
            return "";
        }

        long long id = sqlite3_last_insert_rowid(db);
        string pnr = "CIN" + to_string(1000 + id);

        exec(
            "UPDATE bookings SET pnr='" + pnr +
            "' WHERE id=" + to_string(id)
        );

        for (auto& s : seats) {
            sqlite3_prepare_v2(
                db,
                "INSERT INTO seats VALUES(?,?,?,?,?,?)",
                -1, &st, nullptr
            );

            sqlite3_bind_int(st, 1, (int)id);

            sqlite3_bind_text(
                st, 2, movie.c_str(), -1, SQLITE_TRANSIENT
            );
            sqlite3_bind_text(
                st, 3, date.c_str(), -1, SQLITE_TRANSIENT
            );
            sqlite3_bind_text(
                st, 4, time.c_str(), -1, SQLITE_TRANSIENT
            );

            sqlite3_bind_int(st, 5, s.first);
            sqlite3_bind_int(st, 6, s.second);

            ok = sqlite3_step(st) == SQLITE_DONE;
            sqlite3_finalize(st);

            if (!ok) {
                exec("ROLLBACK");
                return "";
            }
        }

        exec("COMMIT");
        return pnr;
    }

    bool cancel(const string& pnr, const string& name) {
        sqlite3_stmt* st;

        sqlite3_prepare_v2(
            db,
            "DELETE FROM bookings "
            "WHERE pnr=? AND lower(name)=lower(?)",
            -1, &st, nullptr
        );

        sqlite3_bind_text(
            st, 1, pnr.c_str(), -1, SQLITE_TRANSIENT
        );
        sqlite3_bind_text(
            st, 2, name.c_str(), -1, SQLITE_TRANSIENT
        );

        sqlite3_step(st);
        sqlite3_finalize(st);

        return sqlite3_changes(db) > 0;
    }

    // Load tickets into the PNR hash map.
    void loadTickets(unordered_map<string, Ticket>& map) {
        map.clear();

        sqlite3_stmt* st;

        sqlite3_prepare_v2(
            db,
            "SELECT b.pnr,b.name,b.movie,b.show_date,"
            "b.show_time,b.total,s.seat_row,s.seat_col "
            "FROM bookings b JOIN seats s "
            "ON s.booking_id=b.id "
            "ORDER BY b.id, s.seat_row, s.seat_col",
            -1, &st, nullptr
        );

        while (sqlite3_step(st) == SQLITE_ROW) {
            string pnr =
                (const char*)sqlite3_column_text(st, 0);

            Ticket& t = map[pnr];

            t.pnr = pnr;
            t.name = (const char*)sqlite3_column_text(st, 1);
            t.movie = (const char*)sqlite3_column_text(st, 2);
            t.date = (const char*)sqlite3_column_text(st, 3);
            t.time = (const char*)sqlite3_column_text(st, 4);
            t.total = sqlite3_column_int(st, 5);

            if (!t.seats.empty())
                t.seats += ", ";

            t.seats += seatName(
                sqlite3_column_int(st, 6),
                sqlite3_column_int(st, 7)
            );
        }

        sqlite3_finalize(st);
    }
};

// ---------------- User Interface ----------------
static void line() {
    cout << string(60, '=') << "\n";
}

static void banner() {
    line();
    cout << "        CINESEAT - Movie Booking & Seat Allocation\n";
    line();
}

static void showSeatMap(const vector<vector<bool>>& g) {
    cout << "\n                 [ S C R E E N ]\n\n      ";

    for (int c = 0; c < COLS; c++)
        cout << setw(4) << c + 1;

    cout << "\n";

    for (int r = 0; r < ROWS; r++) {
        cout << "  " << char('A' + r) << "   ";

        for (int c = 0; c < COLS; c++) {
            cout << setw(4) << (g[r][c] ? "[X]" : "[ ]");
        }

        cout << "   Rs." << RowTierPricing().price(r)
             << " " << RowTierPricing::tier(r) << "\n";
    }

    cout << "\n  [ ] = Available    [X] = Booked\n";
}

static void printTicket(const Ticket& t) {
    line();

    cout << "  PNR       : " << t.pnr
         << "\n  Name      : " << t.name
         << "\n  Movie     : " << t.movie
         << "\n  Date      : " << t.date
         << "\n  Time      : " << t.time
         << "\n  Seats     : " << t.seats
         << "\n  Total     : Rs." << t.total << "\n";

    line();
}

static bool parseSeat(
    const string& tok, int& r, int& c
) {
    if (tok.size() < 2 || tok.size() > 3)
        return false;

    char ch = (char)toupper((unsigned char)tok[0]);

    if (ch < 'A' || ch >= 'A' + ROWS)
        return false;

    for (size_t i = 1; i < tok.size(); i++) {
        if (!isdigit((unsigned char)tok[i]))
            return false;
    }

    int n = stoi(tok.substr(1));

    if (n < 1 || n > COLS)
        return false;

    r = ch - 'A';
    c = n - 1;

    return true;
}

// ---------------- Movie Selection ----------------
static bool chooseMovie(
    const Trie& trie, string& movie
) {
    while (true) {
        string p;

        if (!readLine(
            "\nEnter a movie name or prefix "
            "(Enter = show all movies, 0 = back): ", p
        ))
            return false;

        if (p == "0")
            return false;

        auto res = trie.autocomplete(p);

        if (res.empty()) {
            cout << "  No movies found matching \""
                 << p << "\". Please try again.\n";
            continue;
        }

        sort(res.begin(), res.end());

        for (size_t i = 0; i < res.size(); i++)
            cout << "  " << i + 1 << ". " << res[i] << "\n";

        string s;

        if (!readLine(
            "Select a movie number (0 = search again): ", s
        ))
            return false;

        if (s.empty() ||
            !all_of(s.begin(), s.end(), ::isdigit)) {
            cout << "  Invalid choice.\n";
            continue;
        }

        size_t k = stoul(s);

        if (k == 0)
            continue;

        if (k > res.size()) {
            cout << "  Invalid choice.\n";
            continue;
        }

        movie = res[k - 1];
        return true;
    }
}

// ---------------- Book Ticket ----------------
static void bookFlow(
    Database& db,
    const Trie& trie,
    unordered_map<string, Ticket>& pnrMap
) {
    RowTierPricing pricing;

    string name, movie, date, slot, in;

    while (true) {
        if (!readLine("\nEnter your name: ", name))
            return;

        if (validName(name))
            break;

        cout << "  Name must contain only letters, "
             << "spaces, or periods (2-40 characters).\n";
    }

    if (!chooseMovie(trie, movie))
        return;

    while (true) {
        if (!readLine("Show date (YYYY-MM-DD): ", date))
            return;

        string e = checkShowDate(date);

        if (e.empty())
            break;

        cout << "  " << e << "\n";
    }

    while (true) {
        cout << "\nShow timings:\n";

        for (size_t i = 0; i < SLOTS.size(); i++) {
            cout << "  " << i + 1 << ". " << SLOTS[i]
                 << (slotPassedToday(date, SLOTS[i])
                     ? "  (already passed)" : "")
                 << "\n";
        }

        if (!readLine("Select a time slot number: ", in))
            return;

        if (!in.empty() &&
            all_of(in.begin(), in.end(), ::isdigit)) {

            size_t k = stoul(in);

            if (k >= 1 && k <= SLOTS.size()) {
                if (slotPassedToday(date, SLOTS[k - 1])) {
                    cout << "  This show time has already passed.\n";
                    continue;
                }

                slot = SLOTS[k - 1];
                break;
            }
        }

        cout << "  Invalid slot.\n";
    }

    // Seat selection loop.
    while (true) {
        vector<vector<bool>> grid;

        db.loadSeatMap(movie, date, slot, grid);

        cout << "\n" << movie << " | " << date
             << " | " << slot;

        showSeatMap(grid);

        if (!readLine(
            "\nEnter seats (e.g. A1 A2 C5), 0 = cancel: ", in
        ))
            return;

        if (in == "0")
            return;

        istringstream ss(in);
        string tok;

        vector<pair<int, int>> sel;
        bool bad = false;

        while (ss >> tok) {
            int r, c;

            if (!parseSeat(tok, r, c)) {
                cout << "  Invalid seat: " << tok
                     << " (A-H, 1-10)\n";
                bad = true;
                break;
            }

            if (grid[r][c]) {
                cout << "  Seat " << seatName(r, c)
                     << " is already booked!\n";
                bad = true;
                break;
            }

            if (find(
                sel.begin(), sel.end(), make_pair(r, c)
            ) != sel.end()) {
                cout << "  Seat " << tok
                     << " was entered more than once.\n";
                bad = true;
                break;
            }

            sel.push_back({r, c});
        }

        if (bad)
            continue;

        if (sel.empty()) {
            cout << "  No seats selected.\n";
            continue;
        }

        if (sel.size() > 6) {
            cout << "  Maximum 6 seats are allowed per booking.\n";
            continue;
        }

        int total = 0;
        string seatStr;

        for (auto& s : sel) {
            total += pricing.price(s.first);

            seatStr +=
                (seatStr.empty() ? "" : ", ") +
                seatName(s.first, s.second);
        }

        cout << "\nSeats: " << seatStr
             << "   Total: Rs." << total << "\n";

        if (!readLine("Confirm booking? (y/n): ", in))
            return;

        if (upper(in) != "Y") {
            cout << "  Booking cancelled.\n";
            return;
        }

        string pnr = db.book(
            name, movie, date, slot, sel, total
        );

        if (pnr.empty()) {
            cout << "  Booking failed: one or more seats may "
                 << "have just been booked. Please review "
                 << "the updated seat map.\n";
            continue;
        }

        Ticket t{pnr, name, movie, date, slot, seatStr, total};

        pnrMap[pnr] = t;

        cout << "\n  Booking successful!\n";
        printTicket(t);

        return;
    }
}

// ---------------- Verify Ticket ----------------
static void verifyFlow(
    unordered_map<string, Ticket>& pnrMap
) {
    string p;

    if (!readLine("\nEnter PNR: ", p))
        return;

    auto it = pnrMap.find(upper(p));

    if (it == pnrMap.end()) {
        cout << "  Ticket not found.\n";
    } else {
        cout << "\n  Ticket Found!\n";
        printTicket(it->second);
    }
}

// ---------------- View Seat Map ----------------
static void seatMapFlow(
    Database& db, const Trie& trie
) {
    string movie, date, in, slot;

    if (!chooseMovie(trie, movie))
        return;

    while (true) {
        if (!readLine("Date (YYYY-MM-DD): ", date))
            return;

        string e = checkShowDate(date);

        if (e.empty())
            break;

        cout << "  " << e << "\n";
    }

    for (size_t i = 0; i < SLOTS.size(); i++)
        cout << "  " << i + 1 << ". " << SLOTS[i] << "\n";

    if (!readLine("Slot number: ", in) ||
        in.empty() ||
        !all_of(in.begin(), in.end(), ::isdigit))
        return;

    size_t k = stoul(in);

    if (k < 1 || k > SLOTS.size()) {
        cout << "  Invalid slot.\n";
        return;
    }

    vector<vector<bool>> g;

    db.loadSeatMap(movie, date, SLOTS[k - 1], g);

    cout << "\n" << movie << " | " << date
         << " | " << SLOTS[k - 1];

    showSeatMap(g);
}

// ---------------- Cancel Ticket ----------------
static void cancelFlow(
    Database& db,
    unordered_map<string, Ticket>& pnrMap
) {
    string p, n;

    if (!readLine("\nEnter PNR: ", p) ||
        !readLine("Enter the name used for booking: ", n))
        return;

    p = upper(p);

    if (db.cancel(p, n)) {
        pnrMap.erase(p);

        cout << "  Ticket cancelled successfully. "
             << "The seats are now available.\n";
    } else {
        cout << "  PNR or name did not match.\n";
    }
}

// ---------------- Main Function ----------------
int main() {
    Database db;

    if (!db.open("cineseat.db")) {
        cerr << "Unable to open the database.\n";
        return 1;
    }

    Trie trie;

    for (auto m : {
        "Avengers", "Avatar", "Animal", "Batman",
        "Barbie", "Dangal", "Inception", "Interstellar",
        "Jawan", "Oppenheimer", "Pathaan", "Pushpa 2",
        "Stree 2"
    }) {
        trie.insert(m);
    }

    unordered_map<string, Ticket> pnrMap;

    db.loadTickets(pnrMap);

    while (true) {
        banner();

        cout << "  1. Book a ticket\n"
             << "  2. Verify a ticket (PNR)\n"
             << "  3. View seat map\n"
             << "  4. Cancel a ticket\n"
             << "  5. Movie search\n"
             << "  0. Exit\n";

        line();

        string c;

        if (!readLine("Choice: ", c))
            break;

        if (c == "1")
            bookFlow(db, trie, pnrMap);

        else if (c == "2")
            verifyFlow(pnrMap);

        else if (c == "3")
            seatMapFlow(db, trie);

        else if (c == "4")
            cancelFlow(db, pnrMap);

        else if (c == "5") {
            string m;
            chooseMovie(trie, m);
        }

        else if (c == "0")
            break;

        else
            cout << "  Invalid choice.\n";
    }

    cout << "Thank you!\n";

    return 0;
}