#include <winsock2.h>
#include <ws2tcpip.h>
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
#include <map>

#pragma comment(lib, "ws2_32.lib")


struct Candle {
    std::string timestamp;
    double open, high, low, close, volume;
};

struct Instrument {
    std::string symbol;
    std::string displayName;
    int lotSize;
    std::string segment;
    double fallbackBasePrice;
};

struct Indicators {
    double ltp;
    double ema20;
    double vwap;
    double pivot;
    double r1;
    double s1;
    std::string signalText;
    std::string dataSource;
    std::vector<double> recentCloses;
    std::vector<double> recentEmas;
    std::vector<double> recentVwaps;
};

struct RiskValidationResult {
    bool approved;
    std::vector<std::string> reasons;
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
    std::string symbol;
    std::string side;
    int lots;
    int quantity;
    double entryPrice;
    double stopLoss;
    double targetPrice;
    double exitPrice;
    std::string status;
    double grossPnl;
    double taxesCharges;
    double netPnl;
    std::string setupTag;
    std::string timestamp;
};


std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += " ";
        else if (c == '\r') continue;
        else out += c;
    }
    return out;
}

std::string urlDecode(const std::string& str) {
    std::string ret;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int hexVal = 0;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> hexVal) {
                ret += static_cast<char>(hexVal);
                i += 2;
            } else {
                ret += str[i];
            }
        } else if (str[i] == '+') {
            ret += ' ';
        } else {
            ret += str[i];
        }
    }
    return ret;
}

std::map<std::string, std::string> parseQueryParams(const std::string& query) {
    std::map<std::string, std::string> params;
    std::stringstream ss(query);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = urlDecode(pair.substr(0, eq));
            std::string v = urlDecode(pair.substr(eq + 1));
            params[k] = v;
        }
    }
    return params;
}


class QuantEngine {
public:
    static std::vector<Candle> fetchCandles(const Instrument& inst, std::string& outSource) {
        std::vector<Candle> candles;
        std::string cmd = "python -c \"import yfinance as yf; df = yf.Ticker('" + inst.symbol +
                          "').history(period='5d', interval='5m'); "
                          "df.tail(75).to_csv('live_feed.csv')\" >nul 2>&1";

        int ret = system(cmd.c_str());
        std::ifstream file("live_feed.csv");

        if (ret == 0 && file.is_open()) {
            std::string line;
            std::getline(file, line);
            while (std::getline(file, line)) {
                if (line.empty()) continue;
                std::stringstream ss(line);
                std::string ts, o, h, l, c, v;
                std::getline(ss, ts, ',');
                std::getline(ss, o, ',');
                std::getline(ss, h, ',');
                std::getline(ss, l, ',');
                std::getline(ss, c, ',');
                std::getline(ss, v, ',');
                try {
                    Candle candle;
                    candle.timestamp = ts.substr(0, 16);
                    candle.open = std::stod(o);
                    candle.high = std::stod(h);
                    candle.low = std::stod(l);
                    candle.close = std::stod(c);
                    candle.volume = std::stod(v);
                    if (candle.volume <= 0) candle.volume = 1.0;
                    candles.push_back(candle);
                } catch (...) {}
            }
            file.close();
        }

        if (candles.size() >= 20) {
            outSource = "LIVE NSE FEED (C++17 Winsock <-> yfinance Bridge)";
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
            double highP = std::max(openP, closeP) + (inst.fallbackBasePrice * 0.0008);
            double lowP = std::min(openP, closeP) - (inst.fallbackBasePrice * 0.0008);
            double vol = 25000 + (rand() % 80000);
            candles.push_back({"2026-11-16 10:" + std::to_string(10 + (i % 49)), openP, highP, lowP, closeP, vol});
            price = closeP;
        }
        return candles;
    }

    static Indicators computeIndicators(const Instrument& inst) {
        std::string source;
        std::vector<Candle> candles = fetchCandles(inst, source);

        double k = 2.0 / (20.0 + 1.0);
        double ema20 = candles[0].close;

        // Anchor VWAP & Pivots to the most recent 35 intraday candles for crisp scaling
        size_t startSession = (candles.size() > 35) ? (candles.size() - 35) : 0;
        double cumTypPriceVol = 0.0;
        double cumVol = 0.0;
        double sessionHigh = candles[startSession].high;
        double sessionLow = candles[startSession].low;

        std::vector<double> closes, emas, vwaps;

        for (size_t i = 0; i < candles.size(); ++i) {
            const Candle& c = candles[i];
            ema20 = (c.close * k) + (ema20 * (1.0 - k));

            if (i >= startSession) {
                double typicalPrice = (c.high + c.low + c.close) / 3.0;
                cumTypPriceVol += (typicalPrice * c.volume);
                cumVol += c.volume;
                if (c.high > sessionHigh) sessionHigh = c.high;
                if (c.low < sessionLow) sessionLow = c.low;

                double currentVwap = (cumVol > 0) ? (cumTypPriceVol / cumVol) : c.close;
                closes.push_back(c.close);
                emas.push_back(ema20);
                vwaps.push_back(currentVwap);
            }
        }

        double ltp = candles.back().close;
        double vwap = vwaps.empty() ? ltp : vwaps.back();
        double pivot = (sessionHigh + sessionLow + ltp) / 3.0;
        double r1 = (2.0 * pivot) - sessionLow;
        double s1 = (2.0 * pivot) - sessionHigh;

        std::string sig;
        if (ltp > ema20 && ltp > vwap) {
            sig = "BULLISH (Price Above 20 EMA & VWAP)";
        } else if (ltp < ema20 && ltp < vwap) {
            sig = "BEARISH (Price Below 20 EMA & VWAP)";
        } else {
            sig = "SIDEWAYS / CHOPPY (Await Clear Breakout)";
        }

        return {ltp, ema20, vwap, pivot, r1, s1, sig, source, closes, emas, vwaps};
    }

