#include "BudgetManager.hpp"
#include <tabulate/table.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <map>
#include <algorithm>
#include <ctime>
#include <filesystem>

using json = nlohmann::json;
using namespace tabulate;

BudgetManager::BudgetManager(bool readOnly) : readOnly(readOnly) {}

std::chrono::system_clock::time_point BudgetManager::parseDate(const std::string& dateStr) {
    std::tm tm = {};
    std::stringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (!ss.fail())
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::system_clock::now();
}

// =============================================
//  Add transaction
// =============================================
void BudgetManager::addTransaction() {
    if (readOnly) {
        std::cout << "\n  [!] Guest mode is read-only. Please login or register.\n";
        return;
    }

    Transaction t;
    std::cout << "\n  --- Add New Transaction ---\n";

    int typeChoice;
    std::cout << "  Type:\n    1. Income\n    2. Expense\n  Choose (1/2): ";
    std::cin >> typeChoice;
    std::cin.ignore();
    t.type = (typeChoice == 1) ? TransactionType::INCOME : TransactionType::EXPENSE;

    std::cout << "  Category (e.g. salary, food, rent, transport): ";
    std::getline(std::cin, t.category);
    if (t.category.empty()) t.category = "general";

    std::cout << "  Amount ($): ";
    while (!(std::cin >> t.amount) || t.amount <= 0) {
        std::cout << "  [!] Enter a valid positive amount: ";
        std::cin.clear();
        std::cin.ignore(256, '\n');
    }
    std::cin.ignore();

    std::cout << "  Date (YYYY-MM-DD): ";
    std::string dateStr;
    std::getline(std::cin, dateStr);
    std::tm tm = {};
    std::stringstream ss(dateStr);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (!ss.fail()) {
        t.date = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    } else {
        t.date = std::chrono::system_clock::now();
        std::cout << "  [!] Invalid date. Use YYYY-MM-DD (e.g. 2026-03-10). Using today.\n";
    }

    std::cout << "  Note (press Enter to skip): ";
    std::getline(std::cin, t.note);
    if (t.note.empty()) t.note = "-";

    t.id = Transaction::generateId();
    transactions.push_back(t);
    std::cout << "\n  [+] Transaction added! (ID: " << t.id << ")\n";

    checkWarnings();
}

// =============================================
//  View transactions using tabulate
// =============================================
void BudgetManager::viewTransactions(const std::string& filterMonth,
                                      const std::string& filterCategory,
                                      const std::string& filterType) {
    if (transactions.empty()) {
        std::cout << "\n  [!] No transactions found.\n";
        return;
    }

    // Apply filters
    std::vector<Transaction> filtered;
    for (const auto& t : transactions) {
        bool ok = true;
        if (!filterMonth.empty()    && t.getMonthString() != filterMonth) ok = false;
        if (!filterType.empty()     && t.getTypeString()  != filterType)  ok = false;
        if (!filterCategory.empty()) {
            std::string tCat = t.category, fCat = filterCategory;
            std::transform(tCat.begin(), tCat.end(), tCat.begin(), ::tolower);
            std::transform(fCat.begin(), fCat.end(), fCat.begin(), ::tolower);
            if (tCat.find(fCat) == std::string::npos) ok = false;
        }
        if (ok) filtered.push_back(t);
    }

    if (filtered.empty()) {
        std::cout << "\n  [!] No transactions match your filter.\n";
        return;
    }

    // Use tabulate for pretty table
    Table table;

    // Header row
    table.add_row({"ID", "Type", "Category", "Amount", "Date", "Note"});

    // Style header
    table[0].format()
        .font_style({FontStyle::bold})
        .font_color(Color::cyan)
        .border_top("-")
        .border_bottom("-");

    // Data rows
    for (const auto& t : filtered) {
        std::ostringstream amt;
        amt << "$" << std::fixed << std::setprecision(2) << t.amount;

        table.add_row({
            std::to_string(t.id),
            t.getTypeString(),
            t.category,
            amt.str(),
            t.getDateString(),
            t.note
        });

        // Color income green, expense red
        size_t row = table.size() - 1;
        if (t.type == TransactionType::INCOME)
            table[row][1].format().font_color(Color::green);
        else
            table[row][1].format().font_color(Color::red);
    }

    std::cout << "\n" << table << "\n";
    std::cout << "  Showing: " << filtered.size() << " / " << transactions.size() << " record(s)\n";
}

