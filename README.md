# QuantDesk --- Stock & F&O Risk Management System

QuantDesk is an educational stock and Futures & Options (F&O) analysis
and trade-risk management project. It combines a market-data scanner,
pre-trade risk validation, an open-position/mark-to-market desk, and
post-trade performance reporting.

The repository includes a **Python + Streamlit application** and C++
implementations (console and Windows GUI). The Python application is the
primary dashboard described in the setup instructions below.

> **Important:** QuantDesk is an educational/demo project, not a
> brokerage platform or investment recommendation. It does not place
> orders with a broker. "Execute & Log" records a simulated/manual trade
> in the local database. Market data may be delayed, unavailable, or
> replaced with generated fallback data. Verify all calculations and
> statutory charges independently before relying on them.

## Features

### 1. Live Technical Scanner

-   Instrument selection for the configured index and equity symbols.
-   Displays last traded price (LTP), 20-period EMA, VWAP, central
    pivot, R1 and S1.
-   Produces a simple bullish, bearish, or sideways signal based on the
    relationship between LTP, EMA-20 and VWAP.
-   Displays candlesticks with EMA, VWAP and pivot support/resistance
    overlays.
-   Attempts to retrieve five-minute market history using `yfinance`.
-   Uses deterministic synthetic data if the live feed is unavailable or
    insufficient.

### 2. Pre-Trade Risk Gatekeeper

-   Accepts BUY/SELL direction, number of lots, entry, stop-loss and
    target prices.
-   Checks that stop-loss and target are on the correct side of the
    entry price.
-   Calculates position quantity from the instrument's configured lot
    size.
-   Calculates trade risk, potential reward, risk-to-reward ratio and
    percentage of capital at risk.
-   Blocks the trade if the risk-to-reward ratio is below 1:1.5 or the
    configured per-trade capital-risk limit is exceeded.
-   Shows a recommended maximum lot count based on the configured risk
    limit.
-   Logs approved trades to SQLite.

### 3. Live Order Book & MTM

-   Lists open positions stored in SQLite.
-   Estimates unrealized mark-to-market (MTM) P&L.
-   Allows the user to enter an exit price and square off a recorded
    position.
-   Calculates gross P&L, estimated charges and net P&L when a position
    is closed.

### 4. Post-Trade Analytics & Taxes

-   Summarizes closed trades.
-   Displays gross realized P&L, estimated taxes/charges, net P&L and
    win rate.
-   Plots a cumulative net realized P&L curve.
-   Displays a historical closed-trade journal.

## Technology Stack

  Technology   Purpose
  ------------ ----------------------------------------------------
  Python       Application logic and risk calculations
  Streamlit    Interactive web dashboard
  SQLite       Persistent account, instrument and trade records
  yfinance     Retrieval of market-history data
  pandas       Tabular data processing
  NumPy        Numerical operations and fallback data generation
  Plotly       Interactive candlestick and performance charts
  C++          Additional console and Windows GUI implementations

## Project Files

### Python / Streamlit application

``` text
QuantDesk/
├── app.py             # Streamlit user interface and application flow
├── db.py              # SQLite schema and database operations
├── engine.py          # Market indicators, risk validation and charges
└── portfolio.db       # SQLite database created/used by the application
```

### C++ implementations

``` text
QuantDesk/
├── quantdesk.cpp          # C++ console implementation
├── quantdesk.exe          # Compiled console executable (Windows)
├── quantdesk_gui.cpp      # C++ Windows GUI implementation
├── quantdesk_gui.exe      # Compiled GUI executable (Windows)
└── trades_ledger.csv      # CSV trade ledger used by the C++ implementation
```

The Python and C++ versions are separate implementations and use
different persistence approaches: the Python dashboard uses SQLite,
while the C++ console/GUI code uses a CSV trade ledger.

## Requirements

-   Python 3.9 or later recommended.
-   Windows, macOS or Linux for the Python/Streamlit application.
-   Internet connection for attempting live market-data retrieval.
-   A C++17-compatible compiler and Windows SDK/libraries for building
    the Windows C++ GUI version.

## Installation --- Python Application

1.  Clone or download the project.
2.  Open a terminal in the project directory.
3.  Create and activate a virtual environment (recommended).

### Windows PowerShell

``` powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
```

### macOS / Linux

``` bash
python3 -m venv .venv
source .venv/bin/activate
```

4.  Install the Python dependencies:

``` bash
python -m pip install streamlit pandas plotly yfinance numpy
```

5.  Start the application:

``` bash
streamlit run app.py
```

Streamlit will print a local address (usually `http://localhost:8501`)
in the terminal and normally open it in your browser.