    static RiskValidationResult validatePreTradeRisk(
        double capital, double maxRiskPct, const std::string& side,
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

        double riskPerUnit = std::fabs(entry - sl);
        double rewardPerUnit = std::fabs(target - entry);

        if (riskPerUnit < 0.0001 || requestedLots <= 0) {
            res.approved = false;
            res.reasons.push_back("Invalid Stop-Loss or Lot Quantity.");
            res.totalQty = 0;
            res.rrRatio = 0;
            res.totalTradeRiskRs = 0;
            res.potentialRewardRs = 0;
            res.maxAllowedRiskRs = capital * (maxRiskPct / 100.0);
            res.riskPctOfCapital = 0;
            res.recommendedMaxLots = 0;
            return res;
        }

        res.rrRatio = rewardPerUnit / riskPerUnit;
        res.totalQty = requestedLots * lotSize;
        res.totalTradeRiskRs = riskPerUnit * res.totalQty;
        res.potentialRewardRs = rewardPerUnit * res.totalQty;
        res.maxAllowedRiskRs = capital * (maxRiskPct / 100.0);
        res.riskPctOfCapital = (res.totalTradeRiskRs / capital) * 100.0;
        res.recommendedMaxLots = static_cast<int>(std::floor(res.maxAllowedRiskRs / (riskPerUnit * lotSize)));
        if (res.recommendedMaxLots < 0) res.recommendedMaxLots = 0;

        if (res.rrRatio < 1.5) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << "Unfavorable Risk-to-Reward (1:" << res.rrRatio << "). Minimum required is 1:1.50.";
            res.reasons.push_back(oss.str());
        }

        if (res.totalTradeRiskRs > res.maxAllowedRiskRs) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2)
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

// ==================== CLASS 2: PERSISTENT DATABASE MANAGER ====================
class DatabaseManager {
private:
    std::string dbFile = "trades_ledger.csv";
    std::vector<Trade> trades;

    std::string currentTimestamp() {
        time_t now = time(0);
        tm* ltm = localtime(&now);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", ltm);
        return std::string(buf);
    }

public:
    double totalCapital = 500000.0;
    double maxRiskPct = 2.0;
    double maxDailyLoss = 15000.0;
    std::vector<Instrument> instruments;

    DatabaseManager() {
        instruments = {
            {"^NSEI",       "NIFTY 50 INDEX (F&O)",   75,  "FUTIDX", 25040.0},
            {"^NSEBANK",    "BANK NIFTY INDEX (F&O)", 30,  "FUTIDX", 51600.0},
            {"RELIANCE.NS", "RELIANCE INDUSTRIES",    250, "FUTSTK", 1295.0},
            {"HDFCBANK.NS", "HDFC BANK LTD",          550, "FUTSTK", 1730.0},
            {"TCS.NS",      "TATA CONSULTANCY SERV",  175, "FUTSTK", 4120.0},
            {"ICICIBANK.NS","ICICI BANK LTD",         700, "FUTSTK", 1280.0}
        };
        loadFromDisk();
    }

    Instrument getInstrument(const std::string& sym) {
        for (const auto& inst : instruments) {
            if (inst.symbol == sym) return inst;
        }
        return instruments[0];
    }