// =============================================
//  Filter menu
// =============================================
void BudgetManager::filterMenu() {
    std::cout << "\n  --- Filter Transactions ---\n";
    std::cout << "  1. Filter by Month\n";
    std::cout << "  2. Filter by Category\n";
    std::cout << "  3. Filter by Type (income/expense)\n";
    std::cout << "  4. Show All\n";
    std::cout << "  Choose: ";

    int choice; std::cin >> choice; std::cin.ignore();

    if (choice == 1) {
        std::cout << "  Enter month (YYYY-MM): ";
        std::string m; std::getline(std::cin, m);
        viewTransactions(m, "", "");
    } else if (choice == 2) {
        std::cout << "  Enter category: ";
        std::string c; std::getline(std::cin, c);
        viewTransactions("", c, "");
    } else if (choice == 3) {
        std::cout << "  Enter type (income/expense): ";
        std::string tp; std::getline(std::cin, tp);
        viewTransactions("", "", tp);
    } else {
        viewTransactions();
    }
}

// =============================================
//  Monthly summary using tabulate
// =============================================
void BudgetManager::monthlySummary() {
    if (transactions.empty()) {
        std::cout << "\n  [!] No transactions to summarize.\n";
        return;
    }

    std::map<std::string, double> incomeMap, expenseMap;
    for (const auto& t : transactions) {
        if (t.type == TransactionType::INCOME)
            incomeMap[t.getMonthString()] += t.amount;
        else
            expenseMap[t.getMonthString()] += t.amount;
    }

    std::map<std::string, bool> allMonths;
    for (auto& p : incomeMap)  allMonths[p.first] = true;
    for (auto& p : expenseMap) allMonths[p.first] = true;

    Table table;
    table.add_row({"Month", "Income", "Expense", "Balance", "Status"});
    table[0].format().font_style({FontStyle::bold}).font_color(Color::cyan);

    double grandIncome = 0, grandExpense = 0;
    for (auto& p : allMonths) {
        std::string m = p.first;
        double inc = incomeMap.count(m)  ? incomeMap[m]  : 0;
        double exp = expenseMap.count(m) ? expenseMap[m] : 0;
        double bal = inc - exp;
        grandIncome += inc; grandExpense += exp;

        std::ostringstream incStr, expStr, balStr;
        incStr << "$" << std::fixed << std::setprecision(2) << inc;
        expStr << "$" << std::fixed << std::setprecision(2) << exp;
        balStr << "$" << std::fixed << std::setprecision(2) << bal;

        std::string status = (bal < 0) ? "Over budget!" : "OK";
        table.add_row({m, incStr.str(), expStr.str(), balStr.str(), status});

        size_t row = table.size() - 1;
        if (bal < 0)
            table[row][4].format().font_color(Color::red);
        else
            table[row][4].format().font_color(Color::green);
    }

    // Total row
    std::ostringstream tInc, tExp, tBal;
    tInc << "$" << std::fixed << std::setprecision(2) << grandIncome;
    tExp << "$" << std::fixed << std::setprecision(2) << grandExpense;
    tBal << "$" << std::fixed << std::setprecision(2) << (grandIncome - grandExpense);
    table.add_row({"TOTAL", tInc.str(), tExp.str(), tBal.str(), ""});
    table[table.size()-1].format().font_style({FontStyle::bold});

    std::cout << "\n  MONTHLY SUMMARY\n" << table << "\n";
}

