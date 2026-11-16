#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <algorithm>

using namespace std;

// ==================== ANSI COLOR CODES ====================
const string RESET   = "\033[0m";
const string BOLD    = "\033[1m";
const string RED     = "\033[31m";
const string GREEN   = "\033[32m";
const string YELLOW  = "\033[33m";
const string CYAN    = "\033[36m";
const string MAGENTA = "\033[35m";

// ==================== DATA STRUCTURES ====================
struct Candle {
    string timestamp;
    double open, high, low, close, volume;
};

struct Instrument {
    int id;
    string symbol;
    string displayName;
    int lotSize;
    string segment;
    double fallbackBasePrice;
};

struct Indicators {
    double ltp;
    double ema20;
    double vwap;
    double pivot;
    double r1;
    double s1;
    string signalText;
    string dataSource;
};

struct RiskValidationResult {
    bool approved;
    vector<string> reasons;
    double rrRatio;
    int totalQty;
    double totalTradeRiskRs;
    double potentialRewardRs;
    double maxAllowedRiskRs;
    double riskPctOfCapital;
    int recommendedMaxLots;
};

struct Trade {
    int tradeId;
    string symbol;
    string side;        // BUY or SELL
    int lots;
    int quantity;
    double entryPrice;
    double stopLoss;
    double targetPrice;
    double exitPrice;
    string status;      // OPEN or CLOSED
    double grossPnl;
    double taxesCharges;
    double netPnl;
    string setupTag;
    string timestamp;
};

// ==================== CLASS 1: QUANT & RISK ENGINE ====================
class QuantEngine {
public:
    static vector<Candle> fetchCandles(const Instrument& inst, string& outSource) {
        vector<Candle> candles;
        string cmd = "python -c \"import yfinance as yf; df = yf.Ticker('" + inst.symbol +
                     "').history(period='5d', interval='5m'); "
                     "df.tail(75).to_csv('live_feed.csv')\" >nul 2>&1";
        
        int ret = system(cmd.c_str());
        ifstream file("live_feed.csv");
        
        if (ret == 0 && file.is_open()) {
            string line;
            getline(file, line); // Skip CSV Header
            while (getline(file, line)) {
                if (line.empty()) continue;
                stringstream ss(line);
                string ts, o, h, l, c, v;
                getline(ss, ts, ',');
                getline(ss, o, ',');
                getline(ss, h, ',');
                getline(ss, l, ',');
                getline(ss, c, ',');
                getline(ss, v, ',');
                try {
                    Candle candle;
                    candle.timestamp = ts.substr(0, 16);
                    candle.open = stod(o);
                    candle.high = stod(h);
                    candle.low = stod(l);
                    candle.close = stod(c);
                    candle.volume = stod(v);
                    if (candle.volume <= 0) candle.volume = 1.0;
                    candles.push_back(candle);
                } catch (...) {}
            }
            file.close();
        }

        if (candles.size() >= 20) {
            outSource = "LIVE NSE FEED (C++ <-> yfinance Bridge)";
            return candles;
        }

        outSource = "OFFLINE C++ DETERMINISTIC ENGINE (Wi-Fi Safe Mode)";
        candles.clear();
        srand(42);
        double price = inst.fallbackBasePrice;
        for (int i = 0; i < 60; ++i) {
            double delta = ((rand() % 200) - 95) * (inst.fallbackBasePrice * 0.00003);
            double openP = price;
            double closeP = price + delta;
            double highP = max(openP, closeP) + (inst.fallbackBasePrice * 0.0008);
            double lowP = min(openP, closeP) - (inst.fallbackBasePrice * 0.0008);
            double vol = 25000 + (rand() % 80000);
            candles.push_back({"2026-11-10 10:" + to_string(10 + (i % 49)), openP, highP, lowP, closeP, vol});
            price = closeP;
        }
        return candles;
    }

