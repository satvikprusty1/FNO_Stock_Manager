import streamlit as st
import pandas as pd
import plotly.graph_objects as go
import db
import engine

st.set_page_config(page_title="F&O Risk & Stock Management System", layout="wide")

db.init_db()
account = db.get_account_config()
instruments = db.get_instruments()
inst_dict = {row[0]: {"name": row[1], "lot_size": row[2], "segment": row[3]} for row in instruments}

st.sidebar.title("QuantDesk F&O")
st.sidebar.caption("Algorithmic Stock & Risk Management System")
st.sidebar.markdown("---")
st.sidebar.subheader("Account Risk Profile")
st.sidebar.metric("Total Trading Capital", f"Rs.{account['total_capital']:,.0f}")
st.sidebar.metric("Max Risk Per Trade Rule", f"{account['max_risk_pct']}% (Rs.{account['total_capital'] * account['max_risk_pct'] / 100:,.0f})")
st.sidebar.metric("Max Daily Loss Circuit", f"Rs.{account['max_daily_loss']:,.0f}")

selected_symbol = st.sidebar.selectbox(
    "Select Active Instrument",
    options=list(inst_dict.keys()),
    format_func=lambda x: f"{inst_dict[x]['name']} (Lot: {inst_dict[x]['lot_size']})"
)

st.sidebar.markdown("---")
st.sidebar.info("**Project Team:**\n- **Satvik:** Backend & DBMS\n- **Akshit:** Quant & F&O Risk Logic\n- **Siddhesh:** System Design & Analytics")

market = engine.fetch_market_data_and_indicators(selected_symbol)
active_lot_size = inst_dict[selected_symbol]["lot_size"]

st.title("Institutional Stock & F&O Risk Management System")
st.caption(f"Data Feed Status: **{market['source']}** | Instrument: **{inst_dict[selected_symbol]['name']}**")

tab1, tab2, tab3, tab4 = st.tabs([
    "1. Live Technical Scanner",
    "2. Pre-Trade Risk Gatekeeper",
    "3. Live Order Book & MTM",
    "4. Post-Trade Analytics & Taxes"
])

with tab1:
    c1, c2, c3, c4, c5 = st.columns(5)
    c1.metric("Last Traded Price (LTP)", f"Rs.{market['ltp']:,.2f}")
    c2.metric("20 EMA (Intraday)", f"Rs.{market['ema20']:,.2f}")
    c3.metric("Intraday VWAP", f"Rs.{market['vwap']:,.2f}")
    c4.metric("Central Pivot (P)", f"Rs.{market['pivot']:,.2f}")
    c5.metric("R1 / S1 Levels", f"{market['r1']:,.0f} / {market['s1']:,.0f}")

    if "BULLISH" in market["signal"]:
        st.success(f"**QUANT ENGINE SIGNAL:** {market['signal']}")
    elif "BEARISH" in market["signal"]:
        st.error(f"**QUANT ENGINE SIGNAL:** {market['signal']}")
    else:
        st.warning(f"**QUANT ENGINE SIGNAL:** {market['signal']}")

    df_chart = market["df"]
    fig = go.Figure()
    fig.add_trace(go.Candlestick(
        x=df_chart["Timestamp"], open=df_chart["Open"], high=df_chart["High"],
        low=df_chart["Low"], close=df_chart["Close"], name="5m Price"
    ))
    fig.add_trace(go.Scatter(x=df_chart["Timestamp"], y=df_chart["EMA_20"], line=dict(color="#00E5FF", width=2), name="20 EMA"))
    fig.add_trace(go.Scatter(x=df_chart["Timestamp"], y=df_chart["VWAP"], line=dict(color="#FFD600", width=2, dash="dot"), name="VWAP"))
    fig.add_hline(y=market["pivot"], line_dash="dash", line_color="gray", annotation_text="Pivot (P)")
    fig.add_hline(y=market["r1"], line_dash="dot", line_color="red", annotation_text="R1 Resistance")
    fig.add_hline(y=market["s1"], line_dash="dot", line_color="green", annotation_text="S1 Support")
    fig.update_layout(height=480, xaxis_rangeslider_visible=False, template="plotly_dark", margin=dict(l=20, r=20, t=30, b=20))
    st.plotly_chart(fig, use_container_width=True)