    void loadFromDisk() {
        trades.clear();
        std::ifstream file(dbFile);
        if (!file.is_open()) {
            seedDemoTrades();
            saveToDisk();
            return;
        }
        std::string line;
        std::getline(file, line);
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            std::stringstream ss(line);
            std::string item;
            Trade t;
            try {
                std::getline(ss, item, ','); t.tradeId = std::stoi(item);
                std::getline(ss, t.symbol, ',');
                std::getline(ss, t.side, ',');
                std::getline(ss, item, ','); t.lots = std::stoi(item);
                std::getline(ss, item, ','); t.quantity = std::stoi(item);
                std::getline(ss, item, ','); t.entryPrice = std::stod(item);
                std::getline(ss, item, ','); t.stopLoss = std::stod(item);
                std::getline(ss, item, ','); t.targetPrice = std::stod(item);
                std::getline(ss, item, ','); t.exitPrice = std::stod(item);
                std::getline(ss, t.status, ',');
                std::getline(ss, item, ','); t.grossPnl = std::stod(item);
                std::getline(ss, item, ','); t.taxesCharges = std::stod(item);
                std::getline(ss, item, ','); t.netPnl = std::stod(item);
                std::getline(ss, t.setupTag, ',');
                std::getline(ss, t.timestamp, ',');
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
        std::ofstream file(dbFile);
        file << "trade_id,symbol,side,lots,quantity,entry_price,stop_loss,target_price,exit_price,status,gross_pnl,taxes_charges,net_pnl,setup_tag,timestamp\n";
        for (const auto& t : trades) {
            file << t.tradeId << "," << t.symbol << "," << t.side << ","
                 << t.lots << "," << t.quantity << ","
                 << std::fixed << std::setprecision(2) << t.entryPrice << ","
                 << t.stopLoss << "," << t.targetPrice << "," << t.exitPrice << ","
                 << t.status << "," << t.grossPnl << "," << t.taxesCharges << ","
                 << t.netPnl << "," << t.setupTag << "," << t.timestamp << "\n";
        }
        file.close();
    }

    void seedDemoTrades() {
        trades = {
            {1, "^NSEI", "BUY", 2, 150, 24820.0, 24770.0, 24920.0, 24915.0, "CLOSED", 14250.0, 285.40, 13964.60, "VWAP + 20 EMA Bullish Bounce", "2026-11-11 10:15"},
            {2, "^NSEBANK", "SELL", 2, 60, 51800.0, 51920.0, 51550.0, 51560.0, "CLOSED", 14400.0, 260.15, 14139.85, "R1 Pivot Rejection Short", "2026-11-12 11:05"},
            {3, "RELIANCE.NS", "BUY", 1, 250, 1290.0, 1280.0, 1310.0, 1281.0, "CLOSED", -2250.0, 195.80, -2445.80, "VWAP Breakout Failure", "2026-11-13 09:45"},
            {4, "^NSEI", "BUY", 2, 150, 24950.0, 24900.0, 25050.0, 25040.0, "CLOSED", 13500.0, 288.10, 13211.90, "S1 Support + 20 EMA Reclaim", "2026-11-14 13:20"},
            {5, "HDFCBANK.NS", "BUY", 1, 550, 1720.0, 1708.0, 1745.0, 1742.0, "CLOSED", 12100.0, 310.50, 11789.50, "Institutional Volume Spike", "2026-11-15 14:50"},
            {6, "^NSEI", "BUY", 1, 75, 25010.0, 24960.0, 25110.0, 0.0, "OPEN", 0.0, 0.0, 0.0, "VWAP + 20 EMA Bullish Bounce", currentTimestamp()}
        };
    }

    void insertOpenTrade(const std::string& sym, const std::string& side, int lots, int qty, double entry, double sl, double tg, const std::string& setup) {
        int nextId = trades.empty() ? 1 : (trades.back().tradeId + 1);
        trades.push_back({nextId, sym, side, lots, qty, entry, sl, tg, 0.0, "OPEN", 0.0, 0.0, 0.0, setup, currentTimestamp()});
        saveToDisk();
    }

    bool closeTrade(int id, double exitPx) {
        for (auto& t : trades) {
            if (t.tradeId == id && t.status == "OPEN") {
                t.exitPrice = exitPx;
                t.status = "CLOSED";
                t.grossPnl = (t.side == "BUY") ? ((exitPx - t.entryPrice) * t.quantity) : ((t.entryPrice - exitPx) * t.quantity);
                t.taxesCharges = QuantEngine::calculateIndianFnoCharges(t.entryPrice, exitPx, t.quantity);
                t.netPnl = t.grossPnl - t.taxesCharges;
                saveToDisk();
                return true;
            }
        }
        return false;
    }

    std::string buildStateJson(const std::string& sym) {
        Instrument inst = getInstrument(sym);
        Indicators ind = QuantEngine::computeIndicators(inst);

        std::ostringstream json;
        json << std::fixed << std::setprecision(2);
        json << "{";
        json << "\"capital\":" << totalCapital << ",";
        json << "\"maxRiskPct\":" << maxRiskPct << ",";
        json << "\"maxDailyLoss\":" << maxDailyLoss << ",";
        json << "\"symbol\":\"" << escapeJson(inst.symbol) << "\",";
        json << "\"displayName\":\"" << escapeJson(inst.displayName) << "\",";
        json << "\"lotSize\":" << inst.lotSize << ",";
        json << "\"ltp\":" << ind.ltp << ",";
        json << "\"ema20\":" << ind.ema20 << ",";
        json << "\"vwap\":" << ind.vwap << ",";
        json << "\"pivot\":" << ind.pivot << ",";
        json << "\"r1\":" << ind.r1 << ",";
        json << "\"s1\":" << ind.s1 << ",";
        json << "\"signal\":\"" << escapeJson(ind.signalText) << "\",";
        json << "\"source\":\"" << escapeJson(ind.dataSource) << "\",";

        json << "\"closes\":[";
        for (size_t i = 0; i < ind.recentCloses.size(); ++i) {
            json << ind.recentCloses[i] << (i + 1 < ind.recentCloses.size() ? "," : "");
        }
        json << "],\"emas\":[";
        for (size_t i = 0; i < ind.recentEmas.size(); ++i) {
            json << ind.recentEmas[i] << (i + 1 < ind.recentEmas.size() ? "," : "");
        }
        json << "],\"vwaps\":[";
        for (size_t i = 0; i < ind.recentVwaps.size(); ++i) {
            json << ind.recentVwaps[i] << (i + 1 < ind.recentVwaps.size() ? "," : "");
        }
        json << "],\"trades\":[";
        for (size_t i = 0; i < trades.size(); ++i) {
            const auto& t = trades[i];
            json << "{"
                 << "\"id\":" << t.tradeId << ","
                 << "\"symbol\":\"" << escapeJson(t.symbol) << "\","
                 << "\"side\":\"" << escapeJson(t.side) << "\","
                 << "\"lots\":" << t.lots << ","
                 << "\"qty\":" << t.quantity << ","
                 << "\"entry\":" << t.entryPrice << ","
                 << "\"sl\":" << t.stopLoss << ","
                 << "\"target\":" << t.targetPrice << ","
                 << "\"exit\":" << t.exitPrice << ","
                 << "\"status\":\"" << escapeJson(t.status) << "\","
                 << "\"grossPnl\":" << t.grossPnl << ","
                 << "\"taxes\":" << t.taxesCharges << ","
                 << "\"netPnl\":" << t.netPnl << ","
                 << "\"setup\":\"" << escapeJson(t.setupTag) << "\","
                 << "\"timestamp\":\"" << escapeJson(t.timestamp) << "\""
                 << "}" << (i + 1 < trades.size() ? "," : "");
        }
        json << "]}";
        return json.str();
    }
};

// ==================== EMBEDDED DARK-MODE WEB GUI ====================
const std::string INDEX_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>QuantDesk F&O | C++17 Risk & Stock Management System</title>
<style>
  :root { --bg:#0b0f19; --card:#131a2a; --border:#1f293d; --text:#e2e8f0; --muted:#94a3b8; --green:#10b981; --red:#ef4444; --cyan:#06b6d4; --amber:#f59e0b; }
  * { box-sizing:border-box; margin:0; padding:0; font-family:'Segoe UI',system-ui,sans-serif; }
  body { background:var(--bg); color:var(--text); display:flex; min-height:100vh; }
  .sidebar { width:280px; background:#090d16; border-right:1px solid var(--border); padding:24px 18px; display:flex; flex-direction:column; gap:16px; }
  .brand { font-size:22px; font-weight:800; color:var(--cyan); letter-spacing:0.5px; }
  .badge-cpp { display:inline-block; background:rgba(6,182,212,0.15); color:var(--cyan); border:1px solid rgba(6,182,212,0.4); font-size:11px; padding:3px 8px; border-radius:4px; margin-top:4px; }
  .side-card { background:var(--card); border:1px solid var(--border); border-radius:8px; padding:12px; }
  .side-label { font-size:11px; color:var(--muted); text-transform:uppercase; }
  .side-val { font-size:20px; font-weight:700; margin-top:4px; }
  select, input { width:100%; background:#0b0f19; border:1px solid var(--border); color:var(--text); padding:9px 12px; border-radius:6px; font-size:14px; margin-top:6px; }
  .main { flex:1; padding:26px 32px; overflow-y:auto; }
  .header { display:flex; justify-content:space-between; align-items:center; margin-bottom:20px; }
  .tabs { display:flex; gap:10px; border-bottom:1px solid var(--border); padding-bottom:12px; margin-bottom:22px; }
  .tab { background:var(--card); border:1px solid var(--border); color:var(--muted); padding:10px 18px; border-radius:6px; cursor:pointer; font-weight:600; font-size:14px; }
  .tab.active { background:var(--cyan); color:#05101a; border-color:var(--cyan); }
  .grid-5 { display:grid; grid-template-columns:repeat(5,1fr); gap:14px; margin-bottom:18px; }
  .grid-4 { display:grid; grid-template-columns:repeat(4,1fr); gap:14px; margin-bottom:18px; }
  .grid-2 { display:grid; grid-template-columns:1fr 1fr; gap:20px; }
  .card { background:var(--card); border:1px solid var(--border); border-radius:10px; padding:16px; }
  .metric-title { font-size:12px; color:var(--muted); }
  .metric-num { font-size:21px; font-weight:700; margin-top:6px; }
  .signal-bar { padding:14px 18px; border-radius:8px; font-weight:700; margin-bottom:18px; border:1px solid; }
  .sig-bull { background:rgba(16,185,129,0.12); border-color:var(--green); color:var(--green); }
  .sig-bear { background:rgba(239,68,68,0.12); border-color:var(--red); color:var(--red); }
  .sig-chop { background:rgba(245,158,11,0.12); border-color:var(--amber); color:var(--amber); }
  canvas { width:100%; height:290px; background:#090d16; border-radius:8px; border:1px solid var(--border); }
  .btn { padding:11px 18px; border:none; border-radius:6px; font-weight:700; cursor:pointer; font-size:14px; width:100%; margin-top:14px; }
  .btn-primary { background:var(--green); color:#04150e; }
  .btn-danger { background:var(--red); color:#fff; width:auto; padding:6px 12px; margin-top:0; }
  .btn-refresh { background:var(--card); border:1px solid var(--cyan); color:var(--cyan); width:auto; margin-top:0; padding:8px 14px; }
  table { width:100%; border-collapse:collapse; margin-top:12px; font-size:13px; }
  th, td { padding:11px 12px; text-align:left; border-bottom:1px solid var(--border); }
  th { color:var(--muted); font-weight:600; background:#0e1422; }
  .hidden { display:none; }
  .alert-box { padding:14px; border-radius:8px; margin-top:14px; font-size:14px; line-height:1.5; }
  .alert-ok { background:rgba(16,185,129,0.15); border:1px solid var(--green); color:#a7f3d0; }
  .alert-err { background:rgba(239,68,68,0.15); border:1px solid var(--red); color:#fecaca; }
</style>
</head>
<body>
<div class="sidebar">
  <div>
    <div class="brand">QuantDesk F&O</div>
    <div class="badge-cpp">C++17 Winsock Engine</div>
  </div>
  <div class="side-card">
    <div class="side-label">Trading Capital</div>
    <div class="side-val" style="color:var(--green)" id="sbCap">Rs.5,00,000</div>
  </div>
  <div class="side-card">
    <div class="side-label">Max Risk Per Trade</div>
    <div class="side-val" style="color:var(--amber)" id="sbRisk">2.0% (Rs.10,000)</div>
  </div>
  <div class="side-card">
    <div class="side-label">Active NSE Instrument</div>
    <select id="instSelect" onchange="loadState()">
      <option value="^NSEI">NIFTY 50 INDEX (Lot: 75)</option>
      <option value="^NSEBANK">BANK NIFTY INDEX (Lot: 30)</option>
      <option value="RELIANCE.NS">RELIANCE IND (Lot: 250)</option>
      <option value="HDFCBANK.NS">HDFC BANK LTD (Lot: 550)</option>
      <option value="TCS.NS">TCS LTD (Lot: 175)</option>
      <option value="ICICIBANK.NS">ICICI BANK LTD (Lot: 700)</option>
    </select>
  </div>
  <div class="side-card" style="font-size:12px;color:var(--muted);line-height:1.6;">
    <strong style="color:var(--text)">Project Team:</strong><br>
    • <b>Satvik:</b> C++ Backend & HTTP Server<br>
    • <b>Akshit:</b> F&O Quant & Risk Math<br>
    • <b>Siddhesh:</b> System Design & UI
  </div>
</div>

<div class="main">
  <div class="header">
    <div>
      <h1 style="font-size:22px;">Institutional Stock & F&O Risk Management System</h1>
      <div style="font-size:13px;color:var(--muted);margin-top:4px;" id="feedStatus">Connecting to C++ Engine...</div>
    </div>
    <button class="btn btn-refresh" onclick="loadState()">↻ Refresh C++ Engine</button>
  </div>

  <div class="tabs">
    <button class="tab active" onclick="switchTab(0)">1. Live Technical Scanner</button>
    <button class="tab" onclick="switchTab(1)">2. Pre-Trade Risk Gatekeeper</button>
    <button class="tab" onclick="switchTab(2)">3. Open Positions & MTM</button>
    <button class="tab" onclick="switchTab(3)">4. Post-Trade Analytics & Taxes</button>
  </div>

  <!-- TAB 1 -->
  <div id="tab0">
    <div class="grid-5">
      <div class="card"><div class="metric-title">Last Traded Price (LTP)</div><div class="metric-num" id="mLtp">-</div></div>
      <div class="card"><div class="metric-title">20 EMA (Intraday)</div><div class="metric-num" style="color:var(--cyan)" id="mEma">-</div></div>
      <div class="card"><div class="metric-title">Intraday VWAP</div><div class="metric-num" style="color:var(--amber)" id="mVwap">-</div></div>
      <div class="card"><div class="metric-title">Central Pivot (P)</div><div class="metric-num" id="mPivot">-</div></div>
      <div class="card"><div class="metric-title">Resistance R1 / Support S1</div><div class="metric-num" style="font-size:17px;" id="mR1S1">-</div></div>
    </div>
    <div id="signalBanner" class="signal-bar sig-bull">Loading Signal...</div>
    <div class="card">
      <div style="display:flex;justify-content:space-between;margin-bottom:10px;font-size:13px;">
        <strong>Intraday Price Action vs 20 EMA & VWAP (Rendered from C++ Vector Stream)</strong>
        <span><span style="color:#10b981">● Price</span> &nbsp; <span style="color:#06b6d4">● 20 EMA</span> &nbsp; <span style="color:#f59e0b">● VWAP</span></span>
      </div>
      <canvas id="priceChart" width="900" height="290"></canvas>
    </div>
  </div>

  <!-- TAB 2 -->
  <div id="tab1" class="hidden">
    <div class="grid-2">
      <div class="card">
        <h3 style="margin-bottom:12px;">Order Parameters (Enforces 2% Risk & 1:1.5 R:R)</h3>
        <label class="side-label">Direction</label>
        <select id="ordSide" onchange="autoFillLevels()">
          <option value="BUY">BUY (Long)</option>
          <option value="SELL">SELL (Short)</option>
        </select>
        <label class="side-label" style="display:block;margin-top:10px;">Number of Lots (Try 25 to test Gatekeeper block!)</label>
        <input type="number" id="ordLots" value="1" min="1" max="100">
        <label class="side-label" style="display:block;margin-top:10px;">Entry Price (Rs.)</label>
        <input type="number" id="ordEntry" step="0.5">
        <label class="side-label" style="display:block;margin-top:10px;">Stop-Loss Price (Rs.)</label>
        <input type="number" id="ordSl" step="0.5">
        <label class="side-label" style="display:block;margin-top:10px;">Target Price (Rs.)</label>
        <input type="number" id="ordTarget" step="0.5">
        <label class="side-label" style="display:block;margin-top:10px;">Technical Setup Tag</label>
        <select id="ordSetup">
          <option>VWAP + 20 EMA Bullish Bounce</option>
          <option>R1/S1 Pivot Breakout</option>
          <option>Opening Range Breakout (ORB)</option>
          <option>Institutional Volume Spike</option>
        </select>
        <button class="btn btn-primary" onclick="submitOrder()">Validate & Execute via C++ Gatekeeper</button>
      </div>
      <div class="card">
        <h3 style="margin-bottom:12px;">C++ Pre-Trade Risk Audit Output</h3>
        <p style="font-size:13px;color:var(--muted);">Every order passes through <code>QuantEngine::validatePreTradeRisk()</code> in C++ before writing to <code>trades_ledger.csv</code>.</p>
        <div id="gatekeeperResult" class="alert-box alert-ok" style="margin-top:18px;">
          Ready to audit order. Enter lots, Stop-Loss, and Target on the left and click <b>Validate & Execute</b>.
        </div>
      </div>
    </div>
  </div>

  <!-- TAB 3 -->
  <div id="tab2" class="hidden">
    <div class="card">
      <h3>Active Open Positions & Mark-to-Market (MTM) Desk</h3>
      <table>
        <thead>
          <tr><th>ID</th><th>Symbol</th><th>Side</th><th>Qty (Lots)</th><th>Entry</th><th>Stop-Loss</th><th>Target</th><th>Live MTM</th><th>Exit Price & Action</th></tr>
        </thead>
        <tbody id="openTradesBody"></tbody>
      </table>
    </div>
  </div>

  <!-- TAB 4 -->
  <div id="tab3" class="hidden">
    <div class="grid-4">
      <div class="card"><div class="metric-title">Gross Realized PnL</div><div class="metric-num" id="aGross">-</div></div>
      <div class="card"><div class="metric-title">Total STT, GST & Brokerage</div><div class="metric-num" style="color:var(--red)" id="aTaxes">-</div></div>
      <div class="card"><div class="metric-title">Net Post-Tax Profit</div><div class="metric-num" style="color:var(--green)" id="aNet">-</div></div>
      <div class="card"><div class="metric-title">Win Rate %</div><div class="metric-num" style="color:var(--cyan)" id="aWin">-</div></div>
    </div>
    <div class="card">
      <h3>Closed Trade Ledger (Persisted in <code>trades_ledger.csv</code>)</h3>
      <table>
        <thead>
          <tr><th>ID</th><th>Time</th><th>Symbol</th><th>Side</th><th>Qty</th><th>Entry</th><th>Exit</th><th>Gross PnL</th><th>Taxes</th><th>Net PnL</th><th>Setup Tag</th></tr>
        </thead>
        <tbody id="closedTradesBody"></tbody>
      </table>
    </div>
  </div>
</div>

<script>
let currentState = null;

function switchTab(idx) {
  document.querySelectorAll('.tab').forEach((t, i) => t.classList.toggle('active', i === idx));
  for (let i = 0; i < 4; i++) document.getElementById('tab' + i).classList.toggle('hidden', i !== idx);
}

async function loadState() {
  const sym = document.getElementById('instSelect').value;
  const res = await fetch('/api/state?symbol=' + encodeURIComponent(sym));
  currentState = await res.json();

  document.getElementById('feedStatus').innerHTML = `Feed Status: <strong style="color:var(--cyan)">${currentState.source}</strong> | Active: <strong>${currentState.displayName}</strong>`;
  document.getElementById('mLtp').innerText = 'Rs.' + currentState.ltp.toLocaleString('en-IN');
  document.getElementById('mEma').innerText = 'Rs.' + currentState.ema20.toLocaleString('en-IN');
  document.getElementById('mVwap').innerText = 'Rs.' + currentState.vwap.toLocaleString('en-IN');
  document.getElementById('mPivot').innerText = 'Rs.' + currentState.pivot.toLocaleString('en-IN');
  document.getElementById('mR1S1').innerText = `${currentState.r1.toFixed(0)} / ${currentState.s1.toFixed(0)}`;

  const sb = document.getElementById('signalBanner');
  sb.innerText = 'QUANT ENGINE SIGNAL: ' + currentState.signal;
  sb.className = 'signal-bar ' + (currentState.signal.includes('BULLISH') ? 'sig-bull' : currentState.signal.includes('BEARISH') ? 'sig-bear' : 'sig-chop');

  autoFillLevels();
  drawChart(currentState.closes, currentState.emas, currentState.vwaps);
  renderTables();
}

function autoFillLevels() {
  if (!currentState) return;
  const side = document.getElementById('ordSide').value;
  const ltp = currentState.ltp;
  document.getElementById('ordEntry').value = ltp.toFixed(1);
  document.getElementById('ordSl').value = (side === 'BUY' ? ltp * 0.996 : ltp * 1.004).toFixed(1);
  document.getElementById('ordTarget').value = (side === 'BUY' ? ltp * 1.008 : ltp * 0.992).toFixed(1);
}

function drawChart(closes, emas, vwaps) {
  const canvas = document.getElementById('priceChart');
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (!closes || closes.length < 2) return;

  const all = [...closes, ...emas, ...vwaps];
  const minV = Math.min(...all) * 0.9995;
  const maxV = Math.max(...all) * 1.0005;
  const pad = 30;
  const w = canvas.width - pad * 2;
  const h = canvas.height - pad * 2;

  const plotLine = (arr, color, dash=[]) => {
    ctx.beginPath();
    ctx.setLineDash(dash);
    ctx.strokeStyle = color;
    ctx.lineWidth = 2.5;
    arr.forEach((v, i) => {
      const x = pad + (i / (arr.length - 1)) * w;
      const y = pad + h - ((v - minV) / (maxV - minV || 1)) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  };

  plotLine(vwaps, '#f59e0b', [6, 4]);
  plotLine(emas, '#06b6d4', []);
  plotLine(closes, '#10b981', []);
}

async function submitOrder() {
  const params = new URLSearchParams({
    symbol: document.getElementById('instSelect').value,
    side: document.getElementById('ordSide').value,
    lots: document.getElementById('ordLots').value,
    entry: document.getElementById('ordEntry').value,
    sl: document.getElementById('ordSl').value,
    target: document.getElementById('ordTarget').value,
    setup: document.getElementById('ordSetup').value
  });
  const res = await fetch('/api/order?' + params.toString());
  const data = await res.json();
  const box = document.getElementById('gatekeeperResult');

  if (data.approved) {
    box.className = 'alert-box alert-ok';
    box.innerHTML = `<strong>✅ RISK AUDIT PASSED & ORDER COMMITTED!</strong><br>
      Quantity: <b>${data.totalQty} Qty</b> | Risk-to-Reward: <b>1:${data.rrRatio}</b><br>
      Capital at Risk: <b>Rs.${data.riskRs} (${data.riskPct}%)</b> | Potential Reward: <b>Rs.${data.rewardRs}</b>`;
    loadState();
  } else {
    box.className = 'alert-box alert-err';
    box.innerHTML = `<strong>🚫 ORDER BLOCKED BY C++ RISK GATEKEEPER:</strong><br>• ` + data.reasons.join('<br>• ');
  }
}

async function squareOff(id) {
  const exitPx = document.getElementById('exit_' + id).value;
  await fetch(`/api/close?id=${id}&exit=${exitPx}`);
  loadState();
}

function renderTables() {
  const openBody = document.getElementById('openTradesBody');
  const closedBody = document.getElementById('closedTradesBody');
  openBody.innerHTML = '';
  closedBody.innerHTML = '';

  let grossSum = 0, taxSum = 0, netSum = 0, wins = 0, closedCount = 0;

  currentState.trades.forEach(t => {
    if (t.status === 'OPEN') {
      const livePx = (t.symbol === currentState.symbol) ? currentState.ltp : (t.entry + 15);
      const mtm = (t.side === 'BUY') ? (livePx - t.entry) * t.qty : (t.entry - livePx) * t.qty;
      const mtmColor = mtm >= 0 ? 'var(--green)' : 'var(--red)';
      openBody.innerHTML += `<tr>
        <td>#${t.id}</td><td><b>${t.symbol}</b></td><td>${t.side}</td><td>${t.qty} (${t.lots}L)</td>
        <td>Rs.${t.entry}</td><td>Rs.${t.sl}</td><td>Rs.${t.target}</td>
        <td style="color:${mtmColor};font-weight:700;">Rs.${mtm.toFixed(2)}</td>
        <td style="display:flex;gap:8px;align-items:center;">
          <input type="number" id="exit_${t.id}" value="${livePx.toFixed(1)}" style="width:100px;margin:0;padding:6px;">
          <button class="btn btn-danger" onclick="squareOff(${t.id})">Square Off</button>
        </td>
      </tr>`;
    } else {
      closedCount++;
      grossSum += t.grossPnl;
      taxSum += t.taxes;
      netSum += t.netPnl;
      if (t.netPnl > 0) wins++;
      const netColor = t.netPnl >= 0 ? 'var(--green)' : 'var(--red)';
      closedBody.innerHTML += `<tr>
        <td>#${t.id}</td><td>${t.timestamp}</td><td><b>${t.symbol}</b></td><td>${t.side}</td><td>${t.qty}</td>
        <td>Rs.${t.entry}</td><td>Rs.${t.exit}</td><td>Rs.${t.grossPnl.toFixed(2)}</td>
        <td style="color:var(--red)">-Rs.${t.taxes.toFixed(2)}</td>
        <td style="color:${netColor};font-weight:700;">Rs.${t.netPnl.toFixed(2)}</td><td>${t.setup}</td>
      </tr>`;
    }
  });

  document.getElementById('aGross').innerText = 'Rs.' + grossSum.toFixed(2);
  document.getElementById('aTaxes').innerText = '-Rs.' + taxSum.toFixed(2);
  document.getElementById('aNet').innerText = 'Rs.' + netSum.toFixed(2);
  document.getElementById('aWin').innerText = closedCount ? ((wins / closedCount) * 100).toFixed(1) + '%' : '0%';
}

loadState();
</script>
</body>
</html>)HTML";

// ==================== HTTP SERVER CONTROLLER ====================
int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Port 8080 bind failed. Close any previous instance and try again.\n";
        return 1;
    }

    listen(serverSocket, 10);
    DatabaseManager db;

    std::cout << "====================================================================\n";
    std::cout << "  QUANTDESK C++17 HTTP SERVER RUNNING AT: http://localhost:8080\n";
    std::cout << "  Open http://localhost:8080 in Chrome/Edge to use the Interface!\n";
    std::cout << "====================================================================\n";

    while (true) {
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET) continue;

        char buffer[4096] = {0};
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0) {
            closesocket(clientSocket);
            continue;
        }

        std::string request(buffer, bytesRead);
        std::istringstream reqStream(request);
        std::string method, fullPath, version;
        reqStream >> method >> fullPath >> version;

        std::string path = fullPath;
        std::string query = "";
        size_t qPos = fullPath.find('?');
        if (qPos != std::string::npos) {
            path = fullPath.substr(0, qPos);
            query = fullPath.substr(qPos + 1);
        }

        std::string contentType = "text/html; charset=UTF-8";
        std::string body = "";

        if (path == "/") {
            body = INDEX_HTML;
        } else if (path == "/api/state") {
            contentType = "application/json";
            auto params = parseQueryParams(query);
            std::string sym = params.count("symbol") ? params["symbol"] : "^NSEI";
            body = db.buildStateJson(sym);
        } else if (path == "/api/order") {
            contentType = "application/json";
            auto p = parseQueryParams(query);
            std::string sym = p["symbol"];
            std::string side = p["side"];
            int lots = std::stoi(p["lots"]);
            double entry = std::stod(p["entry"]);
            double sl = std::stod(p["sl"]);
            double target = std::stod(p["target"]);
            std::string setup = p["setup"];

            Instrument inst = db.getInstrument(sym);
            RiskValidationResult val = QuantEngine::validatePreTradeRisk(
                db.totalCapital, db.maxRiskPct, side, entry, sl, target, inst.lotSize, lots
            );

            std::ostringstream json;
            json << std::fixed << std::setprecision(2);
            json << "{\"approved\":" << (val.approved ? "true" : "false") << ","
                 << "\"totalQty\":" << val.totalQty << ","
                 << "\"rrRatio\":" << val.rrRatio << ","
                 << "\"riskRs\":" << val.totalTradeRiskRs << ","
                 << "\"riskPct\":" << val.riskPctOfCapital << ","
                 << "\"rewardRs\":" << val.potentialRewardRs << ","
                 << "\"reasons\":[";
            for (size_t i = 0; i < val.reasons.size(); ++i) {
                json << "\"" << escapeJson(val.reasons[i]) << "\"" << (i + 1 < val.reasons.size() ? "," : "");
            }
            json << "]}";

            if (val.approved) {
                db.insertOpenTrade(sym, side, lots, val.totalQty, entry, sl, target, setup);
            }
            body = json.str();
        } else if (path == "/api/close") {
            contentType = "application/json";
            auto p = parseQueryParams(query);
            int id = std::stoi(p["id"]);
            double exitPx = std::stod(p["exit"]);
            bool ok = db.closeTrade(id, exitPx);
            body = ok ? "{\"closed\":true}" : "{\"closed\":false}";
        } else {
            body = "404 Not Found";
        }

        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;

        std::string respStr = response.str();
        send(clientSocket, respStr.c_str(), static_cast<int>(respStr.size()), 0);
        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}