    static Indicators computeIndicators(const Instrument& inst) {
        string source;
        vector<Candle> candles = fetchCandles(inst, source);

        double k = 2.0 / (20.0 + 1.0);
        double ema20 = candles[0].close;
        
        double cumTypPriceVol = 0.0;
        double cumVol = 0.0;
        double sessionHigh = candles[0].high;
        double sessionLow = candles[0].low;

        for (size_t i = 0; i < candles.size(); ++i) {
            const Candle& c = candles[i];
            ema20 = (c.close * k) + (ema20 * (1.0 - k));

            double typicalPrice = (c.high + c.low + c.close) / 3.0;
            cumTypPriceVol += (typicalPrice * c.volume);
            cumVol += c.volume;

            if (c.high > sessionHigh) sessionHigh = c.high;
            if (c.low < sessionLow) sessionLow = c.low;
        }

        double ltp = candles.back().close;
        double vwap = (cumVol > 0) ? (cumTypPriceVol / cumVol) : ltp;
        double pivot = (sessionHigh + sessionLow + ltp) / 3.0;
        double r1 = (2.0 * pivot) - sessionLow;
        double s1 = (2.0 * pivot) - sessionHigh;

        string sig;
        if (ltp > ema20 && ltp > vwap) {
            sig = "BULLISH (Price Above 20 EMA & VWAP)";
        } else if (ltp < ema20 && ltp < vwap) {
            sig = "BEARISH (Price Below 20 EMA & VWAP)";
        } else {
            sig = "SIDEWAYS / CHOPPY (Await Clear Breakout)";
        }

        return {ltp, ema20, vwap, pivot, r1, s1, sig, source};
    }

    static RiskValidationResult validatePreTradeRisk(
        double capital, double maxRiskPct, const string& side,
        double entry, double sl, double target, int lotSize, int requestedLots
    ) {
        RiskValidationResult res;
        res.approved = true;

        if (side == "BUY") {
            if (sl >= entry) res.reasons.push_back("For a BUY order, Stop-Loss must be strictly BELOW Entry Price.");
            if (target <= entry) res.reasons.push_back("For a BUY order, Target Price must be strictly ABOVE Entry Price.");
        } else {
            if (sl <= entry) res.reasons.push_back("For a SELL order, Stop-Loss must be strictly ABOVE Entry Price.");
            if (target >= entry) res.reasons.push_back("For a SELL order, Target Price must be strictly BELOW Entry Price.");
        }

        double riskPerUnit = fabs(entry - sl);
        double rewardPerUnit = fabs(target - entry);

        if (riskPerUnit < 0.0001) {
            res.approved = false;
            res.reasons.push_back("Stop-Loss cannot equal Entry Price.");
            res.totalQty = 0;
            return res;
        }

        res.rrRatio = rewardPerUnit / riskPerUnit;
        res.totalQty = requestedLots * lotSize;
        res.totalTradeRiskRs = riskPerUnit * res.totalQty;
        res.potentialRewardRs = rewardPerUnit * res.totalQty;
        res.maxAllowedRiskRs = capital * (maxRiskPct / 100.0);
        res.riskPctOfCapital = (res.totalTradeRiskRs / capital) * 100.0;
        res.recommendedMaxLots = static_cast<int>(floor(res.maxAllowedRiskRs / (riskPerUnit * lotSize)));
        if (res.recommendedMaxLots < 0) res.recommendedMaxLots = 0;

        if (res.rrRatio < 1.5) {
            ostringstream oss;
            oss << fixed << setprecision(2) << "Unfavorable Risk-to-Reward (1:" << res.rrRatio << "). Minimum required is 1:1.50.";
            res.reasons.push_back(oss.str());
        }

        if (res.totalTradeRiskRs > res.maxAllowedRiskRs) {
            ostringstream oss;
            oss << fixed << setprecision(2)
                << "Risk Limit Breached! Trade risks Rs." << res.totalTradeRiskRs
                << " (" << res.riskPctOfCapital << "% of capital), exceeding the "
                << maxRiskPct << "% limit (Rs." << res.maxAllowedRiskRs << "). Max safe lots: "
                << res.recommendedMaxLots << ".";
            res.reasons.push_back(oss.str());
        }

        res.approved = res.reasons.empty();
        return res;
    }