// =============================================
//  Category breakdown with tabulate
// =============================================
void BudgetManager::categoryBreakdown() {
    if (transactions.empty()) {
        std::cout << "\n  [!] No transactions found.\n";
        return;
    }

    std::map<std::string, double> catMap;
    double totalExpense = 0;

    for (const auto& t : transactions) {
        if (t.type == TransactionType::EXPENSE) {
            std::string cat = t.category;
            std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
            catMap[cat] += t.amount;
            totalExpense += t.amount;
        }
    }

    if (totalExpense == 0) { std::cout << "\n  [!] No expenses found.\n"; return; }

    std::vector<std::pair<std::string, double>> sorted(catMap.begin(), catMap.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const std::pair<std::string,double>& a, const std::pair<std::string,double>& b) {
            return a.second > b.second;
        });

    Table table;
    table.add_row({"Category", "Amount", "Percentage", "Bar"});
    table[0].format().font_style({FontStyle::bold}).font_color(Color::cyan);

    for (auto& p : sorted) {
        double pct = (p.second / totalExpense) * 100.0;
        int bars = (int)(pct / 5);

        std::string bar = "";
        for (int i = 0; i < bars; i++) bar += "#";
        for (int i = bars; i < 20; i++) bar += "-";

        std::ostringstream amtStr, pctStr;
        amtStr << "$" << std::fixed << std::setprecision(2) << p.second;
        pctStr << std::fixed << std::setprecision(1) << pct << "%";

        table.add_row({p.first, amtStr.str(), pctStr.str(), bar});
    }

    std::ostringstream totalStr;
    totalStr << "$" << std::fixed << std::setprecision(2) << totalExpense;
    table.add_row({"TOTAL", totalStr.str(), "100%", ""});
    table[table.size()-1].format().font_style({FontStyle::bold});

    std::cout << "\n  CATEGORY BREAKDOWN\n" << table << "\n";
}

// =============================================
//  Warning banner
// =============================================
void BudgetManager::checkWarnings() {
    std::map<std::string, double> incomeMap, expenseMap;
    for (const auto& t : transactions) {
        if (t.type == TransactionType::INCOME)
            incomeMap[t.getMonthString()] += t.amount;
        else
            expenseMap[t.getMonthString()] += t.amount;
    }

    bool warned = false;
    for (auto& p : expenseMap) {
        double inc = incomeMap.count(p.first) ? incomeMap[p.first] : 0;
        if (p.second > inc) {
            if (!warned) { std::cout << "\n  !! WARNING !!\n"; warned = true; }
            std::cout << "  [!] " << p.first
                      << ": Expenses ($" << std::fixed << std::setprecision(2) << p.second
                      << ") exceed Income ($" << inc << ")!\n";
        }
    }
    if (!warned && !transactions.empty())
        std::cout << "\n  [OK] Budget is on track!\n";
}

