/* This will will just get #include'd into the main user fnc .cc file.
 * All the relevant library include's will be on top of that file. */

constexpr double epsilon = 0.3010299956639812;

// Gracefully handle log10(0) case.
template<typename T>
inline auto llog10(T x) -> decltype(std::log10(x)) {
	if(x == 0) return 0;
	return std::log10(x) + epsilon;
}
inline int cceil(double x) noexcept {
	int r = static_cast<int>(x);
	return (r > 0 and x != (double)r) ? r+1 : r;
}
inline int ffloor(double x) noexcept {
	int r = static_cast<int>(x);
	return (x < 0 and x != (double)r) ? r-1 : r;
}

#define MAX_BINS_MICRO 256
#define MAX_RANGE_MICRO_DEFAULT 10'000'000 // Given in units of 10 ns ==> 100 ms = 0.1s, everything above that is overflow.
#define MIN_RANGE_MICRO_DEFAULT 1          // This is true zero in log scale (x axis).
/* Note: bin[0] shall ALWAYS start at 10 ns. Users can only change the maximum range of the scale. */

class MicrospillHist {
	uint32_t max_range; 
public:
	// Lookup array, _arr[] = {0,1, ... 255} used for index splicing.
	static constexpr auto _arr = []<size_t... I>(std::index_sequence<I...>) {
		return std::array{static_cast<uint32_t>(I)...};
	}(std::make_index_sequence<MAX_BINS_MICRO>{});

	uint32_t nbins = DEFAULT_BINS_MICRO;
	double max_range_log;
	uint32_t cutoff_index;

	std::string name;

	int32_t hits_counted;
	uint32_t overflows = 0;
	uint32_t arr[MAX_BINS_MICRO] = {0};

	uint32_t ecl_start, ecl_end; // values recorded at first hit/last hit in the spill.
	uint32_t start_ts, end_ts;   // from VULOM's clock, last hit and first hit in the spill.
	uint64_t spill_ts;           // from Whiterabbit, potentially.
	
	MicrospillHist() : max_range(MAX_RANGE_MICRO_DEFAULT),
		ecl_start(0), ecl_end(0),
		start_ts(0), end_ts(0)
	{
		max_range_log = log10(max_range);
		set_cutoff();
	}
	
	void set_range(uint32_t max_range) {
		assert(max_range > 100);
		this->max_range = max_range;
		max_range_log = log10(max_range);
		set_cutoff();
	}

	void set_bins(uint32_t nbins) {
		assert(nbins > 5 and nbins <= MAX_BINS_MICRO);
		this->nbins = nbins;
		set_cutoff();
	}

	uint32_t fill(unpack_event *event) {
		nil<1024>* delta_t = &event->trloii_mvlc.dt;
		uint32_t nitems = delta_t->_num_items;
		FOR(i, nitems) {
			uint32_t bin = static_cast<uint32_t>(nbins / max_range_log * log10(delta_t->_items[i].value));
			if(bin >= nbins) { ++overflows; --hits_counted; }
			else ++arr[bin];
		}
		hits_counted += nitems;
		return nitems;
	}

	void reset() {
		memset(arr, 0, sizeof(*arr) * nbins);
		overflows = 0; hits_counted = 0;
		ecl_start = 0; start_ts = 0;
	}

	/* For a list, e.g. [0,0,... 0, a1, a2, 0, a3, ... aN, 0, 0, ... 0]
	 * With bunch of trailing and leading 0's, slice them out but keep last remaining
	 * (possible) zero's on both sides. */
	std::pair<int,int> get_bounds() const {
		int l = 2;
		int r = 4;
		for(uint32_t i=0; i<nbins-1; ++i) {
			if(arr[i+1] != 0) { l=i; break; }
		}
		for(uint32_t i=nbins-1; i>0; --i) {
			if(arr[i-1] != 0) { r=i; break; }
		}
		return {l,r};
	}
private:
	// For Poisson prediction: take all bins up to `right_i` + a bit, above 20 ns cutoff
	void set_cutoff() {
		FOR(x,nbins) {
			if(max_range_log / nbins * (x+0.5) -8 > -7.6) {
				cutoff_index = x;
				return;
			}
		}
	}
};

