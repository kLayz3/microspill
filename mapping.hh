/* Keep WR mapped to RAW,.. if there's no WR then this maps to consistent 0's. */
SIGNAL(WR_LO, trloii_mvlc.wr_ts.ts_lo, DATA32);
SIGNAL(WR_HI, trloii_mvlc.wr_ts.ts_hi, DATA32);
SIGNAL(WR_INCREMENT, trloii_mvlc.wr_ts.increment, DATA32);

/* TPAT */
SIGNAL(TPAT, trloii_mvlc.header.tpat.tpat, DATA32);

/* Status of the 32-low bits of 10 ns VULOM clock. */
SIGNAL(VULOM_CLOCK, trloii_mvlc.header.clk, DATA32);

SIGNAL(ECL_FULL, trloii_mvlc.header.ecl, DATA32);

/* Increments since last of this trigger type. */
SIGNAL(TS_INCREMENT, trloii_mvlc.header.inc_clk, DATA32);
SIGNAL(ECL_INCREMENT, trloii_mvlc.header.inc_ecl, DATA32);

/* Timing differences. */
SIGNAL(NO_INDEX_LIST: DELTA_T_1024);
SIGNAL(DELTA_T_1, trloii_mvlc.dt, DATA32);

#ifdef DEBUG
SIGNAL(NO_INDEX_LIST: TRIG_DIFF_1024);
SIGNAL(TRIG_DIFF_1, trloii_mvlc.trig_dt, DATA32);

SIGNAL(NO_INDEX_LIST: DTTRIG_SIGN_1024);
SIGNAL(DTTRIG_SIGN_1, trloii_mvlc.trig_dt_sgn, DATA32);

SIGNAL(NO_INDEX_LIST: RAWTS_1024);
SIGNAL(RAWTS_1, trloii_mvlc.spill.timing, DATA32);
#endif
