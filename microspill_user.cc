#include "cmath"
#include <thread>
#include <future>
#include <iostream>
#include <fstream>
#include <regex>
#include <cassert>

#include "structures.hh"
#include "common.hh"
#include "nlohmann/json.hpp"
using json = nlohmann::json;

constexpr double million = 1'000'000.0;
constexpr double clock_freq = 100'000'000.0;

#define DEFAULT_BINS_MICRO 100
#define DEFAULT_BIN_MACRO 0.1

struct g_config_t {
	std::string name[4] = {"ECL_IN(1)", "ECL_IN(2)", "ECL_IN(3)", "ECL_IN(4)"};
	int nbins_micro[4] = {DEFAULT_BINS_MICRO, DEFAULT_BINS_MICRO, DEFAULT_BINS_MICRO, DEFAULT_BINS_MICRO};
	int max_range_micro[4] = {-1,-1,-1,-1}; // -1 = default.

	double acc_period_macro[4] = {DEFAULT_BIN_MACRO,DEFAULT_BIN_MACRO,DEFAULT_BIN_MACRO,DEFAULT_BIN_MACRO};
	
	bool json_dump = false;
	bool should_send_json = false;

	int tcp_port = 8888;
} g_config;

class MicrospillHist;
class MacrospillHist;

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
	uint32_t calc_increment() const noexcept { 
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
		return (prev_data == (uint32_t)(-1)) and (curr_data == 0xeeeeeeee);
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
		if(val & 0x80000000) { \
			/* One hit in between has been lost for sure. 
			 * Try to fake it by supposing it's right in the middle of them. */ \
			out_delta_t->append_item().value = dt / 2; \
			out_delta_t->append_item().value = dt / 2; \
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

#include "zmqpp/zmqpp.hpp"
zmqpp::context *context;
zmqpp::socket *pub;

#include "tcp/microspill.hpp"
json jmicro;
char ts_string[32] = {'\0'};

enum class SpillStatus {
	Unknown,
	Onspill,
	Offspill
};

int unpack_user_function(unpack_event *event) {
	unpack_wr_increment(event);
	unpack_header(event);
	unpack_spill_data(event);

	static uint32_t bos_ts = 0;
	static uint32_t eos_ts = 0;
	static uint32_t spill_number = 0;

	static SpillStatus spill_status = SpillStatus::Unknown;
	auto ttype = event->trigger; /* 1,2,3,4 ; 12,13 */

	if(!g_config.should_send_json) goto return_placeholder;
	
	if(ttype == 12) { // BoS
		bos_ts = vulom_time[0].curr_data;
		FOR(i,4) Macro[i].bos_ts = bos_ts;	
		
		FOR(i,4) { micro[i].reset(); Macro[i].init(); }

		auto r = micro[0].fill(event);
		if(r > 0) {
			micro[0].ecl_start = ecl_in[0].curr_data;
			micro[0].start_ts = vulom_time[0].curr_data;
		}
		Macro[0].fill(event);
		spill_status = SpillStatus::Onspill;
	}

	else if(ttype == 13) { // EoS
		eos_ts = vulom_time[0].curr_data;
		FOR(i,4) Macro[i].eos_ts = eos_ts;	
		auto r = micro[0].fill(event);	
		if(r > 0) {
			micro[0].ecl_end = ecl_in[0].curr_data;
			micro[0].end_ts = vulom_time[0].curr_data;
		}
		Macro[0].fill(event);
		/* Extract timestamp. */
		uint64_t ts = 0;
		if(event->trloii_mvlc.wr_ts.ts_hi == 0) {
			if(clock_gettime(CLOCK_REALTIME, &sys_ts) == 0) {
				ts = sys_ts.tv_nsec + (uint64_t)sys_ts.tv_sec * 1000000000ULL;
			} else {
				WARN("Error "); perror("clock_gettime");
			}
		}
		else {
			ts = (((uint64_t)(event->trloii_mvlc.wr_ts.ts_hi) << 32) | 
			       (uint64_t)event->trloii_mvlc.wr_ts.ts_lo)
				  - 1000000000ULL * (LEAP_SECONDS + TAI_AHEAD_OF_UTC);
		}
		timestamp_to_string(ts, ts_string);
			
		/* If the spill is not fully sampled, don't histogram the data.
		 * Initially unpacker can start catching packets within ongoing spill, 
		 * catching an EoS without first catching BoS. */
		
		if(spill_status != SpillStatus::Unknown) {
			/* Convert to JSON. */
			std::vector<std::future<json>> json_future;
			FOR(i,4) {
				json_future.emplace_back(std::async(std::launch::async,
					convert_to_json, std::ref(micro[i]), std::ref(Macro[i])));
			}
			FOR(i,4) {
				jmicro["data"][i] = std::move(json_future[i].get());
			}
			jmicro["spill_number"] = ++spill_number;
			jmicro["spill_duration"] = Scaler<>::calc_diff(eos_ts, bos_ts);
			jmicro["timestamp"] = ts_string;
			
			if(g_config.json_dump) {
				const char* fileName = "tcp/example.json";
				json j;
				std::ofstream file(fileName);
				file << std::setw(4) << jmicro.dump(4) << std::endl;
				WARN(KBH_RED "\nSampling done. Check " EMPH(%s) "\n", fileName);
				printf(BOLD ".. Exiting\n\n" KNRM); 
				exit(0);
			}

			/* Send over TCP */
			std::string message = jmicro.dump();
			
			/* This call can block at most UCESB_TCP_SERVER_TIMEOUT ms. */
			pub->send(message);
		}
		FOR(i,4) Macro[i].reset();
		spill_status = SpillStatus::Offspill;
	}
	
	else {
		if(ttype > 4) {
			YELL("Trigger type: %u; should be < 5. Aborting.\n", ttype); exit(1);
		}
		int i = ttype - 1;
		if(spill_status == SpillStatus::Onspill) {
			micro[i].fill(event);
			
			// Assign initial ECL_IN(x) status.
			if(micro[i].ecl_start == 0) {
				micro[i].ecl_start = ecl_in[i].curr_data;
				micro[i].start_ts = vulom_time[i].curr_data;
			}
			// Assign `potential` final ECL_IN(x) status.
			micro[i].ecl_end = ecl_in[i].curr_data;
			micro[i].end_ts = vulom_time[i].curr_data;

			Macro[i].fill(event);
		}
		else if(spill_status == SpillStatus::Offspill) {
			Macro[i].fill_offspill(event);
		}
	}

return_placeholder:
	return 1;
}

bool handle_command_line_option(const char *arg) {
#define MATCH_PREFIX(prefix,post) (strncmp(arg,prefix,strlen(prefix)) == 0 and *(post = arg + strlen(prefix)) != '\0')
#define MATCH_ARG(name) (strcmp(arg,name) == 0)
	
	if(MATCH_ARG("--json_dump")) {
		WARN(KBH_RED "Will sample only one spill, then quit the program!\n\n" KNRM);
		g_config.json_dump = true;
		g_config.should_send_json = true;
		return true;
	}
	const char* post;
	if(MATCH_PREFIX("--json,", post)) {
		std::regex re(R"(^port=([1-9]\d*)$)");
		std::cmatch m;
		if(std::regex_match(post, m, re)) {
			int v;
			try {
				v = (int)stoi(m[1].str());
				if(v < 1024 || v >= (1<<16)) throw std::exception{};
			}
			catch(std::exception& e) {
				YELL("TCP port cannot be parsed or isn't in [1023, 65535] interval.\n");
				return false;
			}
			WARN("Parsed sending JSON, on port: " BOLD "%d\n" KNRM, g_config.tcp_port); 	
			g_config.tcp_port = v;
			g_config.should_send_json = true;
			return true;
		}
	}
	if(MATCH_ARG("--json")) {
		WARN("Parsed sending JSON, on port: " BOLD "%d\n" KNRM, g_config.tcp_port); 	
		g_config.should_send_json = true;
		return true;
	}
	
	if(MATCH_PREFIX("--nbins_micro", post)) {
		std::regex re(R"(^(_[1-4])?=([1-9]\d*)$)");
		std::cmatch m;
		if(std::regex_match(post, m, re)) {
			int i = -1;
			int val;
			if(m[1].matched) i = (int)(*(m[1].str().c_str() + 1) - '0') - 1;
			try { val = std::stoi(m[2].str()); }
			catch(std::exception& e) { WARN("Exception: "); std::cout << e.what() << std::endl; return false; }
			if(i == -1) 
				FOR(k,4) g_config.nbins_micro[k] = val;
			else
				g_config.nbins_micro[i] = val;
			WARN("Parsed " EMPH(--nbins_micro) BOLD ": %d" KNRM, val);
			if(i == -1) printf(" for all four channels.\n");
			else printf(" for channel: " BOLD "%d" KNRM "\n", i+1);
			return true;
		}
	}
	
	if(MATCH_PREFIX("--bin_macro", post)) {
		std::regex re(R"(^(_[1-4])?=((0|[1-9]\d*)(\.\d*)?)$)");
		std::cmatch m;
		if(std::regex_match(post, m, re)) {
			int i = -1;
			double val;
			if(m[1].matched) i = (int)(*(m[1].str().c_str() + 1) - '0') - 1;
			try { 
				val = std::stod(m[2].str());
				if(val < 0.05 or val >= 2.0) throw std::out_of_range("parsed: " + std::to_string(val) + " which is <0.05 or >2.0");
			}
			catch(std::exception& e) {
				YELL("Parsing error: ");
				std::cout << e.what() << std::endl;
				return false;
			}
			if(i == -1) 
				FOR(k,4) g_config.acc_period_macro[k] = val;
			else
				g_config.acc_period_macro[i] = val;
			WARN("Parsed " EMPH(--bin_macro) BOLD ": %.3f seconds," KNRM, val);
			if(i == -1) printf(" for all four channels.\n");
			else printf(" for channel: " BOLD "%d" KNRM "\n", i+1);
			return true;
		}
	}
	
	if(MATCH_PREFIX("--alias", post)) {
		std::regex re(R"(^_([1-4])=([^=]+)$)");
		std::cmatch m;
		if(std::regex_match(post, m, re)) {
			int i = (int)(*m[1].str().c_str() - '0') - 1;
			g_config.name[i] = m[2].str();

			WARN("Successfully parsed " EMPH(--alias) KBH_GRN " '%s' " KNRM "for channel: " BOLD "%d\n" KNRM, m[2].str().c_str(), i+1);
			return true;
		} 	
	}

	return false;
}

void usage_command_line_options() {
	printf(EMPH(Unpacker specific utilities) "\n");
	printf(BOLD "  --json_dump        " KNRM
		   "Dump the example JSON of micro- and macrospill data format for one spill, and then terminate the program.\n");
	printf(BOLD "  --json[,port=N]     " KNRM
		   "Send the spill histogramm'ed data in JSON format over port number N. Default port number is 8888.\n");
	printf(BOLD "  --nbins_micro=N    " KNRM
			"Bin all four channels of microspill data in N bins. Default %d.\n", DEFAULT_BINS_MICRO);
	printf(BOLD "  --nbins_micro_i=N  " KNRM
			"Bin the microspill data from " BOLD "i" KNRM "th channel in N bins, where i=1,2,3 or 4.\n");
	printf(BOLD "  --bin_macro=N      " KNRM
			"Bin all four channels of macrospill data in bin-widths of N seconds (decimal). Default %.1fs.\n", DEFAULT_BIN_MACRO);
	printf(BOLD "  --bin_macro_i=N    " KNRM
			"Bin the macrospill data from " BOLD "i" KNRM "th channel in bin-widths of N seconds (decimal). Default %.1fs.\n", DEFAULT_BIN_MACRO);
	printf(BOLD "  --alias_i=name     " KNRM
		   "Alias the channel ECL_IN(i) to a new name `name`, where i=1,2,3 or 4. Quote the \"name\" if you use whitespaces.\n");
}

void init_user_function() {
	if(g_config.should_send_json) {
		context = new zmqpp::context;
		pub = new zmqpp::socket(*context, zmqpp::socket_type::publish);
		char _s[64] = {'\0'};
		sprintf(_s, "tcp://*:%d", g_config.tcp_port);
		try {
			pub->bind(_s);
			WARN("Successfully bound JSON server to TCP port: " EMPH(%d) ".\n", g_config.tcp_port);
		}
		catch(std::exception& e) {
			YELL("\nError: Unable to bind to TCP port: %d .. Exiting.\n\n", g_config.tcp_port);
			WARN(" .. was executing pub->bind(\"%s\")\n", _s);

			if(pub && pub->operator bool()) pub->close();
			exit(2);
		}
		/* Put a small timeout (in milliseconds), how long a send call
		 * can block the main thread. */
#define UCESB_TCP_SERVER_TIMEOUT 30
		pub->set(zmqpp::socket_option::send_timeout, UCESB_TCP_SERVER_TIMEOUT);

		FOR(i,4) {
			micro[i].name = g_config.name[i];
			micro[i].set_bins(g_config.nbins_micro[i]);
			if(g_config.max_range_micro[i] > 100)
				micro[i].set_range(g_config.max_range_micro[i]);
		}
		
		jmicro["data"] = json::array({
			json::object(),
			json::object(),
			json::object(),
			json::object()
		});

		FOR(i,4) {
			Macro[i].bin_width = g_config.acc_period_macro[i];
		}
	}
} 

void exit_user_function() {
	if(pub && pub->operator bool()) pub->close();
	if(context && context->operator bool()) context->terminate();
	
	if(g_config.should_send_json) {
		WARN("Cleaned up the TCP (network) processes.\n");
	}
}