    static double calculateIndianFnoCharges(double entryPrice, double exitPrice, int quantity) {
        double buyTurnover = entryPrice * quantity;
        double sellTurnover = exitPrice * quantity;
        double totalTurnover = buyTurnover + sellTurnover;

        double brokerage = 40.0;
        double stt = sellTurnover * 0.0002;
        double exchangeTxn = totalTurnover * 0.0000188;
        double sebiFees = totalTurnover * 0.000001;
        double stampDuty = buyTurnover * 0.00002;
        double gst = 0.18 * (brokerage + exchangeTxn + sebiFees);

        return brokerage + stt + exchangeTxn + sebiFees + stampDuty + gst;
    }
};

// ==================== CLASS 2: DATABASE MANAGER ====================
class DatabaseManager {
private:
    string dbFile = "trades_ledger.csv";
    vector<Trade> trades;

    string currentTimestamp() {
        time_t now = time(0);
        tm* ltm = localtime(&now);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", ltm);
        return string(buf);
    }

public:
    double totalCapital = 500000.0;
    double maxRiskPct = 2.0;
    double maxDailyLoss = 15000.0;
    vector<Instrument> instruments;

    DatabaseManager() {
        instruments = {
            {1, "^NSEI",       "NIFTY 50 INDEX (F&O)",   75,  "FUTIDX", 25040.0},
            {2, "^NSEBANK",    "BANK NIFTY INDEX (F&O)", 30,  "FUTIDX", 51600.0},
            {3, "RELIANCE.NS", "RELIANCE INDUSTRIES",    250, "FUTSTK", 1295.0},
            {4, "HDFCBANK.NS", "HDFC BANK LTD",          550, "FUTSTK", 1730.0},
            {5, "TCS.NS",      "TATA CONSULTANCY SERV",  175, "FUTSTK", 4120.0},
            {6, "ICICIBANK.NS","ICICI BANK LTD",         700, "FUTSTK", 1280.0}
        };
        loadFromDisk();
    }

    void loadFromDisk() {
        trades.clear();
        ifstream file(dbFile);
        if (!file.is_open()) {
            seedDemoTrades();
            saveToDisk();
            return;
        }
        string line;
        getline(file, line);
        while (getline(file, line)) {
            if (line.empty()) continue;
            stringstream ss(line);
            string item;
            Trade t;
            try {
                getline(ss, item, ','); t.tradeId = stoi(item);
                getline(ss, t.symbol, ',');
                getline(ss, t.side, ',');
                getline(ss, item, ','); t.lots = stoi(item);
                getline(ss, item, ','); t.quantity = stoi(item);
                getline(ss, item, ','); t.entryPrice = stod(item);
                getline(ss, item, ','); t.stopLoss = stod(item);
                getline(ss, item, ','); t.targetPrice = stod(item);
                getline(ss, item, ','); t.exitPrice = stod(item);
                getline(ss, t.status, ',');
                getline(ss, item, ','); t.grossPnl = stod(item);
                getline(ss, item, ','); t.taxesCharges = stod(item);
                getline(ss, item, ','); t.netPnl = stod(item);
                getline(ss, t.setupTag, ',');
                getline(ss, t.timestamp, ',');
                trades.push_back(t);
            } catch (...) {}
        }
        file.close();
        if (trades.empty()) {
            seedDemoTrades();
            saveToDisk();
        }
    }

    void saveToDisk() {
        ofstream file(dbFile);
        file << "trade_id,symbol,side,lots,quantity,entry_price,stop_loss,target_price,exit_price,status,gross_pnl,taxes_charges,net_pnl,setup_tag,timestamp\n";
        for (const auto& t : trades) {
            file << t.tradeId << "," << t.symbol << "," << t.side << ","
                 << t.lots << "," << t.quantity << ","
                 << fixed << setprecision(2) << t.entryPrice << ","
                 << t.stopLoss << "," << t.targetPrice << "," << t.exitPrice << ","
                 << t.status << "," << t.grossPnl << "," << t.taxesCharges << ","
                 << t.netPnl << "," << t.setupTag << "," << t.timestamp << "\n";
        }
        file.close();
    }