## How to Use

1.  Start the Streamlit application.
2.  Review the account risk profile in the sidebar.
3.  Select an instrument from the sidebar.
4.  Open **Live Technical Scanner** to inspect price and indicator
    information.
5.  Open **Pre-Trade Risk Gatekeeper** and enter the proposed trade
    details.
6.  Review the validation result. A trade can be logged only if the
    configured risk checks pass.
7.  Open **Live Order Book & MTM** to review open positions and record
    an exit price.
8.  Open **Post-Trade Analytics & Taxes** to review closed-trade
    results.

The application initializes its database on startup. The database module
creates the required tables and inserts its initial
account/instrument/demo data when the relevant tables are empty.

## Risk Calculations

The risk engine uses the following core calculations:

-   **Total quantity** = number of lots × configured lot size
-   **Risk per unit** = absolute value of entry price − stop-loss price
-   **Total trade risk** = risk per unit × total quantity
-   **Potential reward** = absolute value of target price − entry price,
    multiplied by total quantity
-   **Risk-to-reward ratio** = reward per unit ÷ risk per unit
-   **Risk percentage** = total trade risk ÷ total capital × 100
-   **Maximum permitted risk amount** = total capital × configured risk
    percentage ÷ 100

The current Streamlit interface describes a 1:1.5 minimum risk-to-reward
rule and uses the account's configured per-trade risk percentage. The
displayed maximum daily loss is part of the account risk profile; do not
assume that it is an automatic daily-loss trading lock unless that
behavior is separately implemented.

## Market Data and Fallback Mode

The Python engine requests five-minute historical data for the selected
symbol through `yfinance`. If the request fails or the returned dataset
is insufficient, it generates synthetic sample candles and labels the
source as an offline fallback simulation.

Always check the **Data Feed Status** shown in the dashboard. Synthetic
fallback prices are for demonstration and testing only; they are not
live market prices.

## Database

The Python application stores information in `portfolio.db`. The
database module defines tables for: - `account_config` --- account
capital and risk settings. - `instruments` --- symbols, display names,
lot sizes and market segments. - `trades` --- trade direction, lots,
quantity, entry/stop/target/exit prices, status, P&L, charges, setup tag
and timestamp.

Back up `portfolio.db` before manually changing or deleting it. Removing
the database may cause the application to initialize a fresh database
and seed initial/demo data.

## C++ Versions

The project also contains C++ source files: - `quantdesk.cpp` ---
console-based implementation. - `quantdesk_gui.cpp` --- Windows GUI
implementation.

The C++ source uses C++17-era standard library features and calls
Python/yfinance to retrieve market data, with a generated fallback feed
when the live data request is unavailable. The GUI source also includes
Windows networking headers and links against Winsock.

The included `.exe` files are precompiled Windows binaries. Run only
binaries you trust and understand. To rebuild them, use the
compiler/toolchain configuration appropriate to the source and your
Windows environment; exact build commands depend on the GUI framework
and compiler setup used.

## Current Scope and Limitations

-   This is a project/demo risk-management tool, not a broker-connected
    trading terminal.
-   It does not submit real orders or connect to a broker account.
-   Market data is sourced through `yfinance` and is not guaranteed to
    be real-time, complete or suitable for execution.
-   Fallback market data is synthetic.
-   The charge calculation is an estimate implemented in the project;
    actual fees, taxes and exchange rules may differ and can change.
-   The displayed daily-loss limit should not be interpreted as an
    enforced circuit breaker unless a separate enforcement mechanism is
    added.
-   Risk checks are only as reliable as the inputs, instrument lot sizes
    and assumptions supplied to the application.

## Suggested Future Improvements

-   Add broker integration only with secure authentication and
    appropriate safeguards.
-   Add configurable account profiles and persistent settings.
-   Add a robust market-data provider interface and clearer feed
    freshness checks.
-   Add automated unit tests for indicator, risk and P&L calculations.
-   Add validation for invalid, missing or stale market prices.
-   Implement and test a true daily-loss circuit breaker if required.
-   Add exportable reports and improved audit logs.
-   Review statutory charge calculations against current official
    schedules.

## Team

The Streamlit application's sidebar credits the following project roles:

-   **Satvik** --- Backend & DBMS
-   **Akshit** --- Quant & F&O Risk Logic
-   **Siddhesh** --- System Design & Analytics

## Disclaimer

This software is provided for educational and demonstration purposes
only. It is not financial, investment, tax or legal advice. No
performance outcome is guaranteed. Do not use simulated data or
unverified calculations to make real trading decisions. The user is
responsible for independently validating market data, lot sizes, risk
assumptions, charges and any trading decision.