with tab2:
    st.subheader("Pre-Trade Risk Validation & Position Sizing Gatekeeper")
    st.write("Prevents emotional over-leveraging by enforcing **minimum 1:1.5 Risk-to-Reward** and **maximum 2% capital risk per trade**.")

    col_left, col_right = st.columns([1, 1])
    with col_left:
        side = st.radio("Order Direction", ["BUY", "SELL"], horizontal=True)
        lots = st.number_input("Number of Lots", min_value=1, max_value=50, value=1)
        default_sl = round(market["ltp"] * 0.995, 1) if side == "BUY" else round(market["ltp"] * 1.005, 1)
        default_tg = round(market["ltp"] * 1.01, 1) if side == "BUY" else round(market["ltp"] * 0.99, 1)
        entry_price = st.number_input("Entry Price (Rs.)", value=float(market["ltp"]), step=1.0)
        stop_loss = st.number_input("Stop-Loss Price (Rs.)", value=float(default_sl), step=1.0)
        target_price = st.number_input("Target Price (Rs.)", value=float(default_tg), step=1.0)
        setup_tag = st.selectbox("Technical Setup Tag", [
            "VWAP + 20 EMA Bullish Bounce",
            "R1/S1 Pivot Breakout",
            "Opening Range Breakout (ORB)",
            "Institutional Volume Spike"
        ])

    validation = engine.validate_pre_trade_risk(
        capital=account["total_capital"],
        max_risk_pct=account["max_risk_pct"],
        side=side,
        entry=entry_price,
        sl=stop_loss,
        target=target_price,
        lot_size=active_lot_size,
        requested_lots=lots
    )

    with col_right:
        st.markdown("### Gatekeeper Audit Result")
        m1, m2, m3 = st.columns(3)
        m1.metric("Total Quantity", f"{validation['total_qty']} Qty ({lots} Lots)")
        m2.metric("Risk-to-Reward", f"1 : {validation.get('rr_ratio', 0)}")
        m3.metric("Capital at Risk", f"Rs.{validation.get('total_trade_risk_rs', 0):,.2f} ({validation.get('risk_pct_of_capital', 0)}%)")

        st.info(f"**Max Recommended Safe Lots (2% Rule):** `{validation.get('recommended_max_lots', 0)} Lots` | **Potential Target Profit:** `Rs.{validation.get('potential_reward_rs', 0):,.2f}`")

        if validation["approved"]:
            st.success("RISK AUDIT PASSED: Trade complies with institutional risk parameters.")
            if st.button("Execute & Log ACID Order to SQLite", type="primary", use_container_width=True):
                db.insert_trade(
                    symbol=selected_symbol, side=side, lots=lots, quantity=validation["total_qty"],
                    entry_price=entry_price, stop_loss=stop_loss, target_price=target_price, setup_tag=setup_tag
                )
                st.toast("Order committed to SQLite database!")
                st.rerun()
        else:
            st.error("ORDER BLOCKED BY RISK GATEKEEPER:")
            for reason in validation["reasons"]:
                st.markdown(f"- {reason}")
            st.button("Order Execution Locked (Fix Risk Parameters)", disabled=True, use_container_width=True)