    void seedDemoTrades() {
        trades = {
            {1, "^NSEI", "BUY", 2, 150, 24820.0, 24770.0, 24920.0, 24915.0, "CLOSED", 14250.0, 285.40, 13964.60, "VWAP + 20 EMA Bullish Bounce", "2026-11-05 10:15"},
            {2, "^NSEBANK", "SELL", 2, 60, 51800.0, 51920.0, 51550.0, 51560.0, "CLOSED", 14400.0, 260.15, 14139.85, "R1 Pivot Rejection Short", "2026-11-06 11:05"},
            {3, "RELIANCE.NS", "BUY", 1, 250, 1290.0, 1280.0, 1310.0, 1281.0, "CLOSED", -2250.0, 195.80, -2445.80, "VWAP Breakout Failure", "2026-11-07 09:45"},
            {4, "^NSEI", "BUY", 2, 150, 24950.0, 24900.0, 25050.0, 25040.0, "CLOSED", 13500.0, 288.10, 13211.90, "S1 Support + 20 EMA Reclaim", "2026-11-08 13:20"},
            {5, "HDFCBANK.NS", "BUY", 1, 550, 1720.0, 1708.0, 1745.0, 1742.0, "CLOSED", 12100.0, 310.50, 11789.50, "Institutional Volume Spike", "2026-11-09 14:50"},
            {6, "^NSEI", "BUY", 1, 75, 25010.0, 24960.0, 25110.0, 0.0, "OPEN", 0.0, 0.0, 0.0, "VWAP + 20 EMA Bullish Bounce", currentTimestamp()}
        };
    }

    void insertOpenTrade(const string& sym, const string& side, int lots, int qty, double entry, double sl, double tg, const string& setup) {
        int nextId = trades.empty() ? 1 : (trades.back().tradeId + 1);
        trades.push_back({nextId, sym, side, lots, qty, entry, sl, tg, 0.0, "OPEN", 0.0, 0.0, 0.0, setup, currentTimestamp()});
        saveToDisk();
    }

    bool closeTrade(int id, double exitPx, Trade& outClosed) {
        for (auto& t : trades) {
            if (t.tradeId == id && t.status == "OPEN") {
                t.exitPrice = exitPx;
                t.status = "CLOSED";
                t.grossPnl = (t.side == "BUY") ? ((exitPx - t.entryPrice) * t.quantity) : ((t.entryPrice - exitPx) * t.quantity);
                t.taxesCharges = QuantEngine::calculateIndianFnoCharges(t.entryPrice, exitPx, t.quantity);
                t.netPnl = t.grossPnl - t.taxesCharges;
                outClosed = t;
                saveToDisk();
                return true;
            }
        }
        return false;
    }

    vector<Trade> getTradesByStatus(const string& status) {
        vector<Trade> filtered;
        for (const auto& t : trades) {
            if (t.status == status) filtered.push_back(t);
        }
        return filtered;
    }
};

// ==================== CLASS 3: CLI TERMINAL CONTROLLER ====================
class QuantDeskTerminal {
private:
    DatabaseManager db;
    int activeInstrumentIdx = 0;

    void printBanner() {
        cout << "\n" << CYAN << BOLD
             << "========================================================================================\n"
             << "   QUANTDESK F&O (C++17 ENGINE) | INSTITUTIONAL STOCK & RISK MANAGEMENT SYSTEM\n"
             << "========================================================================================" << RESET << "\n";
        cout << " Capital: " << GREEN << "Rs." << fixed << setprecision(0) << db.totalCapital << RESET
             << " | Max Risk/Trade: " << YELLOW << db.maxRiskPct << "% (Rs." << (db.totalCapital * db.maxRiskPct / 100.0) << ")" << RESET
             << " | Active Instrument: " << MAGENTA << BOLD << db.instruments[activeInstrumentIdx].displayName
             << " (Lot: " << db.instruments[activeInstrumentIdx].lotSize << ")" << RESET << "\n";
        cout << " Team: Satvik (C++ Architecture & DB) | Akshit (F&O Quant Logic) | Siddhesh (System Design)\n"
             << "----------------------------------------------------------------------------------------\n";
    }

public:
    void module1_LiveScanner() {
        const Instrument& inst = db.instruments[activeInstrumentIdx];
        cout << "\n" << YELLOW << "[Fetching market feed & computing vectorized C++ indicators for " << inst.symbol << "...]" << RESET << "\n";
        Indicators ind = QuantEngine::computeIndicators(inst);

        cout << "\n" << BOLD << "--- MODULE 1: LIVE TECHNICAL SCANNER (" << inst.displayName << ") ---" << RESET << "\n";
        cout << " Data Feed Status : " << CYAN << ind.dataSource << RESET << "\n";
        cout << fixed << setprecision(2);
        cout << "----------------------------------------------------------------------------------------\n";
        cout << " Last Traded Price (LTP) : Rs." << setw(10) << ind.ltp
             << " | 20 EMA (Intraday) : Rs." << setw(10) << ind.ema20 << "\n";
        cout << " Intraday VWAP           : Rs." << setw(10) << ind.vwap
             << " | Central Pivot (P) : Rs." << setw(10) << ind.pivot << "\n";
        cout << " Resistance 1 (R1)       : Rs." << setw(10) << ind.r1
             << " | Support 1 (S1)    : Rs." << setw(10) << ind.s1 << "\n";
        cout << "----------------------------------------------------------------------------------------\n";

        if (ind.signalText.find("BULLISH") != string::npos) {
            cout << " QUANT ENGINE SIGNAL: " << GREEN << BOLD << "[ " << ind.signalText << " ]" << RESET << "\n";
        } else if (ind.signalText.find("BEARISH") != string::npos) {
            cout << " QUANT ENGINE SIGNAL: " << RED << BOLD << "[ " << ind.signalText << " ]" << RESET << "\n";
        } else {
            cout << " QUANT ENGINE SIGNAL: " << YELLOW << BOLD << "[ " << ind.signalText << " ]" << RESET << "\n";
        }
    }