/* Array size depends on the splice parameters, `left_i`, `right_i`, as such this function
 * cannot return an array, and must return a vector. 
 * `inds` is the vector of indices. */
std::vector<double> poisson_log_expected(std::vector<uint32_t> inds,
const uint32_t N0, const int32_t T_total, const uint32_t nbins, const double M) noexcept {
	const double f = N0 / (double)T_total;
	const double C = M / nbins;
	const double logN0 = log10(N0);

	std::vector<double> r; r.reserve(MAX_BINS_MICRO);
	for(uint32_t x : inds) {
		double val = logN0 + 
			log10(exp(-f * pow(10, x * C)) - exp(-f * pow(10, (x+1) * C))) 
			+ epsilon;
		val = (val > 0) ? val : 0.0;
		r.push_back(val);
	}

	return r;
}

constexpr auto lookup_time_scale = std::array{
       std::make_pair(0  , R"(1 s)"),
       std::make_pair(-1 , R"(100 ms)"),
       std::make_pair(-2 , R"(10 ms)"),
       std::make_pair(-3 , R"(1 ms)"),
       std::make_pair(-4 , R"(100 $\mathrm{\mu}$s)"),
       std::make_pair(-5 , R"(10 $\mathrm{\mu}$s)"),
       std::make_pair(-6 , R"(1 $\mathrm{\mu}$s)"),
       std::make_pair(-7 , R"(100 ns)"),
       std::make_pair(-8 , R"(10 ns)"),
       std::make_pair(-9 , R"(1 ns)")
};
constexpr auto lookup_y_scale = std::array{
	R"($1$)",      // [0]
	R"($10$)",     // [1]
	R"($10^{2}$)", // [2]
	R"($10^{3}$)", // [3]
	R"($10^{4}$)", // [4]
	R"($10^{5}$)", // [5]
	R"($10^{6}$)", // [6]
	R"($10^{7}$)", // [7]
	R"($10^{8}$)", // [8]
};

// Array of doubles, served to look-up the log10(x) values.
constexpr auto log10_lookup = []() {
	std::array<double, 10> logArray{};
	for(int i=0; i<2;  ++i) logArray[i] = NAN;
	for(int i=2; i<10; ++i) logArray[i] = log10(i);
	return logArray;
}();

using TicksTuple = std::tuple<
	std::vector<double>,         // major ticks
	std::vector<const char*>, // labels of major ticks
	std::vector<double>       // minor ticks
>;

// `xs` is already sorted.
TicksTuple GetXTicks(const std::vector<double>& xs) {
	const int minx = ffloor(xs.front());
	const int maxx = cceil(xs.back());
	std::vector<double> major_ticks(maxx - minx + 1);
	std::vector<double> minor_ticks;
	std::iota(major_ticks.begin(), major_ticks.end(), minx);

	std::vector<const char*> major_tick_labels(maxx - minx + 1);
	std::transform(major_ticks.begin(), major_ticks.end(), major_tick_labels.begin(), 
		[](auto x) { 
			for(int i=0; i < (int) lookup_time_scale.size(); ++i) {
				if(lookup_time_scale[i].first == static_cast<int>(x))
					return lookup_time_scale[i].second;
			}
			return "NaN";
	});
	
	for(size_t i=0; i < major_ticks.size()-1; ++i) {
		for(int j=2; j<10; ++j) 
			minor_ticks.push_back(
				major_ticks[i] + log10_lookup[j]
			);
	}

	return std::make_tuple(
		major_ticks, major_tick_labels, minor_ticks
	);
}

TicksTuple GetYTicks(const std::vector<double>& ys) {
	const int minx = 0;
	const double max_value = *std::max_element(ys.begin(), ys.end());
	const int maxx = ffloor(max_value * 1.08);

	std::vector<double> major_ticks(maxx - minx + 1);
	std::vector<const char*> major_tick_labels(maxx - minx + 1);
	std::vector<double> minor_ticks;
	
	std::iota(major_ticks.begin(), major_ticks.end(), epsilon);
	std::transform(major_ticks.begin(), major_ticks.end(), major_tick_labels.begin(),
		[](auto x) { return lookup_y_scale.at(static_cast<int>(x)); }
	);
	
	for(size_t i=0; i < major_ticks.size()-1; ++i) {
		for(int j=2; j<10; ++j) 
			minor_ticks.push_back(
				major_ticks[i] + log10_lookup[j]
			);
	}
	double last_tick = major_ticks.back();
	for(int j=2; j<10; ++j) {
		double tval = last_tick + log10_lookup[j];
		if(tval >= max_value) break;
		else minor_ticks.push_back(tval);
	}
	
	return std::make_tuple(
		major_ticks, major_tick_labels, minor_ticks
	);
}

