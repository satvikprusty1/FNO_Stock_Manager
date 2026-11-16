import sqlite3
from datetime import datetime, timedelta

DB_NAME = "portfolio.db"

def get_connection():
    return sqlite3.connect(DB_NAME, check_same_thread=False)

def init_db():
    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS account_config (
            account_id INTEGER PRIMARY KEY,
            trader_name TEXT NOT NULL,
            total_capital REAL NOT NULL,
            max_risk_pct REAL NOT NULL,
            max_daily_loss REAL NOT NULL
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS instruments (
            symbol TEXT PRIMARY KEY,
            display_name TEXT NOT NULL,
            lot_size INTEGER NOT NULL,
            segment TEXT NOT NULL
        )
    """)

    cursor.execute("""
        CREATE TABLE IF NOT EXISTS trades (
            trade_id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            side TEXT NOT NULL,
            lots INTEGER NOT NULL,
            quantity INTEGER NOT NULL,
            entry_price REAL NOT NULL,
            stop_loss REAL NOT NULL,
            target_price REAL NOT NULL,
            exit_price REAL,
            status TEXT NOT NULL,
            gross_pnl REAL DEFAULT 0.0,
            taxes_charges REAL DEFAULT 0.0,
            net_pnl REAL DEFAULT 0.0,
            setup_tag TEXT NOT NULL,
            timestamp TEXT NOT NULL,
            FOREIGN KEY (symbol) REFERENCES instruments (symbol)
        )
    """)

    cursor.execute("SELECT COUNT(*) FROM account_config")
    if cursor.fetchone()[0] == 0:
        cursor.execute("""
            INSERT INTO account_config (account_id, trader_name, total_capital, max_risk_pct, max_daily_loss)
            VALUES (1, 'Akshit - Pro Desk', 500000.0, 2.0, 15000.0)
        """)

    cursor.execute("SELECT COUNT(*) FROM instruments")
    if cursor.fetchone()[0] == 0:
        instruments_data = [
            ("^NSEI", "NIFTY 50 INDEX (F&O)", 75, "FUTIDX"),
            ("^NSEBANK", "BANK NIFTY INDEX (F&O)", 30, "FUTIDX"),
            ("RELIANCE.NS", "RELIANCE INDUSTRIES", 250, "FUTSTK"),
            ("HDFCBANK.NS", "HDFC BANK LTD", 550, "FUTSTK"),
            ("TCS.NS", "TATA CONSULTANCY SERV", 175, "FUTSTK"),
            ("ICICIBANK.NS", "ICICI BANK LTD", 700, "FUTSTK")
        ]
        cursor.executemany("INSERT INTO instruments VALUES (?, ?, ?, ?)", instruments_data)

    cursor.execute("SELECT COUNT(*) FROM trades")
    if cursor.fetchone()[0] == 0:
        base_time = datetime.now() - timedelta(days=5)
        demo_trades = [
            ("^NSEI", "BUY", 2, 150, 24120.0, 24070.0, 24220.0, 24215.0, "CLOSED", 14250.0, 285.40, 13964.60, "VWAP + 20 EMA Bullish Bounce", (base_time + timedelta(days=1)).strftime("%Y-%m-%d 10:15")),
            ("^NSEBANK", "SELL", 2, 60, 51800.0, 51920.0, 51550.0, 51560.0, "CLOSED", 14400.0, 260.15, 14139.85, "R1 Pivot Rejection Short", (base_time + timedelta(days=2)).strftime("%Y-%m-%d 11:05")),
            ("RELIANCE.NS", "BUY", 1, 250, 1290.0, 1280.0, 1310.0, 1281.0, "CLOSED", -2250.0, 195.80, -2445.80, "VWAP Breakout Failure", (base_time + timedelta(days=3)).strftime("%Y-%m-%d 09:45")),
            ("^NSEI", "BUY", 2, 150, 24250.0, 24200.0, 24350.0, 24340.0, "CLOSED", 13500.0, 288.10, 13211.90, "S1 Support + 20 EMA Reclaim", (base_time + timedelta(days=4)).strftime("%Y-%m-%d 13:20")),
            ("HDFCBANK.NS", "BUY", 1, 550, 1720.0, 1708.0, 1745.0, 1742.0, "CLOSED", 12100.0, 310.50, 11789.50, "Institutional Volume Spike", (base_time + timedelta(days=4, hours=2)).strftime("%Y-%m-%d 14:50")),
            ("^NSEI", "BUY", 1, 75, 24310.0, 24260.0, 24410.0, None, "OPEN", 0.0, 0.0, 0.0, "VWAP + 20 EMA Bullish Bounce", datetime.now().strftime("%Y-%m-%d 09:30"))
        ]
        cursor.executemany("""
            INSERT INTO trades (
                symbol, side, lots, quantity, entry_price, stop_loss, target_price,
                exit_price, status, gross_pnl, taxes_charges, net_pnl, setup_tag, timestamp
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, demo_trades)

    conn.commit()
    conn.close()

def get_account_config():
    conn = get_connection()
    row = conn.execute("SELECT * FROM account_config WHERE account_id = 1").fetchone()
    conn.close()
    return {
        "account_id": row[0],
        "trader_name": row[1],
        "total_capital": row[2],
        "max_risk_pct": row[3],
        "max_daily_loss": row[4]
    }

def get_instruments():
    conn = get_connection()
    rows = conn.execute("SELECT symbol, display_name, lot_size, segment FROM instruments").fetchall()
    conn.close()
    return rows

def insert_trade(symbol, side, lots, quantity, entry_price, stop_loss, target_price, setup_tag):
    conn = get_connection()
    now_str = datetime.now().strftime("%Y-%m-%d %H:%M")
    conn.execute("""
        INSERT INTO trades (
            symbol, side, lots, quantity, entry_price, stop_loss, target_price,
            exit_price, status, gross_pnl, taxes_charges, net_pnl, setup_tag, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, NULL, 'OPEN', 0.0, 0.0, 0.0, ?, ?)
    """, (symbol, side, lots, quantity, entry_price, stop_loss, target_price, setup_tag, now_str))
    conn.commit()
    conn.close()

def close_trade_in_db(trade_id, exit_price, gross_pnl, taxes_charges, net_pnl):
    conn = get_connection()
    conn.execute("""
        UPDATE trades
        SET exit_price = ?, status = 'CLOSED', gross_pnl = ?, taxes_charges = ?, net_pnl = ?
        WHERE trade_id = ?
    """, (exit_price, round(gross_pnl, 2), round(taxes_charges, 2), round(net_pnl, 2), trade_id))
    conn.commit()
    conn.close()

def fetch_trades(status=None):
    conn = get_connection()
    if status:
        rows = conn.execute("SELECT * FROM trades WHERE status = ? ORDER BY trade_id DESC", (status,)).fetchall()
    else:
        rows = conn.execute("SELECT * FROM trades ORDER BY trade_id DESC").fetchall()
    conn.close()
    return rows
