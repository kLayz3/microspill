#include "common.hh"

DUMMY() {
	UINT32 dummy;
}

TIMESTAMP_WHITERABBIT()
{
	MEMBER(DATA12 subsystem_id);
	MEMBER(DATA32 ts_lo);
	MEMBER(DATA32 ts_hi);
	MEMBER(DATA32 increment);

	UINT32 header NOENCODE {
		0_11:  id;
		12_15: 0;
		16: error_bit;
		17_31: 0;
		ENCODE(subsystem_id, (value=id));
	};
	UINT16 tll_marker NOENCODE {
		0_15: 0x03e1;
	}
	UINT16 tll NOENCODE;
	
	UINT16 tlh_marker NOENCODE {
		0_15: 0x04e1;
	}
	UINT16 tlh NOENCODE;

	UINT16 thl_marker NOENCODE {
		0_15: 0x05e1;
	}
	UINT16 thl NOENCODE;
	
	UINT16 thh_marker NOENCODE {
		0_15: 0x06e1;
	}
	UINT16 thh NOENCODE;

	ENCODE(ts_lo, (value = ((static_cast<unsigned>(tlh) << 16)) | tll)); 
	ENCODE(ts_hi, (value = ((static_cast<unsigned>(thh) << 16)) | thl));
}

TPAT() {
	MEMBER(DATA32 tpat);
	UINT32 word NOENCODE {
		0_16: tpat;
		17_21: 0;
		22_23: start_mask;
		24_27: trig_num;
		28_31: lec;
		ENCODE(tpat, (value = tpat));
	}
}

MUX_HEADER() {
	MEMBER(DATA32 ecl);
	MEMBER(DATA32 inc_ecl);
	MEMBER(DATA32 clk);
	MEMBER(DATA32 inc_clk);

	UINT32 barrier NOENCODE {
		0_31: b = MATCH(0xbeefbabe);
	}
	
	tpat = TPAT();
	
	UINT32 ecl_word NOENCODE {
		0_31: data;
		ENCODE(ecl, (value = data));
	}
	UINT32 clk_word NOENCODE {
		0_31: data;
		ENCODE(clk, (value = data));
	}
}

TRLOII_MULTI_TIMING(stackheader) {
	MEMBER(DATA32 timing[1024] NO_INDEX_LIST);

	UINT32 mvlc_header NOENCODE {
		0_10: nwords;
		11_15: 0;
		16_31: indicator = MATCH(stackheader);
	}
	
	list(0 <= i < mvlc_header.nwords) {
		UINT32 w {
			0_31: data;
			ENCODE(timing APPEND_LIST, (value = data));
		}
	}
};


SUBEVENT(multi_timing_trloii) {
	/* Handled in user function */
	MEMBER(DATA32 dt[1024] NO_INDEX_LIST);

#ifdef DEBUG
	MEMBER(DATA32 trig_dt[1024] NO_INDEX_LIST);
	MEMBER(DATA32 trig_dt_sgn[1024] NO_INDEX_LIST);
#endif

	select optional {
		wr_ts = TIMESTAMP_WHITERABBIT();
	}
	
	header = MUX_HEADER();
	
	/* For some reason, this guy appears when the rate is very high ..? */
	select optional {
		spill_extra = TRLOII_MULTI_TIMING(stackheader = 0xf580);
	}

	spill = TRLOII_MULTI_TIMING(stackheader = 0xf500);
}

EVENT {
	trloii_mvlc = multi_timing_trloii(procid=69, control=30);
	ignore_unknown_subevent;
}

#include "mapping.hh"