/* ============ MACROSPILL ============ */

#define MAX_BINS_MACRO 1001
class MacrospillHist {
public:
	/* Last bin is for error'ed hits, where something went wrong. */
	uint32_t arr[MAX_BINS_MACRO+1] = {0};

	uint32_t bos_ts;
	uint32_t eos_ts;
	uint32_t offspill = 0;
	double time_in_spill = 0.0;
	bool is_first_after_bos = true;

	double bin_width = DEFAULT_BIN_MACRO;
	
	MacrospillHist() = default;

	/* Called at the end of EoS trigger. */
	void reset() {
		offspill = 0;
	}

	/* Called at the start of BoS trigger. */
	void init() {
		memset(arr, 0, sizeof(arr));
		time_in_spill = 0.0;
		is_first_after_bos = true;
	}
	void fill(unpack_event *event) {
		nil<1024>* delta_t = &event->trloii_mvlc.dt;
		uint32_t nitems = delta_t->_num_items;
		FOR(i, nitems) {
			time_in_spill += delta_t->_items[i].value / 1e8;
			int bin = std::min(static_cast<int>(time_in_spill/bin_width), MAX_BINS_MACRO);
			++arr[bin];
		}
	}

#if 0
	void fill(unpack_event *event) {
		nil<1024>* delta_t = &event->trloii_mvlc.dt;
		uint32_t nitems = delta_t->_num_items;
		if(!is_first_after_bos) {
			FOR(i, nitems) {
				time_in_spill += delta_t->_items[i].value / 1e8;
				int bin = std::min(static_cast<int>(time_in_spill/bin_width), MAX_BINS_MACRO);
				++arr[bin];
			}
		}
		else {
			/* Time t=0 is begining of spill. Don't rely on `delta_t` between hits,
			 * Initially, get time difference from absolute stamp relative to BoS stamp.
			 * Abusing the fact that timing list is sorted in time. */

			uint32_t init_ts;
			nil<1024>* timing = &event->trloii_mvlc.spill_extra.timing;
			if(timing->_num_items > 0) {
				init_ts = timing->_items[0].value;
			} else if(((timing = &event->trloii_mvlc.spill.timing) and timing->_num_items > 0)) {
				init_ts = timing->_items[0].value;
			} else return;

			time_in_spill = Scaler<>::calc_diff(init_ts, bos_ts) / 1e8; // Can be negative.
			nil<1024>* delta_t = &event->trloii_mvlc.dt;
			uint32_t nitems = delta_t->_num_items;
			for(uint32_t i=0; i < nitems; ++i) {
				if(i>0) time_in_spill += delta_t->_items[i].value / 1e8;
				if(time_in_spill < 0) { ++offspill; continue; }
				int bin = std::min(static_cast<int>(time_in_spill/bin_width), MAX_BINS_MACRO);
				++arr[bin];
			}
			is_first_after_bos = false;
		}
	}
#endif

	void fill_offspill(unpack_event *event) {
		nil<1024>* delta_t = &event->trloii_mvlc.dt;
		offspill += delta_t->_num_items;
	}

	using XYPair = std::pair<std::vector<double>, std::vector<int>>;
	XYPair get_xy() const {
		std::vector<double> xs;
		std::vector<int> ys;

		double spill_length = Scaler<>::calc_diff(eos_ts, bos_ts) / 1e8;
		if(spill_length < 0) {
			YELL("`spill_length` calculated as negative?\n");
			exit(1);
		}
		int last_bin = std::min(
			static_cast<int>(spill_length / bin_width),
			MAX_BINS_MICRO
		);
		for(int i=0; i<=last_bin; ++i) {
			xs.push_back((i+0.5) * bin_width);
			ys.push_back(arr[i]);
		}
		return {xs, ys};
	}