// =============================================
//  Edit transaction
// =============================================
void BudgetManager::editTransaction() {
    if (readOnly) { std::cout << "\n  [!] Guest mode is read-only.\n"; return; }
    if (transactions.empty()) { std::cout << "\n  [!] No transactions to edit.\n"; return; }

    viewTransactions();
    std::cout << "\n  Enter ID to edit: ";
    int id; std::cin >> id; std::cin.ignore();

    auto it = std::find_if(transactions.begin(), transactions.end(),
        [id](const Transaction& t) { return t.id == id; });

    if (it == transactions.end()) { std::cout << "  [!] ID not found.\n"; return; }

    std::cout << "\n  --- Edit Transaction (ID: " << id << ") ---\n";
    std::cout << "  Leave blank to keep current value.\n\n";

    std::string input;
    std::cout << "  Category [" << it->category << "]: ";
    std::getline(std::cin, input);
    if (!input.empty()) it->category = input;

    std::cout << "  Amount [$" << it->amount << "]: ";
    std::getline(std::cin, input);
    if (!input.empty()) { try { it->amount = std::stod(input); } catch (...) {} }

    std::cout << "  Date [" << it->getDateString() << "] (YYYY-MM-DD): ";
    std::getline(std::cin, input);
    if (!input.empty()) {
        std::tm tm = {};
        std::stringstream ss(input);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        if (!ss.fail())
            it->date = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    std::cout << "  Note [" << it->note << "]: ";
    std::getline(std::cin, input);
    if (!input.empty()) it->note = input;

    std::cout << "\n  [+] Transaction " << id << " updated!\n";
}

// =============================================
//  Delete transaction
// =============================================
void BudgetManager::deleteTransaction() {
    if (readOnly) { std::cout << "\n  [!] Guest mode is read-only.\n"; return; }
    if (transactions.empty()) { std::cout << "\n  [!] No transactions to delete.\n"; return; }

    viewTransactions();
    std::cout << "\n  Enter ID to delete: ";
    int id; std::cin >> id;

    auto it = std::find_if(transactions.begin(), transactions.end(),
        [id](const Transaction& t) { return t.id == id; });

    if (it == transactions.end()) { std::cout << "  [!] ID not found.\n"; return; }
    transactions.erase(it);
    std::cout << "  [+] Transaction " << id << " deleted.\n";
}

// =============================================
//  Sample data for guest
// =============================================
void BudgetManager::loadSampleData() {
    transactions.clear();
    auto makeDate = [](const std::string& s) {
        std::tm tm = {};
        std::stringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    };
    transactions.push_back({1, TransactionType::INCOME,  "salary",    3000.0, makeDate("2026-03-01"), "March salary"});
    transactions.push_back({2, TransactionType::EXPENSE, "food",       500.0, makeDate("2026-03-05"), "groceries"});
    transactions.push_back({3, TransactionType::EXPENSE, "rent",       800.0, makeDate("2026-03-01"), "monthly rent"});
    transactions.push_back({4, TransactionType::EXPENSE, "transport",  150.0, makeDate("2026-03-10"), "bus pass"});
    transactions.push_back({5, TransactionType::INCOME,  "freelance",  500.0, makeDate("2026-03-15"), "web project"});
    transactions.push_back({6, TransactionType::EXPENSE, "food",       200.0, makeDate("2026-03-20"), "dining out"});
    std::cout << "\n  [+] Sample data loaded! (Read-only demo)\n";
}

// =============================================
//  Save to Data/budget.json using nlohmann
// =============================================
void BudgetManager::save(const User& user) {
    if (user.isGuest) { std::cout << "  [!] Guest data is not saved.\n"; return; }

    std::filesystem::create_directories("Data");
    std::string filename = "Data/budget.json";

    // Load existing data first (preserve other users)
    json root;
    std::ifstream inFile(filename);
    if (inFile.is_open()) {
        try { root = json::parse(inFile); } catch (...) { root = json::object(); }
        inFile.close();
    }
    if (!root.is_object()) root = json::object();

    // Build transaction array for this user
    json txArray = json::array();
    for (const auto& t : transactions)
        txArray.push_back(t.toJson());

    root[user.username]["transactions"] = txArray;

    std::ofstream outFile(filename);
    if (!outFile) { std::cout << "  [!] Could not save Data/budget.json\n"; return; }
    outFile << root.dump(2);
    outFile.close();

    std::cout << "  [+] Saved to 'Data/budget.json' (user: " << user.username << ")!\n";
}

// =============================================
//  Load from Data/budget.json using nlohmann
// =============================================
void BudgetManager::load(const User& user) {
    if (user.isGuest) { loadSampleData(); return; }

    std::string filename = "Data/budget.json";
    std::ifstream file(filename);
    if (!file) {
        std::cout << "  [!] No saved data for '" << user.username << "'. Starting fresh.\n";
        return;
    }

    try {
        json root = json::parse(file);
        if (!root.contains(user.username)) {
            std::cout << "  [!] No data for '" << user.username << "'. Starting fresh.\n";
            return;
        }

        transactions.clear();
        for (auto& j : root[user.username]["transactions"])
            transactions.push_back(Transaction::fromJson(j));

        std::cout << "  [+] Loaded " << transactions.size()
                  << " transaction(s) for '" << user.username << "' from Data/budget.json.\n";
    } catch (...) {
        std::cout << "  [!] Error reading Data/budget.json\n";
    }
    file.close();
}