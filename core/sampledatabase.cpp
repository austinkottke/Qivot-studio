#include "sampledatabase.h"
#include "sampleschema.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QVariant>
#include <QVector>

namespace {

using C = SampleSchema::Column;

// A tiny deterministic generator: the samples must be identical on every run.
struct Rng {
    quint32 state;
    quint32 next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int  range(int lo, int hi) { return lo + int(next() % quint32(hi - lo + 1)); }
    bool chance(int percent) { return range(1, 100) <= percent; }
    template <typename T> const T &pick(const QList<T> &list) { return list.at(int(next() % quint32(list.size()))); }
};

QVariant null() { return QVariant(); }
QVariant money(qint64 cents) { return cents / 100.0; }

const QStringList kFirst = {
    "Ada", "Alan", "Amara", "Ben", "Chen", "Clara", "Dev", "Elena", "Femi", "Grace", "Hana",
    "Ivan", "Jonas", "Kai", "Lena", "Mateo", "Maya", "Nadia", "Omar", "Priya", "Quinn",
    "Rosa", "Sam", "Sofia", "Tariq", "Uma", "Victor", "Wen", "Yara", "Zoe" };
const QStringList kLast = {
    "Abara", "Bauer", "Castillo", "Dubois", "Eriksen", "Fischer", "Garcia", "Haddad", "Ito",
    "Jensen", "Kowalski", "Larsen", "Mendes", "Nakamura", "Okafor", "Petrov", "Quint", "Rossi",
    "Silva", "Tanaka", "Ueda", "Varga", "Walsh", "Xu", "Yilmaz", "Zimmer" };
const QStringList kCountries = {
    "UK", "US", "Canada", "Nigeria", "Japan", "Brazil", "Germany", "France", "India", "Sweden" };
const QStringList kCities = {
    "London", "Leeds", "Austin", "Denver", "Toronto", "Lagos", "Osaka", "Recife", "Berlin",
    "Lyon", "Pune", "Malmo", "Seattle", "Glasgow", "Kyoto" };

// ============================================================================
// bookshop
// ============================================================================

const QStringList kPublishers = {
    "Ashgrove Press", "Blue Heron", "Cinder & Vale", "Driftwood Books", "Eastlight",
    "Foxglove House", "Granite Street", "Harbour Lane", "Inkwell", "Juniper & Co",
    "Kestrel Editions", "Lantern Row" };
const QStringList kTitleA = {
    "The Silent", "A Northern", "The Last", "Every", "The Glass", "Beneath the", "A Field of",
    "The Paper", "Small", "The Long", "Winter", "The Hidden", "Salt and", "A History of" };
const QStringList kTitleB = {
    "Orchard", "Lighthouse", "Cartographer", "River", "Garden", "Kingdom", "Archive", "Harbour",
    "Signal", "Library", "Machine", "Season", "Atlas", "Promise", "Weather", "Engine" };
const QStringList kStatuses = { "delivered", "delivered", "delivered", "shipped", "pending", "cancelled" };
const QStringList kReviews = {
    "Couldn't put it down.", "Slow start, great ending.", "Beautifully written.",
    "Not for me.", "A new favourite.", "Good, not great.", "Read it twice." };

SampleSchema bookshop()
{
    SampleSchema s(SampleDatabase::version("bookshop"));
    s.addTable("publisher", { C::id(), C::text("name", 80).required().unique(), C::text("country", 40) });
    s.addTable("author", { C::id(), C::text("first_name", 60), C::text("last_name", 60).required(),
                           C::integer("born"), C::text("country", 40) });
    s.addTable("book", { C::id(), C::text("title", 200).required(),
                         C::integer("author_id").required().references("author", "id", "CASCADE"),
                         C::integer("publisher_id").references("publisher", "id", "SET NULL"),
                         C::integer("published"), C::integer("pages"),
                         C::real("price").required().defaultTo(9.99), C::text("isbn", 20).unique() });
    s.addTable("customer", { C::id(), C::text("email", 120).required().unique(), C::text("name", 120).required(),
                             C::text("city", 60), C::date("joined").required() });
    s.addTable("orders", { C::id(), C::integer("customer_id").required().references("customer"),
                           C::dateTime("placed_at").required(),
                           C::text("status", 12).required().defaultTo("pending")
                               .check("status IN ('pending','shipped','delivered','cancelled')"),
                           C::real("total") });
    s.addTable("order_item", { C::integer("order_id").required().references("orders", "id", "CASCADE"),
                               C::integer("book_id").required().references("book"),
                               C::integer("quantity").required().defaultTo(1), C::real("unit_price").required() });
    s.primaryKey("order_item", { "order_id", "book_id" });
    s.addTable("review", { C::id(), C::integer("book_id").required().references("book", "id", "CASCADE"),
                           C::integer("customer_id").references("customer", "id", "SET NULL"),
                           C::integer("rating").required().check("rating BETWEEN 1 AND 5"),
                           C::longText("body"), C::date("created").required() });
    s.index("idx_book_author", "book", { "author_id" });
    s.index("idx_orders_customer_placed", "orders", { "customer_id", "placed_at" });
    s.index("idx_review_book", "review", { "book_id" });
    s.view("book_sales",
           "  SELECT b.id AS book_id, b.title, a.last_name AS author,\n"
           "         SUM(oi.quantity) AS units, CAST(SUM(oi.quantity * oi.unit_price) AS DECIMAL(12,2)) AS revenue\n"
           "  FROM book b JOIN author a ON a.id = b.author_id\n"
           "  LEFT JOIN order_item oi ON oi.book_id = b.id\n"
           "  GROUP BY b.id, b.title, a.last_name",
           "  SELECT b.id AS book_id, b.title, a.last_name AS author,"
           "         SUM(oi.quantity) AS units, ROUND(SUM(oi.quantity * oi.unit_price), 2) AS revenue"
           "  FROM book b JOIN author a ON a.id = b.author_id"
           "  LEFT JOIN order_item oi ON oi.book_id = b.id"
           "  GROUP BY b.id");
    // Full-text search over titles, if this SQLite has FTS5.
    s.sqliteExtra("CREATE VIRTUAL TABLE book_search USING fts5(title, author)", "book_search", { "title", "author" });

    // The rows. The generator calls are in the same order as ever, so the
    // data is too.
    Rng rng{ 20260930u };
    int id = 0;
    for (const QString &p : kPublishers)
        s.insert("publisher", { ++id, p, rng.pick(kCountries) });

    const int nAuthors = 150, nBooks = 1200, nCustomers = 3000, nOrders = 9000, nReviews = 6000;
    for (int i = 0; i < nAuthors; ++i) {
        // A few authors are known by one name; their first_name stays NULL.
        const QVariant first = i % 23 == 0 ? null() : QVariant(rng.pick(kFirst));
        const QString last = rng.pick(kLast);
        const int born = rng.range(1920, 1995);
        s.insert("author", { i + 1, first, last, born, rng.pick(kCountries) });
    }
    for (int i = 0; i < nBooks; ++i) {
        const QString title = rng.pick(kTitleA) + QLatin1Char(' ') + rng.pick(kTitleB);
        const int author = rng.range(1, nAuthors);
        const QVariant publisher = i % 17 == 0 ? null() : QVariant(rng.range(1, kPublishers.size()));
        const int published = rng.range(1950, 2026);
        const int pages = rng.range(96, 880);
        const double price = rng.range(599, 3499) / 100.0;
        s.insert("book", { i + 1, title, author, publisher, published, pages, price,
                           QStringLiteral("978-%1").arg(1000000000 + i * 7919) });
        s.insertExtra({ title, rng.pick(kLast) });
    }
    const QDate epoch(2019, 1, 1);
    for (int i = 0; i < nCustomers; ++i) {
        const QString first = rng.pick(kFirst), last = rng.pick(kLast);
        const QVariant city = i % 11 == 0 ? null() : QVariant(rng.pick(kCities));
        s.insert("customer", { i + 1, QStringLiteral("%1.%2%3@example.com").arg(first.toLower(), last.toLower()).arg(i),
                               first + QLatin1Char(' ') + last, city, epoch.addDays(rng.range(0, 2400)) });
    }
    for (int i = 1; i <= nOrders; ++i) {
        const int lines = rng.range(1, 4);
        double total = 0;
        QSet<int> books;
        QVector<QVariantList> items;
        for (int l = 0; l < lines; ++l) {
            const int qty = rng.range(1, 3);
            const double price = rng.range(599, 3499) / 100.0;
            total += qty * price;
            const int book = rng.range(1, nBooks);
            if (!books.contains(book)) {           // one line per book
                books.insert(book);
                items << QVariantList{ i, book, qty, price };
            }
        }
        const int customer = rng.range(1, nCustomers);
        const QDate day = epoch.addDays(rng.range(30, 2460));
        const int hour = rng.range(8, 22);
        const int minute = rng.range(0, 59);
        s.insert("orders", { i, customer, QDateTime(day, QTime(hour, minute)), rng.pick(kStatuses),
                             qRound(total * 100) / 100.0 });
        for (const QVariantList &r : items)
            s.insert("order_item", r);
    }
    for (int i = 0; i < nReviews; ++i) {
        const int book = rng.range(1, nBooks);
        const QVariant customer = i % 29 == 0 ? null() : QVariant(rng.range(1, nCustomers));
        const int a = rng.range(1, 5);
        const int rating = qBound(1, a + rng.range(0, 1), 5);       // skews positive
        const QVariant body = i % 3 == 0 ? null() : QVariant(rng.pick(kReviews));
        s.insert("review", { i + 1, book, customer, rating, body, epoch.addDays(rng.range(60, 2460)) });
    }
    return s;
}

// ============================================================================
// university
// ============================================================================

struct Department { const char *code, *name, *building; QStringList topics; };
const QList<Department> kDepartments = {
    { "CS",   "Computer Science", "Lovelace Hall", { "Programming", "Algorithms", "Data Structures", "Databases", "Operating Systems", "Networks", "Compilers", "Machine Learning", "Graphics", "Security" } },
    { "MATH", "Mathematics",      "Noether Hall",  { "Calculus", "Linear Algebra", "Probability", "Number Theory", "Topology", "Real Analysis", "Statistics", "Geometry", "Combinatorics" } },
    { "PHYS", "Physics",          "Meitner Hall",  { "Mechanics", "Electromagnetism", "Optics", "Thermodynamics", "Quantum Physics", "Astrophysics", "Relativity", "Acoustics" } },
    { "CHEM", "Chemistry",        "Franklin Hall", { "General Chemistry", "Organic Chemistry", "Biochemistry", "Physical Chemistry", "Spectroscopy", "Polymers", "Analytical Chemistry" } },
    { "BIO",  "Biology",          "Darwin Hall",   { "Cell Biology", "Genetics", "Ecology", "Evolution", "Microbiology", "Neuroscience", "Botany", "Marine Biology" } },
    { "HIST", "History",          "Gibbon Hall",   { "Ancient History", "Medieval Europe", "Modern Asia", "The Americas", "Economic History", "Historiography", "Empires" } },
    { "ECON", "Economics",        "Keynes Hall",   { "Microeconomics", "Macroeconomics", "Econometrics", "Game Theory", "Public Finance", "Labour Economics", "Trade" } },
    { "ENG",  "English",          "Austen Hall",   { "Poetry", "The Novel", "Shakespeare", "Creative Writing", "Rhetoric", "Literary Theory", "Drama" } },
    { "PHIL", "Philosophy",       "Hume Hall",     { "Logic", "Ethics", "Metaphysics", "Philosophy of Mind", "Epistemology", "Aesthetics", "Political Philosophy" } },
    { "ART",  "Art & Design",     "Kahlo Hall",    { "Drawing", "Typography", "Photography", "Sculpture", "Art History", "Interaction Design", "Printmaking" } },
};
const QStringList kCoursePatterns = { "Introduction to %1", "%1", "Topics in %1", "Advanced %1", "%1 Laboratory", "Seminar in %1" };
const QStringList kRanks = { "Professor", "Associate Professor", "Assistant Professor", "Lecturer" };
const QStringList kGrades = { "A", "A", "A", "B", "B", "B", "B", "C", "C", "D", "F", "W" };

SampleSchema university()
{
    SampleSchema s(SampleDatabase::version("university"));
    s.addTable("department", { C::id(), C::text("code", 8).required().unique(), C::text("name", 80).required().unique(),
                               C::text("building", 40), C::decimal("budget", 12, 2),
                               C::integer("chair_id").references("instructor", "id", "SET NULL") });
    s.addTable("instructor", { C::id(), C::text("first_name", 60).required(), C::text("last_name", 60).required(),
                               C::text("email", 120).required().unique(),
                               C::integer("department_id").required().references("department"),
                               C::text("rank_title", 30).required()
                                   .check("rank_title IN ('Professor','Associate Professor','Assistant Professor','Lecturer')"),
                               C::date("hired").required(), C::decimal("salary", 10, 2) });
    s.addTable("student", { C::id(), C::text("first_name", 60).required(), C::text("last_name", 60).required(),
                            C::text("email", 120).required().unique(), C::date("enrolled").required(),
                            C::integer("major_id").references("department", "id", "SET NULL"),
                            C::integer("advisor_id").references("instructor", "id", "SET NULL"),
                            C::boolean("graduated").required().defaultTo(false) });
    s.addTable("course", { C::id(), C::text("code", 12).required().unique(), C::text("title", 120).required(),
                           C::integer("department_id").required().references("department"),
                           C::integer("credits").required().defaultTo(3).check("credits BETWEEN 1 AND 6") });
    // Courses that need other courses first: a many-to-many of course to itself.
    s.addTable("course_prereq", { C::integer("course_id").required().references("course"),
                                  C::integer("prereq_id").required().references("course") });
    s.primaryKey("course_prereq", { "course_id", "prereq_id" });
    s.check("course_prereq", "course_id <> prereq_id");
    s.addTable("term", { C::id(), C::text("name", 20).required().unique(), C::date("starts_on").required(),
                         C::date("ends_on").required() });
    s.check("term", "ends_on > starts_on");
    s.addTable("room", { C::id(), C::text("building", 40).required(), C::text("room_no", 8).required(),
                         C::integer("seats").required().check("seats > 0") });
    s.unique("room", { "building", "room_no" });
    // A section is a course in a term: its key is all three.
    s.addTable("section", { C::integer("course_id").required().references("course", "id", "CASCADE"),
                            C::integer("term_id").required().references("term", "id", "CASCADE"),
                            C::integer("section_no").required(),
                            C::integer("instructor_id").references("instructor", "id", "SET NULL"),
                            C::integer("room_id").references("room", "id", "SET NULL"),
                            C::integer("capacity").required() });
    s.primaryKey("section", { "course_id", "term_id", "section_no" });
    s.addTable("enrollment", { C::integer("student_id").required().references("student", "id", "CASCADE"),
                               C::integer("course_id").required(), C::integer("term_id").required(),
                               C::integer("section_no").required(), C::date("enrolled_on").required(),
                               C::text("grade", 2).check("grade IN ('A','B','C','D','F','W')") });
    s.primaryKey("enrollment", { "student_id", "course_id", "term_id" });
    s.foreignKey("enrollment", { "course_id", "term_id", "section_no" }, "section",
                 { "course_id", "term_id", "section_no" }, "CASCADE");
    s.index("idx_instructor_department", "instructor", { "department_id" });
    s.index("idx_student_advisor", "student", { "advisor_id" });
    s.index("idx_enrollment_section", "enrollment", { "course_id", "term_id", "section_no" });
    s.view("course_enrollment",
           "  SELECT c.code, c.title, d.name AS department, COUNT(e.student_id) AS enrolled\n"
           "  FROM course c JOIN department d ON d.id = c.department_id\n"
           "  LEFT JOIN enrollment e ON e.course_id = c.id\n"
           "  GROUP BY c.id, c.code, c.title, d.name");
    s.view("transcript",
           "  SELECT st.id AS student_id, st.last_name, t.name AS term, c.code, c.title, c.credits, e.grade\n"
           "  FROM enrollment e\n"
           "  JOIN student st ON st.id = e.student_id\n"
           "  JOIN course c ON c.id = e.course_id\n"
           "  JOIN term t ON t.id = e.term_id");

    Rng rng{ 20261001u };
    const int nDepartments = kDepartments.size();

    // Instructors: the first of each department is a professor, and its chair.
    QHash<int, QList<int>> staff;               // department -> instructors
    const int nInstructors = 140;
    for (int i = 1; i <= nInstructors; ++i) {
        const int dept = (i - 1) % nDepartments + 1;
        const QString rank = i <= nDepartments ? kRanks.first() : rng.pick(kRanks);
        const QString first = rng.pick(kFirst), last = rng.pick(kLast);
        const int base = rank == kRanks.at(0) ? 118000 : rank == kRanks.at(1) ? 96000 : rank == kRanks.at(2) ? 82000 : 61000;
        s.insert("instructor", { i, first, last,
                                 QStringLiteral("%1.%2%3@northfield.edu").arg(first.toLower(), last.toLower()).arg(i),
                                 dept, rank, QDate(1990, 8, 15).addDays(rng.range(0, 12000)),
                                 money(qint64(base + rng.range(0, 30000)) * 100) });
        staff[dept] << i;
    }
    for (int d = 1; d <= nDepartments; ++d) {
        const Department &dep = kDepartments.at(d - 1);
        s.insert("department", { d, dep.code, dep.name, dep.building,
                                 money(qint64(rng.range(800, 4000)) * 100000), d /* its first professor */ });
    }

    // Courses: levels 100-400, each department's topics in a few shapes.
    struct Course { int id, dept, number; };
    QList<Course> courses;
    QHash<int, QList<int>> byDept;
    int courseId = 0;
    for (int d = 1; d <= nDepartments; ++d) {
        const Department &dep = kDepartments.at(d - 1);
        QSet<QString> titles;
        for (int n = 0; n < 18; ++n) {
            const int level = 100 * (1 + n / 5);                  // 100, 200, 300, 400
            const int number = level + 1 + (n % 5) * 10 + rng.range(0, 8);
            QString title;
            do {
                title = rng.pick(kCoursePatterns).arg(rng.pick(dep.topics));
            } while (titles.contains(title));
            titles.insert(title);
            ++courseId;
            s.insert("course", { courseId, QStringLiteral("%1 %2").arg(dep.code).arg(number), title, d,
                                 title.endsWith(QLatin1String("Laboratory")) ? 2 : rng.range(3, 4) });
            courses << Course{ courseId, d, number };
            byDept[d] << courseId;
        }
    }
    // Prerequisites: 200-level and up need one or two lower courses of their department.
    for (const Course &c : courses) {
        if (c.number < 200)
            continue;
        QList<int> lower;
        for (const Course &o : courses)
            if (o.dept == c.dept && o.number / 100 < c.number / 100)
                lower << o.id;
        QSet<int> need;
        const int count = rng.range(0, 2);
        for (int k = 0; k < count; ++k)
            need.insert(rng.pick(lower));
        QList<int> sorted = need.values();
        std::sort(sorted.begin(), sorted.end());
        for (int p : sorted)
            s.insert("course_prereq", { c.id, p });
    }

    // Terms: three academic years.
    struct Term { int id; QDate starts, ends; };
    QList<Term> terms;
    int termId = 0;
    for (int year = 2022; year <= 2025; ++year) {
        const QList<QPair<QString, QPair<QDate, QDate>>> parts = {
            { QStringLiteral("Spring %1").arg(year), { QDate(year, 1, 16), QDate(year, 5, 12) } },
            { QStringLiteral("Summer %1").arg(year), { QDate(year, 6, 2), QDate(year, 8, 8) } },
            { QStringLiteral("Fall %1").arg(year),   { QDate(year, 8, 28), QDate(year, 12, 15) } } };
        for (const auto &p : parts) {
            if ((year == 2022 && !p.first.startsWith(QLatin1String("Fall"))) || (year == 2025 && p.first.startsWith(QLatin1String("Fall"))))
                continue;                           // Fall 2022 to Summer 2025
            ++termId;
            s.insert("term", { termId, p.first, p.second.first, p.second.second });
            terms << Term{ termId, p.second.first, p.second.second };
        }
    }

    // Rooms.
    const QStringList buildings = { "Lovelace Hall", "Noether Hall", "Meitner Hall", "Darwin Hall", "Austen Hall", "Commons" };
    const int nRooms = 48;
    for (int r = 1; r <= nRooms; ++r)
        s.insert("room", { r, buildings.at((r - 1) % buildings.size()),
                           QStringLiteral("%1%2").arg(1 + (r - 1) / buildings.size()).arg(rng.range(1, 30), 2, 10, QLatin1Char('0')),
                           rng.pick(QList<int>{ 24, 30, 40, 60, 80, 120, 240 }) });

    // Sections: about half the courses run each term (summer: fewer), intro ones twice.
    struct Section { int course, term, no; };
    QHash<int, QList<Section>> sectionsByTerm;
    for (const Term &t : terms) {
        const bool summer = t.starts.month() == 6;
        for (const Course &c : courses) {
            if (!rng.chance(summer ? 15 : 50))
                continue;
            const int count = c.number < 200 && !summer ? rng.range(1, 3) : 1;
            for (int n = 1; n <= count; ++n) {
                const QVariant teacher = rng.chance(4) ? null() : QVariant(rng.pick(staff.value(c.dept)));   // TBA
                s.insert("section", { c.id, t.id, n, teacher, rng.chance(6) ? null() : QVariant(rng.range(1, nRooms)),
                                      rng.pick(QList<int>{ 20, 30, 40, 60, 90, 120 }) });
                sectionsByTerm[t.id] << Section{ c.id, t.id, n };
            }
        }
    }

    // Students, and what they took: two to four terms of three to five courses.
    const int nStudents = 2800;
    const Term &last = terms.last();
    for (int i = 1; i <= nStudents; ++i) {
        const QString first = rng.pick(kFirst), lastName = rng.pick(kLast);
        const QVariant major = i % 17 == 0 ? null() : QVariant(rng.range(1, nDepartments));     // undeclared
        const QVariant advisor = i % 13 == 0 || major.isNull() ? null() : QVariant(rng.pick(staff.value(major.toInt())));
        const int firstTerm = rng.range(0, terms.size() - 2);
        const QDate enrolled = terms.at(firstTerm).starts.addDays(-rng.range(10, 60));
        s.insert("student", { i, first, lastName,
                              QStringLiteral("%1.%2%3@students.northfield.edu").arg(first.toLower(), lastName.toLower()).arg(i),
                              enrolled, major, advisor, firstTerm < 2 && rng.chance(30) });
        const int termCount = qMin(rng.range(2, 4), terms.size() - firstTerm);
        for (int k = 0; k < termCount; ++k) {
            const Term &t = terms.at(firstTerm + k);
            const QList<Section> &offered = sectionsByTerm.value(t.id);
            if (offered.isEmpty())
                continue;
            QSet<int> taken;
            const int load = t.starts.month() == 6 ? rng.range(1, 2) : rng.range(3, 5);
            for (int c = 0; c < load; ++c) {
                const Section &sec = rng.pick(offered);
                if (taken.contains(sec.course))
                    continue;
                taken.insert(sec.course);
                const QVariant grade = t.id == last.id ? null() : QVariant(rng.pick(kGrades));   // in progress
                s.insert("enrollment", { i, sec.course, sec.term, sec.no, t.starts.addDays(-rng.range(1, 40)), grade });
            }
        }
    }
    return s;
}

// ============================================================================
// company
// ============================================================================

const QStringList kRegions = { "Americas", "Europe", "Asia Pacific", "Middle East & Africa" };
struct Country { const char *code, *name; int region; };
const QList<Country> kCountryList = {
    { "US", "United States", 1 }, { "CA", "Canada", 1 }, { "BR", "Brazil", 1 }, { "MX", "Mexico", 1 },
    { "GB", "United Kingdom", 2 }, { "DE", "Germany", 2 }, { "FR", "France", 2 }, { "NL", "Netherlands", 2 },
    { "SE", "Sweden", 2 }, { "ES", "Spain", 2 }, { "PL", "Poland", 2 },
    { "JP", "Japan", 3 }, { "IN", "India", 3 }, { "AU", "Australia", 3 }, { "SG", "Singapore", 3 }, { "KR", "South Korea", 3 },
    { "AE", "United Arab Emirates", 4 }, { "ZA", "South Africa", 4 }, { "NG", "Nigeria", 4 }, { "EG", "Egypt", 4 } };
const QList<QPair<QString, QString>> kOffices = {
    { "New York", "US" }, { "Austin", "US" }, { "Seattle", "US" }, { "Toronto", "CA" }, { "Sao Paulo", "BR" },
    { "Mexico City", "MX" }, { "London", "GB" }, { "Edinburgh", "GB" }, { "Berlin", "DE" }, { "Munich", "DE" },
    { "Paris", "FR" }, { "Amsterdam", "NL" }, { "Stockholm", "SE" }, { "Madrid", "ES" }, { "Warsaw", "PL" },
    { "Tokyo", "JP" }, { "Bengaluru", "IN" }, { "Pune", "IN" }, { "Sydney", "AU" }, { "Singapore", "SG" },
    { "Seoul", "KR" }, { "Dubai", "AE" }, { "Cape Town", "ZA" }, { "Lagos", "NG" } };
const QStringList kCompanyDepartments = {
    "Operations", "Engineering", "Sales", "Marketing", "Finance", "People", "Legal",
    "Support", "Product", "Design", "Research", "Security" };
struct Job { const char *title; int min, max; };
const QList<Job> kJobs = {
    { "Chief Executive", 250000, 400000 }, { "Director", 160000, 240000 }, { "Manager", 110000, 170000 },
    { "Principal Engineer", 150000, 220000 }, { "Senior Engineer", 120000, 170000 }, { "Engineer", 85000, 130000 },
    { "Account Executive", 70000, 140000 }, { "Sales Representative", 50000, 90000 }, { "Marketing Specialist", 60000, 100000 },
    { "Financial Analyst", 70000, 115000 }, { "Recruiter", 55000, 95000 }, { "Counsel", 120000, 200000 },
    { "Support Specialist", 45000, 75000 }, { "Product Manager", 110000, 175000 }, { "Designer", 75000, 130000 },
    { "Researcher", 90000, 160000 }, { "Security Analyst", 90000, 150000 } };
// The jobs each department hires for (indexes into kJobs, after its director and managers).
const QList<QList<int>> kDepartmentJobs = {
    { 6, 9 }, { 3, 4, 5, 5, 5 }, { 6, 7, 7 }, { 8 }, { 9 }, { 10 }, { 11 }, { 12, 12 }, { 13 }, { 14 }, { 15 }, { 16 } };
const QStringList kProjectA = { "Atlas", "Beacon", "Cobalt", "Delta", "Ember", "Falcon", "Granite", "Harbor", "Iris",
                                "Juniper", "Keystone", "Lumen", "Meridian", "Nova", "Orbit" };
const QStringList kProjectB = { "Migration", "Launch", "Platform", "Redesign", "Rollout", "Audit", "Pilot", "Initiative" };
const QStringList kRoles = { "Lead", "Contributor", "Contributor", "Contributor", "Reviewer", "Sponsor", "Analyst" };

SampleSchema company()
{
    SampleSchema s(SampleDatabase::version("company"));
    s.addTable("region", { C::id(), C::text("name", 40).required().unique() });
    // Countries keep their ISO code as the key: a text key other tables refer to.
    s.addTable("country", { C::text("code", 2).required(), C::text("name", 60).required().unique(),
                            C::integer("region_id").required().references("region") });
    s.primaryKey("country", { "code" });
    s.addTable("office", { C::id(), C::text("city", 60).required(),
                           C::text("country_code", 2).required().references("country", "code"),
                           C::date("opened") });
    s.addTable("department", { C::id(), C::text("name", 60).required().unique(),
                               C::integer("office_id").references("office", "id", "SET NULL"),
                               C::integer("manager_id").references("employee", "id", "SET NULL") });
    s.addTable("job", { C::id(), C::text("title", 60).required().unique(),
                        C::decimal("min_salary", 10, 2).required(), C::decimal("max_salary", 10, 2).required() });
    s.check("job", "max_salary >= min_salary");
    // Everyone but the chief executive reports to someone: employees manage employees.
    s.addTable("employee", { C::id(), C::text("first_name", 60).required(), C::text("last_name", 60).required(),
                             C::text("email", 120).required().unique(), C::date("hired").required(),
                             C::integer("job_id").required().references("job"),
                             C::integer("department_id").required().references("department"),
                             C::integer("manager_id").references("employee"),
                             C::integer("office_id").references("office", "id", "SET NULL"),
                             C::boolean("active").required().defaultTo(true) });
    s.addTable("salary", { C::integer("employee_id").required().references("employee", "id", "CASCADE"),
                           C::date("from_date").required(), C::date("to_date"),
                           C::decimal("amount", 10, 2).required().check("amount > 0") });
    s.primaryKey("salary", { "employee_id", "from_date" });
    s.addTable("job_history", { C::integer("employee_id").required().references("employee", "id", "CASCADE"),
                                C::date("start_date").required(), C::date("end_date").required(),
                                C::integer("job_id").required().references("job"),
                                C::integer("department_id").required().references("department") });
    s.primaryKey("job_history", { "employee_id", "start_date" });
    s.check("job_history", "end_date > start_date");
    s.addTable("project", { C::id(), C::text("code", 12).required().unique(), C::text("name", 80).required(),
                            C::integer("department_id").references("department", "id", "SET NULL"),
                            C::decimal("budget", 12, 2), C::date("starts_on").required(), C::date("ends_on") });
    s.check("project", "ends_on IS NULL OR ends_on > starts_on");
    s.addTable("assignment", { C::integer("project_id").required().references("project", "id", "CASCADE"),
                               C::integer("employee_id").required().references("employee", "id", "CASCADE"),
                               C::text("role", 20).required(),
                               C::integer("allocation").required().check("allocation BETWEEN 5 AND 100") });
    s.primaryKey("assignment", { "project_id", "employee_id" });
    s.index("idx_employee_manager", "employee", { "manager_id" });
    s.index("idx_employee_department", "employee", { "department_id" });
    s.index("idx_assignment_employee", "assignment", { "employee_id" });
    s.view("org_chart",
           "  SELECT e.id, e.first_name, e.last_name, j.title, d.name AS department,\n"
           "         m.id AS manager_id, m.last_name AS manager\n"
           "  FROM employee e\n"
           "  JOIN job j ON j.id = e.job_id\n"
           "  JOIN department d ON d.id = e.department_id\n"
           "  LEFT JOIN employee m ON m.id = e.manager_id");
    s.view("department_headcount",
           "  SELECT d.name AS department, o.city, COUNT(e.id) AS people\n"
           "  FROM department d\n"
           "  LEFT JOIN office o ON o.id = d.office_id\n"
           "  LEFT JOIN employee e ON e.department_id = d.id AND e.active = %TRUE%\n"
           "  GROUP BY d.id, d.name, o.city");

    Rng rng{ 20261002u };
    for (int r = 0; r < kRegions.size(); ++r)
        s.insert("region", { r + 1, kRegions.at(r) });
    for (const Country &c : kCountryList)
        s.insert("country", { c.code, c.name, c.region });
    for (int o = 0; o < kOffices.size(); ++o)
        s.insert("office", { o + 1, kOffices.at(o).first, kOffices.at(o).second,
                             QDate(2008, 3, 1).addDays(rng.range(0, 5000)) });
    for (int j = 0; j < kJobs.size(); ++j)
        s.insert("job", { j + 1, kJobs.at(j).title, double(kJobs.at(j).min), double(kJobs.at(j).max) });

    // People: the chief executive, a director per department, managers, then everyone else.
    const int nDepartments = kCompanyDepartments.size(), nEmployees = 1500;
    QHash<int, QList<int>> leads;           // department -> who people there report to
    QHash<int, QDate> hiredOn;
    QList<int> activeIds;
    auto person = [&](int id, int job, int dept, const QVariant &manager, const QDate &hired, bool active) {
        const QString first = rng.pick(kFirst), last = rng.pick(kLast);
        const QVariant office = rng.chance(5) ? null() : QVariant(rng.range(1, kOffices.size()));
        s.insert("employee", { id, first, last,
                               QStringLiteral("%1.%2%3@harborpine.com").arg(first.toLower(), last.toLower()).arg(id),
                               hired, job + 1, dept, manager, office, active });
        hiredOn.insert(id, hired);
        if (active)
            activeIds << id;
    };
    int id = 1;
    person(id, 0, 1, null(), QDate(2010, 4, 1), true);
    for (int d = 1; d <= nDepartments; ++d) {
        person(++id, 1, d, 1, QDate(2010, 6, 1).addDays(rng.range(0, 2500)), true);
        leads[d] << id;
    }
    while (id < nEmployees) {
        const int d = rng.range(1, nDepartments);
        const bool manager = rng.chance(9);
        const int job = manager ? 2 : rng.pick(kDepartmentJobs.at(d - 1));
        const QDate hired = QDate(2012, 1, 9).addDays(rng.range(0, 4800));
        person(++id, job, d, rng.pick(leads.value(d)), hired, rng.chance(92));
        if (manager)
            leads[d] << id;
    }
    // Each department's director manages it; offices by department.
    for (int d = 1; d <= nDepartments; ++d)
        s.insert("department", { d, kCompanyDepartments.at(d - 1),
                                 rng.chance(10) ? null() : QVariant(rng.range(1, kOffices.size())), leads.value(d).first() });

    // Salary: a raise every year or two since joining; the last one is current
    // (to_date NULL), unless they've left.
    const QDate today(2025, 9, 30);
    QSet<int> active(activeIds.begin(), activeIds.end());
    for (int e = 1; e <= nEmployees; ++e) {
        QDate from = hiredOn.value(e);
        qint64 amount = qint64(rng.range(45, 200)) * 100000;      // cents
        while (true) {
            const QDate next = from.addDays(rng.range(330, 720));
            const bool last = next > today;
            s.insert("salary", { e, from, last ? (active.contains(e) ? null() : QVariant(today.addDays(-rng.range(10, 300))))
                                               : QVariant(next.addDays(-1)),
                                 money(amount) });
            if (last)
                break;
            amount += amount * rng.range(2, 9) / 100;
            from = next;
        }
    }
    // Earlier jobs for a third of people.
    for (int e = 2; e <= nEmployees; ++e) {
        if (!rng.chance(33))
            continue;
        QDate end = hiredOn.value(e).addDays(rng.range(200, 900));
        const int jobs = rng.range(1, 2);
        for (int k = 0; k < jobs && end < today; ++k) {
            const QDate start = end.addDays(-rng.range(300, 1100));
            s.insert("job_history", { e, start, end, rng.range(3, kJobs.size()), rng.range(1, nDepartments) });
            end = start.addDays(-rng.range(10, 120));
        }
    }
    // Projects and who's on them.
    QSet<QString> names;
    for (int p = 1; p <= 64; ++p) {
        QString name;
        do { name = rng.pick(kProjectA) + QLatin1Char(' ') + rng.pick(kProjectB); } while (names.contains(name));
        names.insert(name);
        const QDate starts = QDate(2019, 1, 7).addDays(rng.range(0, 2300));
        s.insert("project", { p, QStringLiteral("PRJ-%1").arg(1000 + p), name,
                              rng.chance(8) ? null() : QVariant(rng.range(1, nDepartments)),
                              rng.chance(10) ? null() : money(qint64(rng.range(5, 400)) * 1000000),
                              starts, rng.chance(40) ? null() : QVariant(starts.addDays(rng.range(60, 700))) });
        QSet<int> team;
        const int size = rng.range(4, 60);
        for (int k = 0; k < size; ++k) {
            const int e = rng.pick(activeIds);
            if (team.contains(e))
                continue;
            team.insert(e);
            s.insert("assignment", { p, e, k == 0 ? QStringLiteral("Lead") : rng.pick(kRoles), 5 * rng.range(1, 20) });
        }
    }
    return s;
}

// ============================================================================
// music
// ============================================================================

const QStringList kGenres = { "Rock", "Jazz", "Metal", "Alternative", "Pop", "Blues", "Classical", "Electronic",
                              "Hip Hop", "R&B", "Soul", "Reggae", "Country", "Folk", "Latin", "Soundtrack", "Ambient", "World" };
const QStringList kMediaTypes = { "MPEG audio file", "AAC audio file", "FLAC audio file", "Protected AAC audio file", "WAV audio file" };
const QStringList kBandA = { "Velvet", "Electric", "Silver", "Midnight", "Paper", "Neon", "Hollow", "Golden", "Crimson",
                             "Static", "Northern", "Wild", "Glass", "Lunar", "Copper", "Quiet", "Feral", "Atomic" };
const QStringList kBandB = { "Foxes", "Harbours", "Engines", "Lanterns", "Saints", "Rivers", "Machines", "Ghosts",
                             "Pilots", "Wolves", "Orchards", "Signals", "Comets", "Tides", "Echoes", "Parades" };
const QStringList kWords = { "Love", "Night", "Summer", "Fire", "Rain", "City", "Heart", "Dream", "Road", "Light",
                             "Shadow", "Ocean", "Gold", "Morning", "Stone", "Wire", "Blue", "Home", "Ghost", "Highway",
                             "Mirror", "Thunder", "Velvet", "Static", "Garden", "Engine", "Satellite", "Winter" };
const QStringList kSongShapes = { "%1", "%1 %2", "The %1", "%1 of %2", "My %1", "%1 Song", "Into the %1",
                                  "%1 (Live)", "%1 in the %2", "No More %1", "%1 & %2", "Last %1" };
const QStringList kPlaylists = { "Favourites", "Road Trip", "Focus", "Late Night", "Workout", "Sunday Morning",
                                 "Party", "Chill", "Classics", "New Arrivals", "Staff Picks", "Rainy Day",
                                 "Deep Cuts", "Throwbacks", "Instrumentals", "Coffee Shop", "Dinner Party",
                                 "Running", "Study", "Summer Hits", "Heavy Rotation", "Discover", "Live Sets", "Acoustic" };

SampleSchema music()
{
    SampleSchema s(SampleDatabase::version("music"));
    s.addTable("artist", { C::id(), C::text("name", 120).required().unique() });
    s.addTable("genre", { C::id(), C::text("name", 40).required().unique() });
    s.addTable("media_type", { C::id(), C::text("name", 40).required().unique() });
    s.addTable("album", { C::id(), C::text("title", 160).required(),
                          C::integer("artist_id").required().references("artist", "id", "CASCADE"),
                          C::integer("released") });
    s.addTable("track", { C::id(), C::text("name", 200).required(),
                          C::integer("album_id").required().references("album", "id", "CASCADE"),
                          C::integer("genre_id").references("genre", "id", "SET NULL"),
                          C::integer("media_type_id").required().references("media_type"),
                          C::text("composer", 160),
                          C::integer("milliseconds").required().check("milliseconds > 0"),
                          C::integer("bytes"), C::decimal("unit_price", 4, 2).required().defaultTo(0.99) });
    s.addTable("employee", { C::id(), C::text("first_name", 40).required(), C::text("last_name", 40).required(),
                             C::text("title", 40), C::integer("reports_to").references("employee"),
                             C::date("hired"), C::text("email", 80).required().unique() });
    s.addTable("customer", { C::id(), C::text("first_name", 40).required(), C::text("last_name", 40).required(),
                             C::text("email", 120).required().unique(), C::text("city", 60), C::text("country", 40),
                             C::integer("support_rep_id").references("employee", "id", "SET NULL") });
    s.addTable("playlist", { C::id(), C::text("name", 80).required(),
                             C::integer("owner_id").references("customer", "id", "SET NULL") });
    s.addTable("playlist_track", { C::integer("playlist_id").required().references("playlist", "id", "CASCADE"),
                                   C::integer("track_id").required().references("track", "id", "CASCADE"),
                                   C::integer("track_no").required() });
    s.primaryKey("playlist_track", { "playlist_id", "track_id" });
    s.addTable("invoice", { C::id(), C::integer("customer_id").required().references("customer", "id", "CASCADE"),
                            C::dateTime("invoice_date").required(), C::text("billing_city", 60),
                            C::text("billing_country", 40), C::decimal("total", 10, 2).required() });
    s.addTable("invoice_line", { C::id(), C::integer("invoice_id").required().references("invoice", "id", "CASCADE"),
                                 C::integer("track_id").required().references("track"),
                                 C::decimal("unit_price", 4, 2).required(),
                                 C::integer("quantity").required().defaultTo(1).check("quantity > 0") });
    s.index("idx_album_artist", "album", { "artist_id" });
    s.index("idx_track_album", "track", { "album_id" });
    s.index("idx_invoice_customer", "invoice", { "customer_id", "invoice_date" });
    s.index("idx_invoice_line_invoice", "invoice_line", { "invoice_id" });
    s.view("track_sales",
           "  SELECT t.id AS track_id, t.name AS track, ar.name AS artist, g.name AS genre,\n"
           "         SUM(il.quantity) AS units, SUM(il.quantity * il.unit_price) AS revenue\n"
           "  FROM track t\n"
           "  JOIN album al ON al.id = t.album_id\n"
           "  JOIN artist ar ON ar.id = al.artist_id\n"
           "  LEFT JOIN genre g ON g.id = t.genre_id\n"
           "  JOIN invoice_line il ON il.track_id = t.id\n"
           "  GROUP BY t.id, t.name, ar.name, g.name");
    s.view("customer_spend",
           "  SELECT c.id AS customer_id, c.first_name, c.last_name, c.country,\n"
           "         e.last_name AS support_rep, COUNT(i.id) AS invoices, SUM(i.total) AS spent\n"
           "  FROM customer c\n"
           "  LEFT JOIN employee e ON e.id = c.support_rep_id\n"
           "  LEFT JOIN invoice i ON i.customer_id = c.id\n"
           "  GROUP BY c.id, c.first_name, c.last_name, c.country, e.last_name");

    Rng rng{ 20261003u };
    for (int g = 0; g < kGenres.size(); ++g)
        s.insert("genre", { g + 1, kGenres.at(g) });
    for (int m = 0; m < kMediaTypes.size(); ++m)
        s.insert("media_type", { m + 1, kMediaTypes.at(m) });

    const int nArtists = 220;
    QSet<QString> taken;
    QHash<int, int> artistGenre;
    for (int a = 1; a <= nArtists; ++a) {
        QString name;
        do {
            switch (rng.range(0, 3)) {
            case 0: name = QStringLiteral("The ") + rng.pick(kBandA) + QLatin1Char(' ') + rng.pick(kBandB); break;
            case 1: name = rng.pick(kFirst) + QLatin1Char(' ') + rng.pick(kLast); break;
            case 2: name = rng.pick(kBandA) + QLatin1Char(' ') + rng.pick(kBandB); break;
            default: name = rng.pick(kFirst) + QLatin1Char(' ') + rng.pick(kLast) + QStringLiteral(" Quartet"); break;
            }
        } while (taken.contains(name));
        taken.insert(name);
        s.insert("artist", { a, name });
        artistGenre.insert(a, rng.range(1, kGenres.size()));
    }

    auto title = [&](void) {
        QString shape = rng.pick(kSongShapes);
        const QString a = rng.pick(kWords), b = rng.pick(kWords);
        return shape.replace(QStringLiteral("%1"), a).replace(QStringLiteral("%2"), b);
    };
    struct Track { int id; qint64 cents; };
    QList<Track> tracks;
    int trackId = 0;
    for (int al = 1; al <= 520; ++al) {
        const int artist = rng.range(1, nArtists);
        s.insert("album", { al, title(), artist, rng.chance(3) ? null() : QVariant(rng.range(1965, 2025)) });
        const int count = rng.range(6, 15);
        const int media = rng.chance(70) ? 1 : rng.range(2, kMediaTypes.size());
        for (int k = 0; k < count; ++k) {
            const int ms = rng.range(95000, 480000);
            const qint64 cents = media == 4 ? 129 : 99;
            const QVariant genre = rng.chance(3) ? null() : QVariant(rng.chance(85) ? artistGenre.value(artist) : rng.range(1, kGenres.size()));
            const QVariant composer = rng.chance(35) ? null()
                : QVariant(rng.pick(kFirst) + QLatin1Char(' ') + rng.pick(kLast)
                           + (rng.chance(30) ? QStringLiteral(", ") + rng.pick(kFirst) + QLatin1Char(' ') + rng.pick(kLast) : QString()));
            s.insert("track", { ++trackId, title(), al, genre, media, composer, ms,
                                rng.chance(5) ? null() : QVariant(qint64(ms) * (media == 3 ? 110 : 32)), money(cents) });
            tracks << Track{ trackId, cents };
        }
    }

    // Staff: a manager, two leads, and support agents reporting to them.
    const QList<QPair<QString, int>> staff = {
        { "General Manager", 0 }, { "Sales Manager", 1 }, { "IT Manager", 1 },
        { "Sales Support Agent", 2 }, { "Sales Support Agent", 2 }, { "Sales Support Agent", 2 },
        { "Sales Support Agent", 2 }, { "Sales Support Agent", 2 }, { "IT Staff", 3 }, { "IT Staff", 3 } };
    QList<int> agents;
    for (int e = 0; e < staff.size(); ++e) {
        const QString first = rng.pick(kFirst), last = rng.pick(kLast);
        s.insert("employee", { e + 1, first, last, staff.at(e).first,
                               staff.at(e).second ? QVariant(staff.at(e).second) : null(),
                               QDate(2016, 2, 1).addDays(rng.range(0, 2400)),
                               QStringLiteral("%1.%2@recordstore.example").arg(first.toLower(), last.toLower()) });
        if (staff.at(e).first == QLatin1String("Sales Support Agent"))
            agents << e + 1;
    }

    const int nCustomers = 900;
    QHash<int, QPair<QString, QString>> where;
    for (int c = 1; c <= nCustomers; ++c) {
        const QString first = rng.pick(kFirst), last = rng.pick(kLast);
        const QString city = rng.pick(kCities), country = rng.pick(kCountries);
        s.insert("customer", { c, first, last, QStringLiteral("%1.%2%3@example.com").arg(first.toLower(), last.toLower()).arg(c),
                               city, country, rng.chance(6) ? null() : QVariant(rng.pick(agents)) });
        where.insert(c, { city, country });
    }

    for (int p = 1; p <= kPlaylists.size(); ++p) {
        s.insert("playlist", { p, kPlaylists.at(p - 1), rng.chance(40) ? null() : QVariant(rng.range(1, nCustomers)) });
        QSet<int> in;
        const int size = rng.range(40, 600);
        int no = 0;
        for (int k = 0; k < size; ++k) {
            const int t = rng.pick(tracks).id;
            if (in.contains(t))
                continue;
            in.insert(t);
            s.insert("playlist_track", { p, t, ++no });
        }
    }

    int lineId = 0;
    for (int i = 1; i <= 5200; ++i) {
        const int customer = rng.range(1, nCustomers);
        const QDateTime at(QDate(2021, 1, 4).addDays(rng.range(0, 1730)), QTime(rng.range(0, 23), rng.range(0, 59), rng.range(0, 59)));
        const int lines = rng.range(1, 6);
        qint64 total = 0;
        QVector<QVariantList> items;
        for (int l = 0; l < lines; ++l) {
            const Track &t = rng.pick(tracks);
            const int qty = rng.chance(92) ? 1 : 2;
            total += t.cents * qty;
            items << QVariantList{ ++lineId, i, t.id, money(t.cents), qty };
        }
        s.insert("invoice", { i, customer, at, where.value(customer).first, where.value(customer).second, money(total) });
        for (const QVariantList &r : items)
            s.insert("invoice_line", r);
    }
    return s;
}

} // namespace

