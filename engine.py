import yfinance as yf
import pandas as pd
import numpy as np

def fetch_market_data_and_indicators(symbol: str):
    try:
        ticker = yf.Ticker(symbol)
        df = ticker.history(period="5d", interval="5m")
        if df.empty or len(df) < 25:
            raise ValueError("Empty feed")
        df = df.reset_index()
        time_col = "Datetime" if "Datetime" in df.columns else "Date"
        df.rename(columns={time_col: "Timestamp"}, inplace=True)
        data_source = "LIVE NSE FEED (yfinance)"
    except Exception:
        np.random.seed(42)
        base_map = {"^NSEI": 24300.0, "^NSEBANK": 51600.0, "RELIANCE.NS": 1295.0, "HDFCBANK.NS": 1730.0}
        base_price = base_map.get(symbol, 1500.0)
        timestamps = pd.date_range(end=pd.Timestamp.now(), periods=75, freq="5min")
        walk = np.cumsum(np.random.normal(0.5, base_price * 0.0012, size=75))
        close = base_price + walk
        open_p = close - np.random.normal(0, base_price * 0.0008, size=75)
        high = np.maximum(open_p, close) + np.abs(np.random.normal(0, base_price * 0.0009, size=75))
        low = np.minimum(open_p, close) - np.abs(np.random.normal(0, base_price * 0.0009, size=75))
        volume = np.random.randint(15000, 120000, size=75)
        df = pd.DataFrame({
            "Timestamp": timestamps,
            "Open": open_p, "High": high, "Low": low, "Close": close, "Volume": volume
        })
        data_source = "OFFLINE FALLBACK SIMULATION (Wi-Fi Safe Mode)"

    df["EMA_20"] = df["Close"].ewm(span=20, adjust=False).mean()
    typical_price = (df["High"] + df["Low"] + df["Close"]) / 3.0
    vol_safe = df["Volume"].replace(0, 1)
    df["VWAP"] = (typical_price * vol_safe).cumsum() / vol_safe.cumsum()

    session_high = float(df["High"].max())
    session_low = float(df["Low"].min())
    session_close = float(df["Close"].iloc[-1])

    pivot = (session_high + session_low + session_close) / 3.0
    r1 = (2.0 * pivot) - session_low
    s1 = (2.0 * pivot) - session_high

    latest = df.iloc[-1]
    ltp = float(latest["Close"])
    ema20 = float(latest["EMA_20"])
    vwap = float(latest["VWAP"])

    if ltp > ema20 and ltp > vwap:
        signal = "BULLISH (Price Above 20 EMA & VWAP)"
    elif ltp < ema20 and ltp < vwap:
        signal = "BEARISH (Price Below 20 EMA & VWAP)"
    else:
        signal = "SIDEWAYS / CHOPPY (Await Clear Breakout)"

    return {
        "df": df.tail(60),
        "ltp": round(ltp, 2),
        "ema20": round(ema20, 2),
        "vwap": round(vwap, 2),
        "pivot": round(pivot, 2),
        "r1": round(r1, 2),
        "s1": round(s1, 2),
        "signal": signal,
        "source": data_source
    }

def validate_pre_trade_risk(capital: float, max_risk_pct: float, side: str, entry: float, sl: float, target: float, lot_size: int, requested_lots: int):
    reasons = []
    if side == "BUY":
        if sl >= entry:
            reasons.append("For a BUY order, Stop-Loss must be strictly BELOW Entry Price.")
        if target <= entry:
            reasons.append("For a BUY order, Target Price must be strictly ABOVE Entry Price.")
    else:
        if sl <= entry:
            reasons.append("For a SELL order, Stop-Loss must be strictly ABOVE Entry Price.")
        if target >= entry:
            reasons.append("For a SELL order, Target Price must be strictly BELOW Entry Price.")

    risk_per_unit = abs(entry - sl)
    reward_per_unit = abs(target - entry)

    if risk_per_unit == 0:
        return {"approved": False, "reasons": ["Stop-Loss cannot equal Entry Price."], "total_qty": 0}

    rr_ratio = round(reward_per_unit / risk_per_unit, 2)
    total_qty = requested_lots * lot_size
    total_trade_risk_rs = round(risk_per_unit * total_qty, 2)
    potential_reward_rs = round(reward_per_unit * total_qty, 2)
    max_allowed_risk_rs = round(capital * (max_risk_pct / 100.0), 2)
    risk_pct_of_capital = round((total_trade_risk_rs / capital) * 100.0, 2)
    recommended_max_lots = int(max_allowed_risk_rs // (risk_per_unit * lot_size))

    if rr_ratio < 1.5:
        reasons.append(f"Unfavorable Risk-to-Reward Ratio (1:{rr_ratio}). Minimum required is 1:1.5.")

    if total_trade_risk_rs > max_allowed_risk_rs:
        reasons.append(
            f"Risk Limit Breached! Trade risks Rs.{total_trade_risk_rs:,.2f} ({risk_pct_of_capital}% of capital), "
            f"exceeding the {max_risk_pct}% limit (Rs.{max_allowed_risk_rs:,.2f}). Max safe lots: {max(0, recommended_max_lots)}."
        )

    return {
        "approved": len(reasons) == 0,
        "reasons": reasons,
        "rr_ratio": rr_ratio,
        "total_qty": total_qty,
        "total_trade_risk_rs": total_trade_risk_rs,
        "potential_reward_rs": potential_reward_rs,
        "max_allowed_risk_rs": max_allowed_risk_rs,
        "risk_pct_of_capital": risk_pct_of_capital,
        "recommended_max_lots": max(0, recommended_max_lots)
    }

def calculate_indian_fno_charges(entry_price: float, exit_price: float, quantity: int):
    buy_turnover = entry_price * quantity
    sell_turnover = exit_price * quantity
    total_turnover = buy_turnover + sell_turnover

    brokerage = 40.0
    stt = sell_turnover * 0.0002
    exchange_txn = total_turnover * 0.0000188
    sebi_charges = total_turnover * 0.000001
    stamp_duty = buy_turnover * 0.00002
    gst = 0.18 * (brokerage + exchange_txn + sebi_charges)

    total_charges = brokerage + stt + exchange_txn + sebi_charges + stamp_duty + gst
    return round(total_charges, 2)