    void module2_RiskGatekeeper() {
        const Instrument& inst = db.instruments[activeInstrumentIdx];
        Indicators ind = QuantEngine::computeIndicators(inst);

        cout << "\n" << BOLD << "--- MODULE 2: PRE-TRADE RISK GATEKEEPER & POSITION SIZER ---" << RESET << "\n";
        cout << " Active Contract: " << CYAN << inst.displayName << " (1 Lot = " << inst.lotSize << " Qty | Current LTP: Rs." << fixed << setprecision(2) << ind.ltp << ")" << RESET << "\n";

        string side;
        int lots;
        double entry, sl, target;
        int setupChoice;

        cout << " Enter Order Side (BUY / SELL): ";
        cin >> side;
        transform(side.begin(), side.end(), side.begin(), ::toupper);
        if (side != "BUY" && side != "SELL") side = "BUY";

        cout << " Enter Number of Lots (Try 25 to test Gatekeeper rejection, or 1-2 for valid trade): ";
        cin >> lots;
        cout << " Enter Entry Price (Rs.) [e.g. " << ind.ltp << "]: ";
        cin >> entry;
        cout << " Enter Stop-Loss Price (Rs.): ";
        cin >> sl;
        cout << " Enter Target Price (Rs.): ";
        cin >> target;

        cout << " Select Technical Setup Tag:\n"
             << "   1. VWAP + 20 EMA Bullish Bounce\n"
             << "   2. R1/S1 Pivot Breakout\n"
             << "   3. Opening Range Breakout (ORB)\n"
             << "   4. Institutional Volume Spike\n"
             << " Choice (1-4): ";
        cin >> setupChoice;

        string setupTag = "VWAP + 20 EMA Bullish Bounce";
        if (setupChoice == 2) setupTag = "R1/S1 Pivot Breakout";
        else if (setupChoice == 3) setupTag = "Opening Range Breakout (ORB)";
        else if (setupChoice == 4) setupTag = "Institutional Volume Spike";

        RiskValidationResult val = QuantEngine::validatePreTradeRisk(
            db.totalCapital, db.maxRiskPct, side, entry, sl, target, inst.lotSize, lots
        );

        cout << "\n" << BOLD << "================ GATEKEEPER AUDIT REPORT ================" << RESET << "\n";
        cout << fixed << setprecision(2);
        cout << " Total Quantity       : " << val.totalQty << " Qty (" << lots << " Lots)\n";
        cout << " Risk-to-Reward Ratio : 1 : " << val.rrRatio << "\n";
        cout << " Capital at Risk      : Rs." << val.totalTradeRiskRs << " (" << val.riskPctOfCapital << "% of Capital)\n";
        cout << " Max Safe Lots (2%)   : " << val.recommendedMaxLots << " Lots | Potential Profit: Rs." << val.potentialRewardRs << "\n";

        if (val.approved) {
            cout << GREEN << BOLD << "\n [PASSED] RISK AUDIT APPROVED! Logging Transaction to Database..." << RESET << "\n";
            db.insertOpenTrade(inst.symbol, side, lots, val.totalQty, entry, sl, target, setupTag);
            cout << GREEN << " >> Order successfully committed to trades_ledger.csv!\n" << RESET;
        } else {
            cout << RED << BOLD << "\n [BLOCKED] ORDER REJECTED BY PRE-TRADE RISK GATEKEEPER:" << RESET << "\n";
            for (const string& r : val.reasons) {
                cout << RED << "  - " << r << RESET << "\n";
            }
        }
    }