with tab3:
    st.subheader("Active Open Positions & Mark-to-Market (MTM) Desk")
    open_trades = db.fetch_trades(status="OPEN")

    if not open_trades:
        st.info("No open positions right now. Place a validated trade in Tab 2!")
    else:
        for t in open_trades:
            t_id, sym, t_side, t_lots, t_qty, t_entry, t_sl, t_tg, _, _, _, _, _, t_setup, t_time = t
            current_ltp = market["ltp"] if sym == selected_symbol else t_entry
            unrealized_pnl = (current_ltp - t_entry) * t_qty if t_side == "BUY" else (t_entry - current_ltp) * t_qty

            with st.container(border=True):
                col_a, col_b, col_c, col_d = st.columns([2, 2, 2, 2])
                col_a.markdown(f"**#{t_id} | {sym} ({t_side})**\n\n`{t_lots} Lots ({t_qty} Qty)` | *{t_setup}*")
                col_b.markdown(f"**Entry:** Rs.{t_entry:,.2f}\n\n**SL:** Rs.{t_sl:,.2f} | **Target:** Rs.{t_tg:,.2f}")
                col_c.metric("Live Unrealized MTM", f"Rs.{unrealized_pnl:,.2f}")
                exit_px = col_d.number_input(f"Exit Price (#{t_id})", value=float(current_ltp), key=f"exit_{t_id}")
                if col_d.button(f"Square Off #{t_id}", key=f"btn_{t_id}", type="primary"):
                    gross = (exit_px - t_entry) * t_qty if t_side == "BUY" else (t_entry - exit_px) * t_qty
                    charges = engine.calculate_indian_fno_charges(t_entry, exit_px, t_qty)
                    net = gross - charges
                    db.close_trade_in_db(t_id, exit_px, gross, charges, net)
                    st.success(f"Position #{t_id} Closed! Net Post-Tax PnL: Rs.{net:,.2f}")
                    st.rerun()

with tab4:
    st.subheader("Post-Trade Performance Audit & Indian Statutory Tax Ledger")
    closed_trades = db.fetch_trades(status="CLOSED")

    if closed_trades:
        cols = ["ID", "Symbol", "Side", "Lots", "Qty", "Entry", "SL", "Target", "Exit", "Status", "Gross PnL", "Taxes & Charges", "Net PnL", "Setup Tag", "Timestamp"]
        df_closed = pd.DataFrame(closed_trades, columns=cols)

        total_gross = df_closed["Gross PnL"].sum()
        total_taxes = df_closed["Taxes & Charges"].sum()
        total_net = df_closed["Net PnL"].sum()
        win_count = len(df_closed[df_closed["Net PnL"] > 0])
        win_rate = (win_count / len(df_closed)) * 100.0

        k1, k2, k3, k4 = st.columns(4)
        k1.metric("Gross Realized PnL", f"Rs.{total_gross:,.2f}")
        k2.metric("Total STT, GST & Brokerage", f"-Rs.{total_taxes:,.2f}")
        k3.metric("Net Post-Tax Profit", f"Rs.{total_net:,.2f}")
        k4.metric("Win Rate %", f"{win_rate:.1f}% ({win_count}/{len(df_closed)} Trades)")

        df_sorted = df_closed.sort_values("ID").copy()
        df_sorted["Cumulative Net PnL"] = df_sorted["Net PnL"].cumsum()
        fig_eq = go.Figure()
        fig_eq.add_trace(go.Scatter(
            x=df_sorted["Timestamp"], y=df_sorted["Cumulative Net PnL"],
            mode="lines+markers", fill="tozeroy", name="Net Post-Tax Equity Curve",
            line=dict(color="#00E676", width=3)
        ))
        fig_eq.update_layout(title="Cumulative Net Realized PnL (After STT, Brokerage & GST)", height=350, template="plotly_dark")
        st.plotly_chart(fig_eq, use_container_width=True)

        st.markdown("### Historical Closed Trade Journal (SQLite `trades` Table)")
        st.dataframe(df_closed[["ID", "Timestamp", "Symbol", "Side", "Qty", "Entry", "Exit", "Gross PnL", "Taxes & Charges", "Net PnL", "Setup Tag"]], use_container_width=True)
