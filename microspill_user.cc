#include "structures.hh"
#include "common.hh"

/* Container to keep the values and increments in a stable way. Plus error notifications.*/
template<uint32_t N = 32>
class Scaler {
	static_assert(N <= 32, "Template parameter for `Scaler` must be <= 32.");
	static const uint32_t _mask = static_cast<uint32_t>((1ULL << N) - 1);
	static const int64_t wrap_point = 1LL << (N - 2);
public:
	uint32_t prev_data; 
	uint32_t curr_data; 
	Scaler() : prev_data(-1), curr_data(0xeeeeeeee) {} 
	
	inline void assign(uint32_t fresh) { 
		prev_data = curr_data; 
		curr_data = fresh & _mask; 
	} 
	uint32_t calc_increment() const { 
		if(curr_data >= prev_data) { 
			return curr_data - prev_data; 
		} 
		/* Possible miscounting! */
		if(prev_data - curr_data < static_cast<uint32_t>(wrap_point)) {
			printf("Backwards counting in scaler struct. Prev = %u, curr = %u\n", prev_data, curr_data); 
			return -1; 
		} 
		/* Wrap-around. */
		return (uint32_t)((1ll << N) + (int64_t)curr_data - (int64_t)prev_data); 
	}
	inline bool is_in_init() const {
		return (prev_data == (uint32_t)(-1)) && (curr_data == 0xeeeeeeee);
	}

	/* `x` and `y` should be at most one wrap-around different. */
	static int32_t calc_diff(uint32_t x, uint32_t y) noexcept {
		x &= _mask; y &= _mask;

		int64_t raw_diff = static_cast<int64_t>(x) - static_cast<int64_t>(y);
		if(raw_diff > wrap_point) { // `y` is one wrap ahead.
			return static_cast<int32_t>(raw_diff - (1ll<<N));
		}
		else if(raw_diff < -wrap_point) { // `x` is one wrap ahead.
			return static_cast<int32_t>(raw_diff + (1ll<<N));
		}
		else {
			return static_cast<int32_t>(raw_diff);
		}
	}
};

/* Part coming from Whiterabbit. Can be 0's if no module present. */
void unpack_wr_increment(unpack_event *event) {
	static uint64_t wr_prev = 0;
	DATA32 ts_lo = event->trloii_mvlc.wr_ts.ts_lo;
	DATA32 ts_hi = event->trloii_mvlc.wr_ts.ts_hi;
	DATA32* ts_inc = &event->trloii_mvlc.wr_ts.increment;

	uint64_t ts = (((uint64_t)ts_hi.value) << 32) | ts_lo.value;
	
	ts_inc->value = (uint32_t)(ts - wr_prev); // Nanoseconds.
	wr_prev = ts;
}

Scaler<> ecl_in[4];
Scaler<> vulom_time[4];

void unpack_header(unpack_event *event) { 
	uint32_t ecl_val = (&event->trloii_mvlc.header.ecl)->value; 
	uint32_t clk_val = (&event->trloii_mvlc.header.clk)->value; 
	
	auto ttype = event->trigger; // 1,2,3,4 ; 12,13 = bos/eos
	if(ttype == 12 || ttype == 13) ttype = 1;

	auto *ecl = &ecl_in[ttype - 1];
	auto *clk = &vulom_time[ttype - 1];
	
	bool is_in_init = ecl->is_in_init();
	ecl->assign(ecl_val);
	clk->assign(clk_val);

	if(is_in_init) return;
	
	DATA32* ecl_inc = &event->trloii_mvlc.header.inc_ecl; 
	DATA32* clk_inc = &event->trloii_mvlc.header.inc_clk;

	ecl_inc->value = ecl->calc_increment();
	clk_inc->value = clk->calc_increment();
}

template<uint32_t N>
using nil = raw_list_ii_zero_suppress<DATA32, DATA32, N>;

Scaler<31> last_ts[4]; 

void unpack_spill_data(unpack_event *event) {
	/* Relative to the trigger - the ACCEPT_TRIG[i] is always with a delay
	 * of ~491 clock cycles, relative to the VULOM clock (31 bits).
	 * So, this hit needs to be kicked out. */

	nil<1024>* out_delta_t = &event->trloii_mvlc.dt;
	nil<1024>* timing;
	
	bool is_trig_included = 0; 
	bool is_trig_kicked_out = 0;
	auto ttype = event->trigger; // 1,2,3,4 or 12,13
	if(ttype == 12 || ttype == 13) {
		ttype = 1; is_trig_included = 1;
	}

	auto* scaler = &last_ts[ttype - 1];
	
	bool is_in_init = scaler->is_in_init();
#define EXPAND \
	for(uint32_t i=0; i < timing->_num_items; ++i) { \
		uint32 val = timing->_items[i].value; \
		if(is_trig_included && !is_trig_kicked_out) { \
			uint32_t clk_val = (&event->trloii_mvlc.header.clk)->value; \
			int diff = Scaler<31>::calc_diff(clk_val, val); \
			if(diff > 490 && diff < 512) { \
				/* Fake hit, coming from trigger input. Don't map it to the *scaler object. */\
				is_trig_kicked_out = true; \
				continue; \
			} \
		} \
		/* 32nd bit is error marker. 
		 * Means one or more hits between `valid` items got simply lost. 
		 * This doesn't happen until ~2.5 MHz (in one channel). */\
		scaler->assign(val); \
		if(is_in_init) continue; \
		uint32_t dt = scaler->calc_increment(); \
		if(val & 0x80000000) { /* One hit in between has been lost for sure. Try to fake it. */ \
			out_delta_t->append_item().value = dt / 2; \
			out_delta_t->append_item().value = dt; \
		} \
		else { /* No hits lost. */ \
			out_delta_t->append_item().value = dt; \
		} \
	}

	/* Try to see if there's a block in front. */
	timing = &event->trloii_mvlc.spill_extra.timing;
	EXPAND

	timing = &event->trloii_mvlc.spill.timing;
	EXPAND
#undef EXPAND
	
#ifdef DEBUG
	uint32_t clk_val = (&event->trloii_mvlc.header.clk)->value; 
	nil<1024>* out_dtrig = &event->trloii_mvlc.trig_dt;
	nil<1024>* out_dtrig_sgn = &event->trloii_mvlc.trig_dt_sgn;
	for(uint32_t i=0; i < timing->_num_items; ++i) { 	
		uint32 val = timing->_items[i].value;
		int diff = Scaler<31>::calc_diff(val, clk_val);
		out_dtrig->append_item().value = std::abs(diff);
		out_dtrig_sgn->append_item().value = (val > clk_val) ? 1 : 0;
	}
#endif
}

int unpack_user_function(unpack_event *event) {
	unpack_wr_increment(event);
	unpack_header(event);
	unpack_spill_data(event);

	return 1;
}


bool handle_command_line_option(const char *arg)
{
	return false;
}

void usage_command_line_options() {}

//void init_user_function() {} 