    void module3_OpenPositionsAndSquareOff() {
        cout << "\n" << BOLD << "--- MODULE 3: ACTIVE OPEN POSITIONS & LIVE MTM BOOK ---" << RESET << "\n";
        vector<Trade> openTrades = db.getTradesByStatus("OPEN");

        if (openTrades.empty()) {
            cout << YELLOW << " No open positions right now. Place a validated trade in Module 2!\n" << RESET;
            return;
        }

        const Instrument& activeInst = db.instruments[activeInstrumentIdx];
        Indicators ind = QuantEngine::computeIndicators(activeInst);

        cout << left << setw(6) << "ID" << setw(14) << "Symbol" << setw(7) << "Side"
             << setw(12) << "Qty(Lots)" << setw(12) << "Entry" << setw(12) << "SL"
             << setw(12) << "Target" << setw(15) << "Live MTM" << "\n";
        cout << "----------------------------------------------------------------------------------------\n";

        for (const auto& t : openTrades) {
            double currentLtp = (t.symbol == activeInst.symbol) ? ind.ltp : (t.entryPrice + 25.0);
            double mtm = (t.side == "BUY") ? ((currentLtp - t.entryPrice) * t.quantity) : ((t.entryPrice - currentLtp) * t.quantity);
            string qtyStr = to_string(t.quantity) + "(" + to_string(t.lots) + "L)";

            cout << left << setw(6) << t.tradeId << setw(14) << t.symbol << setw(7) << t.side
                 << setw(12) << qtyStr << fixed << setprecision(2)
                 << setw(12) << t.entryPrice << setw(12) << t.stopLoss << setw(12) << t.targetPrice;
            if (mtm >= 0) cout << GREEN << "+Rs." << mtm << RESET << "\n";
            else cout << RED << "-Rs." << fabs(mtm) << RESET << "\n";
        }

        cout << "\n Enter Trade ID to Square Off (or 0 to return to menu): ";
        int sqId;
        cin >> sqId;
        if (sqId > 0) {
            double exitPx;
            cout << " Enter Exit Price (Rs.): ";
            cin >> exitPx;
            Trade closedTrade;
            if (db.closeTrade(sqId, exitPx, closedTrade)) {
                cout << GREEN << BOLD << "\n [SQUARED OFF] Position #" << sqId << " Closed!" << RESET << "\n";
                cout << " Gross PnL: Rs." << closedTrade.grossPnl
                     << " | Statutory Taxes (STT+GST+Brokerage): -Rs." << closedTrade.taxesCharges
                     << " | Net Post-Tax PnL: Rs." << closedTrade.netPnl << "\n";
            } else {
                cout << RED << " Invalid Open Trade ID.\n" << RESET;
            }
        }
    }