	uint32_t get_errors() const {
		return arr[MAX_BINS_MACRO];
	}

	uint32_t get_accumulated() const {
		uint32_t acc = 0;
		FOR(i, MAX_BINS_MACRO) acc += arr[i];
		return acc;
	}
};

MicrospillHist micro[4];
MacrospillHist Macro[4];

struct timespec sys_ts;

/* Convert the accumulated hist data into JSON that the TCP will send. */
json convert_to_json(const MicrospillHist& hist, const MacrospillHist& macro) {
	auto [left_i, right_i] = hist.get_bounds();
	assert(left_i > 0 and right_i <= (int)hist.nbins-1);

	std::vector<double> ys; ys.reserve(MAX_BINS_MICRO); // `y` height of each bin, log-scale
	std::vector<double> xs; xs.reserve(MAX_BINS_MICRO); // `x positions of each bin, log-scale
	for(int i = left_i; i <= right_i; ++i) ys.push_back(llog10(hist.arr[i]));

	double bin_width =  hist.max_range_log / hist.nbins;
	for(int i = left_i; i <= right_i; ++i) xs.push_back(bin_width * (i + 0.5) - 8);
	
	uint32_t elapsed_time_10ns = Scaler<>::calc_diff(hist.end_ts, hist.start_ts);
	// For Poisson prediction: take all bins up to `right_i` + a bit, above 20 ns cutoff
		
	const uint32_t* index_l = MicrospillHist::_arr.data() + hist.cutoff_index;
	right_i = std::min(right_i + 3, MAX_BINS_MICRO-1);
	const uint32_t* index_r = MicrospillHist::_arr.data() + right_i;
	std::vector<uint32_t> p_indices(index_l, index_r);
	std::vector<double> px(p_indices.size());
	std::vector<double> py;
	if(hist.hits_counted > 10) {
		py = poisson_log_expected(p_indices, 
			hist.hits_counted, elapsed_time_10ns,
			hist.nbins, hist.max_range_log);
		std::transform(p_indices.begin(), p_indices.end(), px.begin(),
			[bin_width](auto index) { return bin_width * (index + 0.5) - 8; });
	}
	else {
		py = {NAN};
		px = {NAN};
	}
	
	auto [xticks_major, xticks_major_label, xticks_minor] = GetXTicks(xs); 
	auto [yticks_major, yticks_major_label, yticks_minor] = GetYTicks(ys); 

	json j;
	j["name"] = hist.name;
	j["counted"] = hist.hits_counted;
	j["lost_hits"] = abs(hist.hits_counted - Scaler<>::calc_diff(hist.ecl_end, hist.ecl_start));
	j["overflows"] = hist.overflows;
	j["elapsed_time_10ns"] = elapsed_time_10ns;
	j["binx"] = std::move(xs);
	j["biny"] = std::move(ys);
	j["poisson_x"] = std::move(px);
	j["poisson_y"] = std::move(py);
	j["xticks_major"]       = std::move(xticks_major);
	j["xticks_major_label"] = std::move(xticks_major_label);
	j["xticks_minor"]       = std::move(xticks_minor);
	j["yticks_major"]       = std::move(yticks_major);
	j["yticks_major_label"] = std::move(yticks_major_label);
	j["yticks_minor"]       = std::move(yticks_minor);

	j["offspill"] = macro.offspill;
	auto [_vx, _vy] = macro.get_xy();
	
	j["macro_x"] = std::move(_vx);
	j["macro_y"] = std::move(_vy);
	j["macro_errors"] = macro.get_errors();
	return j;
}

void timestamp_to_string(uint64_t ts, char* buffer, size_t count=28) {
	time_t ts_s = ts / 1000000000;
	int cs = (int)((ts / 10000000) % 100);
	struct tm *tm_time = localtime(&ts_s);
	strftime(buffer, count, "%a %b %d %Y %H:%M:%S.", tm_time);
	char* buffer_end = buffer + strlen(buffer);
	sprintf(buffer_end, "%02d", cs);
}