// ============================================================================

QList<SampleDatabase::Info> SampleDatabase::catalogue()
{
    return {
        { "bookshop", "Bookshop", "Books, authors, customers, orders and reviews.",
          { "Single and composite keys", "CASCADE, SET NULL and plain references", "A view and a full-text table" },
          { "publisher", "author", "book", "customer", "orders", "order_item", "review" }, 42122 },
        { "university", "University", "Departments, instructors, students, courses, terms and enrollments.",
          { "A circular reference: departments and their chairs", "Courses that need other courses (many-to-many to itself)",
            "A three-column composite reference" },
          { "department", "instructor", "student", "course", "course_prereq", "term", "room", "section", "enrollment" }, 28540 },
        { "company", "Company", "An org chart: employees, managers, departments, offices, salaries and projects.",
          { "Employees managing employees", "A text key (country codes)", "Salary and job history keyed by date" },
          { "region", "country", "office", "department", "job", "employee", "salary", "job_history", "project", "assignment" }, 12560 },
        { "music", "Music Store", "Artists, albums and tracks; playlists; customers, invoices and their support reps.",
          { "Playlists: a many-to-many with an order", "Cascades three deep: artist to album to track",
            "Staff reporting to staff" },
          { "artist", "genre", "media_type", "album", "track", "employee", "customer", "playlist", "playlist_track", "invoice", "invoice_line" }, 36887 },
    };
}