    void module4_PostTradeAnalytics() {
        cout << "\n" << BOLD << "--- MODULE 4: POST-TRADE PERFORMANCE AUDIT & INDIAN STATUTORY TAX LEDGER ---" << RESET << "\n";
        vector<Trade> closedTrades = db.getTradesByStatus("CLOSED");

        if (closedTrades.empty()) {
            cout << YELLOW << " No closed trades in database yet.\n" << RESET;
            return;
        }

        double totalGross = 0, totalTaxes = 0, totalNet = 0;
        int wins = 0;

        cout << left << setw(5) << "ID" << setw(13) << "Symbol" << setw(6) << "Side"
             << setw(7) << "Qty" << setw(11) << "Entry" << setw(11) << "Exit"
             << setw(13) << "Gross PnL" << setw(11) << "Taxes" << setw(13) << "Net PnL" << "Setup\n";
        cout << "------------------------------------------------------------------------------------------------------------\n";

        for (const auto& t : closedTrades) {
            totalGross += t.grossPnl;
            totalTaxes += t.taxesCharges;
            totalNet += t.netPnl;
            if (t.netPnl > 0) wins++;

            cout << left << setw(5) << t.tradeId << setw(13) << t.symbol << setw(6) << t.side
                 << setw(7) << t.quantity << fixed << setprecision(2)
                 << setw(11) << t.entryPrice << setw(11) << t.exitPrice
                 << setw(13) << t.grossPnl << setw(11) << t.taxesCharges;
            if (t.netPnl >= 0) cout << GREEN << setw(13) << t.netPnl << RESET;
            else cout << RED << setw(13) << t.netPnl << RESET;
            cout << t.setupTag << "\n";
        }

        double winRate = (static_cast<double>(wins) / closedTrades.size()) * 100.0;
        cout << "============================================================================================================\n";
        cout << BOLD << " SUMMARY METRICS:" << RESET << "\n";
        cout << " Gross Realized PnL         : Rs." << fixed << setprecision(2) << totalGross << "\n";
        cout << " Total STT, GST & Brokerage : " << RED << "-Rs." << totalTaxes << RESET << "\n";
        cout << " Net Post-Tax Profit        : " << (totalNet >= 0 ? GREEN : RED) << BOLD << "Rs." << totalNet << RESET << "\n";
        cout << " Win Rate %                 : " << CYAN << fixed << setprecision(1) << winRate << "% (" << wins << "/" << closedTrades.size() << " Trades)" << RESET << "\n";
    }

    void module5_ChangeInstrument() {
        cout << "\n" << BOLD << "--- SELECT ACTIVE NSE F&O INSTRUMENT ---" << RESET << "\n";
        for (size_t i = 0; i < db.instruments.size(); ++i) {
            cout << "  " << (i + 1) << ". " << left << setw(26) << db.instruments[i].displayName
                 << " (Symbol: " << setw(12) << db.instruments[i].symbol
                 << " | Lot Size: " << db.instruments[i].lotSize << ")\n";
        }
        cout << " Enter Choice (1-" << db.instruments.size() << "): ";
        int c;
        cin >> c;
        if (c >= 1 && c <= static_cast<int>(db.instruments.size())) {
            activeInstrumentIdx = c - 1;
            cout << GREEN << " >> Switched active instrument to " << db.instruments[activeInstrumentIdx].displayName << "\n" << RESET;
        }
    }

    void run() {
        while (true) {
            printBanner();
            cout << "  1. Live Technical Scanner (20 EMA, Intraday VWAP & Floor Pivots)\n"
                 << "  2. Pre-Trade Risk Gatekeeper & Order Execution (2% Rule + 1:1.5 R:R)\n"
                 << "  3. Active Open Positions & Live MTM Square-Off Desk\n"
                 << "  4. Post-Trade Analytics & Indian Statutory Tax Ledger (STT, GST, SEBI)\n"
                 << "  5. Switch Active NSE Instrument (Nifty 50, Bank Nifty, Reliance, HDFC, etc.)\n"
                 << "  0. Exit Institutional Terminal\n"
                 << "----------------------------------------------------------------------------------------\n"
                 << BOLD << " Select Option (0-5): " << RESET;

            int choice;
            if (!(cin >> choice)) {
                cin.clear();
                cin.ignore(10000, '\n');
                continue;
            }

            if (choice == 0) {
                cout << GREEN << "\n Shutting down C++ QuantDesk Engine. All trades persisted to trades_ledger.csv.\n" << RESET;
                break;
            }
            switch (choice) {
                case 1: module1_LiveScanner(); break;
                case 2: module2_RiskGatekeeper(); break;
                case 3: module3_OpenPositionsAndSquareOff(); break;
                case 4: module4_PostTradeAnalytics(); break;
                case 5: module5_ChangeInstrument(); break;
                default: cout << RED << " Invalid option. Try again.\n" << RESET;
            }
        }
    }
};

int main() {
    QuantDeskTerminal terminal;
    terminal.run();
    return 0;
}