SampleSchema SampleDatabase::build(const QString &id)
{
    if (id == QLatin1String("bookshop"))   return bookshop();
    if (id == QLatin1String("university")) return university();
    if (id == QLatin1String("company"))    return company();
    if (id == QLatin1String("music"))      return music();
    return SampleSchema();
}

int SampleDatabase::version(const QString &id)
{
    if (id == QLatin1String("bookshop")) return 2;
    return 1;
}

bool SampleDatabase::createFile(const QString &id, const QString &path, QString *error)
{
    const SampleSchema schema = build(id);
    if (schema.tables().isEmpty()) {
        if (error) *error = QStringLiteral("There's no sample called %1.").arg(id);
        return false;
    }
    QFile::remove(path);
    const QString conn = QStringLiteral("studio_sample_builder");
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(path);
        if (!db.open()) {
            if (error) *error = db.lastError().text();
        } else {
            ok = schema.execute(db, QStringLiteral("sqlite"), error);
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    if (!ok)
        QFile::remove(path);
    return ok;
}

bool SampleDatabase::load(const QString &id, QSqlDatabase db, const QString &dialect, QString *error)
{
    const SampleSchema schema = build(id);
    if (schema.tables().isEmpty()) {
        if (error) *error = QStringLiteral("There's no sample called %1.").arg(id);
        return false;
    }
    return schema.execute(db, dialect, error);
}
