/* Compilation is a few seconds longer due to <regex.h>. Maybe avoid it.. ? 
 * ¯\_(ツ)_/¯ 
 * Author: Martin Bajzek */

#include <cstring>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <string>
#include <iostream>
#include <regex>
#include <algorithm>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <queue>
#include <condition_variable>
#include <atomic>

#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include "ext_data_clnt_stderr.hh"
#include "ext_data_struct_info.hh"
#include "ext_data_client.h"
#include "time.h"

#include "colour-coding.hh" 
#include "ext_struct.hh"

//#define ONLY_MICROSPILL

#include <chrono>
template <
	class result_t   = std::chrono::milliseconds,
	class clock_t    = std::chrono::steady_clock,
	class duration_t = std::chrono::milliseconds
>
auto since(std::chrono::time_point<clock_t, duration_t> const& start) {
	return std::chrono::duration_cast<result_t>(clock_t::now() - start).count();
}
using std::chrono::time_point;
using std::chrono::steady_clock;

#define LEN(x) ( sizeof x / sizeof *x )

#ifdef __unix__
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#elif defined(__WIN32) || defined(WIN32)
#define __FILENAME__  (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
#define __FILENAME__ __FILE__
#endif

#define YELL(...) \
	do { \
		fprintf(stderr, KGRN "%s" KNRM ":" KCYN "%d" KNRM " => ", __FILENAME__, __LINE__); \
		fprintf(stderr, KBH_RED); fprintf(stderr, __VA_ARGS__); fprintf(stderr, KNRM); \
	} while(0);
#define WARN(...) \
	do { \
		fprintf(stderr, KGRN "%s" KNRM ":" KCYN "%d" KNRM " => ", __FILENAME__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
	} while (0);

#define FOR(i, m) for(uint32_t i=0; i<(m); ++i)

#define LEAP_SECONDS 27
#define TAI_AHEAD_OF_UTC 10

constexpr double million = 1'000'000.0;
constexpr double clock_freq = 100'000'000.0;

bool ParseCmdLine(const char*, std::string&, int, char**);
bool IsCmdArg(const char*, int, char**);
void verify_no_arguments_left(int, char**);

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

/* A flag, that a printing thread has to respect. */
std::atomic<bool> is_printing(false);

class EventQueue {
	using T = EXT_STR_h101;
	std::queue<T> queue;
	std::mutex mtx;
	std::condition_variable cv;
	using MtxLock = std::lock_guard<std::mutex>;
public:
	void push(const T& item) {
		{
			MtxLock lock(mtx);
			queue.push(item);
		}
		cv.notify_one();
	}
	
	T pop() {
		std::unique_lock<std::mutex> lock(mtx);
		cv.wait(lock, [this] { return !queue.empty(); });
		T item = std::move(queue.front());
		queue.pop();
		return item;
	}

	int clean() {
		MtxLock lock(mtx);
		int s = queue.size();
		std::queue<T>().swap(queue);
		return s;
	}
};

inline double llog10(uint32_t x) {
	return (x == 0) ? 0.0 : log10(x); 	
}

/* Wrapper a'la TH1I, for easier histogramming. 
 * Normal TH1 doesn't allow logarithmic scale. */
#define MAX_BINS_MICRO_DEFAULT 100
#define MAX_RANGE_DEFAULT 1'000'000 // 10 ns units
template<uint32_t nbins = MAX_BINS_MICRO_DEFAULT>
class MicrospillHist {
	static_assert(nbins > 2);
	using T = EXT_STR_h101;
	int max_range; // in units of 10ns.
	double max_range_log;
	uint32_t arr[nbins];
public:
	uint32_t hits_counted;
	uint32_t overflows = 0;
	char name[32] = {'\0'};
	
	MicrospillHist() : max_range(MAX_RANGE_DEFAULT) {
		max_range_log = llog10(max_range);
		memset(arr, 0, sizeof(arr));
	}
	void set_range(uint32_t max_range) {
		assert(max_range > 100);
		this->max_range = max_range;
		max_range_log = llog10(max_range);
	}
	
	void set_name(char* newname) {
		memset(name, '\0', sizeof(name));
		for(int i=0; i < 32 && name[i] != '\0'; ++i) {
			name[i] = newname[i];
		}
	}

	void fill(T* event) {
		FOR(i, event->DELTA_T) {
			uint32_t x = floor(nbins * log10(event->DELTA_Tv[i]) / max_range_log);
			if(x >= nbins) ++overflows;
			else ++arr[x];
		}
		hits_counted += event->DELTA_T;
	}
	
	void print(int index, int ecl_diff, int ts_diff) {
		println("INDEX:%d HIST:%s NBINS:%d MIN:%.4f MAX:%u MAX_LOG:%.4f ECL_DIFF:%d COUNTED:%u OVERFLOWS:%u ELAPSED_TIME:%u", 
		index, name, nbins, 0.0, max_range, max_range_log, ecl_diff, hits_counted, overflows, ts_diff);
		FOR(i,nbins) {
			printf("%d:%u ", i, arr[i]);
		}
		printf("\n");
		fflush(stdout);
	}
	void clear() {
		memset(arr, 0, sizeof(arr));
		overflows = 0;
		hits_counted = 0;
	}
};

ext_data_clnt_stderr *gclient = nullptr;
void sig_callback_handler(int signum) {
	printf("\nCaught abort signal.\n");
	if(gclient) gclient->close();
	exit(signum);
}

int main(int argc,char *argv[]) {
	char _help[2048] = {'\0'};
	sprintf(_help, \
	"\nThis program serves to capture and compress the output of " KGRN "../microspill" KNRM " struct writer. Check README.\n"
	"It will by default print to stdout, which can then be piped into different programs or file handles.\n"
	"Microspill summary is always sent at end-of-spill (EoS) event.\n"
	"Usage: " "%s SERVER" KCYN " [OPT]" KNRM "\n\n"
	"Optional OPTs can be passed as --tag=value (GNU style) or -tag value (Windows style).\n"
	"  --port                       Specify which port to connect to. Can be left empty.\n"
	"  --alias_i=name               Alias the channel ECL_IN(i) to a new name `name`, where i = 1,2,3 or 4.\n"
	"                               Do not use whitespaces inside the (quoted) name. They will be stripped.\n"
	"  --help                       Print this message.\n\n",
	argv[0]);
			/* Fetch an event. */

	if(IsCmdArg("help", argc, argv)) {
		printf("%s", _help); return 0;
	}
	
	ext_data_clnt_stderr client;
	int ok;
	EXT_STR_h101 event;
	ext_data_struct_info struct_info;
	uint32_t struct_map_success;
	signal(SIGINT, sig_callback_handler);

	if (argc < 2) {
		YELL("No server argument provided.\n\n");
		printf("%s", _help);
		exit(1);
	}
	
	/* Connect via port. */
	std::string parse_str;
	if(ParseCmdLine("port", parse_str, argc, argv)) {
		int portnum = atoi(parse_str.c_str());
		if(!portnum) {
			YELL("Port number `%s` not convertible to int.\n", argv[2]);
			printf("%s", _help);
			exit(1);
		}
		if(!client.connect(argv[1], portnum)) {
			YELL("Cannot connect to the client: %s:%d\n", argv[1], portnum);
			exit(1);
		}
	}
	else {
		if(!client.connect(argv[1])) {
			YELL("Cannot connect to the client: %s\n", argv[1]);
			exit(1);
		}
	}

	EXT_STR_h101_ITEMS_INFO(ok, struct_info, 0, EXT_STR_h101, 0);
	if (!ok) {
		perror(".. Ext_data_struct_info_item\n");
		YELL("Failed to setup structure information.\n");
		exit(1);
	}

	/* Handle all the aliasing. */
	std::string alias[4];

	for(int i=0; i<=3; ++i) {
		char lhs[10];
		sprintf(lhs, "alias_%d", i+1);
		if(ParseCmdLine(lhs, parse_str, argc, argv)) {
			// Trim whitespaces and ':'.
			parse_str.erase(std::remove_if(parse_str.begin(), parse_str.end(), 
				[](char c) { return std::isspace(c) || c == ':'; }), parse_str.end());
			if(parse_str.size() > 31) {
				YELL("\nReally .. You need such a big name '%s' for ECL_IN(%d)? Too long, ignoring it. Max char length is 31. \n\n", parse_str.c_str(), i+1);
				continue;
			}
			alias[i] = parse_str;
		}
	}

	/* TODO: allow handling of data without BOS/EOS,.. just based on a set time interval. */
	bool no_bos_eos = 0;
		
	if (! client.setup(NULL, 0,
				&struct_info, &struct_map_success,
				sizeof(event),
				"",NULL,
				EXT_DATA_ITEM_MAP_OK))
	{
		WARN("Cannot setup client. Call returned non-zero\n");
		client.close(); return -1;
	}
	
	gclient = &client; /* To gracefully handle ctrl-C and not block ports. */
	
	/* Fetch a batch of events for first ~0.3s . 
	 * The STRUCT server can have tons (30-40k) of events in the output buffer, basically backwards maybe 
	 * half a minute or so. Initial sanity checks... */
	{
		using namespace std::chrono;
		time_point<steady_clock, milliseconds> t_init;
		for (;;) {
			if (!client.fetch_event(&event,sizeof(event))) {
				WARN("Initial event fetch abruptly over? .. closing the program.\n");
				exit(1);
			}
			if(since(t_init) > 300) 
				break;
		}
	}

	srand(time(NULL));
	using milliseconds = std::chrono::milliseconds;	

	/* By default, the ucesb LMD servers (stream/trans) ships events fastest every 1 seconds,
	 * with --flush=1 flag passed. Therefore, we calculate time difference
	 * based on the internal VULOM clock which should be 100 MHz clock. 
	 * Users can pass --ptn=(num|digit) flag to make it print faster/slower. */
	
	EventQueue q_micro;	
	EventQueue q_macro;
	WARN("Spawning threads.\n");

	/* Event fetcher thread. */
	std::thread t1 ( 
		[&q_micro, &q_macro, &client] {
			WARN("Thread 1 Spawned.\n");
			EXT_STR_h101 event; // shadows the outside `event`.

			for (;;) {
				if (!client.fetch_event(&event, sizeof(event))) {
					WARN("Event fetch loop abruptly over? Server closed? .. closing the program.\n");
					exit(1);
				}
				q_micro.push(event);
				q_macro.push(event);
			}
		}
	);
	
	/* Microspill thread. */
	std::thread t2 (
		[&q_micro, no_bos_eos,
			&alias] {
			WARN("Thread 2 Spawned.\n");
			MicrospillHist<> hist[4];
			FOR(i,4) {
				if(alias[i].size() > 0) 
					strcpy(hist[i].name, alias[i].c_str());
				else
					sprintf(hist[i].name, "%d", i+1);
			}

			EXT_STR_h101 event;
			uint32_t ttype;
			int bos_counter = 0;
			int eos_counter = 0;
			struct timespec sys_ts;
			uint32_t scaler_start[4] = {0};
			uint32_t scaler_start_ts[4] = {0};
			uint32_t scaler_end[4] = {0};
			uint32_t scaler_end_ts[4] = {0};
			
			for(;;) {
				event = q_micro.pop();
				ttype = event.TRIGGER;
				// BoS
				if(ttype == 12) { 
					WARN("Fetched BoS: %d\n", ++bos_counter);
					FOR(i,4) hist[i].clear();
					memset(scaler_start, 0, sizeof(scaler_start));
					memset(scaler_start_ts, 0, sizeof(scaler_start_ts));	
					hist[0].fill(&event);
					
					if(event.DELTA_T > 0) {
						scaler_start[0] = event.ECL_FULL;
						scaler_start_ts[0] = event.VULOM_CLOCK;
					}
				}
				
				// EoS
				else if(ttype == 13) {
					WARN("Fetched EoS: %d\n", ++eos_counter);
					hist[0].fill(&event);
					if(event.DELTA_T > 0) {
						scaler_end[0] = event.ECL_FULL;
						scaler_end_ts[0] = event.VULOM_CLOCK;
					}

					/* Extract timestamp. */
					uint64_t ts = 0;
					if(event.WR_HI == 0) {
						if(clock_gettime(CLOCK_REALTIME, &sys_ts) == 0) {
							ts = sys_ts.tv_nsec + (uint64_t)sys_ts.tv_sec * 1000000000ULL;
						} else {
							WARN("Error "); perror("clock_gettime");
						}
					}
					else {
						ts = (((uint64_t)(event.WR_HI) << 32) | (uint64_t)event.WR_LO)
							- 1000000000ULL*( LEAP_SECONDS + TAI_AHEAD_OF_UTC);
					}
					/* Printing only done at type=13.
					 * Spin around until it's safe to print. */
					while(is_printing.exchange(true)) std::this_thread::sleep_for(milliseconds(1));

					println("TS:%lu", ts);
					FOR(i,4) {
						int diff = Scaler<>::calc_diff(scaler_end[i], scaler_start[i]);
						int ts_diff = Scaler<>::calc_diff(scaler_end_ts[i], scaler_start_ts[i]);
						hist[i].print(i+1, diff, ts_diff);
					}
					is_printing.store(false);
				}
				// Types 1-4
				else {
					if(ttype > 4) {
						YELL("Trigger type: %u; should be < 5. Aborting.\n", ttype); exit(1);
					}
					int i = ttype - 1;
					hist[i].fill(&event);
					// Assign initial ECL_IN(x) status.
					if(scaler_start[i] == 0) {
						scaler_start[i] = event.ECL_FULL;
						scaler_start_ts[i] = event.VULOM_CLOCK;
					}
					
					// Assign `potential` final ECL_IN(x) status.
					scaler_end[i] = event.ECL_FULL;
					scaler_end_ts[i] = event.VULOM_CLOCK;
				}
			}
		}
	);

	/* Macrospill thread. */	
	std::thread t3 ( 
		[&q_macro] {
			WARN("Thread 3 Spawned.\n");
			EXT_STR_h101 event;
			
			/* Make a scaler out of each ECL_IN(x); */
			uint32_t counted[4] = {0};
			uint32_t offspill_counted[4] = {0};
			int64_t ts_inc = 0LL;
			int64_t print_every = static_cast<int64_t>(0.1 * clock_freq); /* In 10ns units. */
			
			Scaler<> clock;
			uint32_t ts_bos = 0;
			uint32_t ts_eos = 0;
			bool is_in_spill = false;
			bool is_first_after_bos[4] = {0};
			
			for(;;) {
				 event = q_macro.pop();
#ifdef ONLY_MICROSPILL
				 continue;
#endif
				 int trig = event.TRIGGER;
				 int i = trig - 1; /* 0,1,2,3 ; 12,13*/
				 if(trig == 12) { /* BoS. Start counting again. */
					is_in_spill = true;
					ts_bos = event.VULOM_CLOCK;
					FOR(i,4) is_first_after_bos[i] = true;
					
					 /* Spin around until it's safe to print. */
					while(is_printing.exchange(true)) std::this_thread::sleep_for(milliseconds(1));
					println("--MACRO RESET");
					is_printing.store(false);
					
					/* Add the counted from ECL_IN(1) to offspill counter. */
					offspill_counted[0] += event.ECL_INCREMENT;

					/* Set the clock counter. */
					clock.assign(event.VULOM_CLOCK);
					continue;
				 }
				 else if(trig == 13) { /* EoS: don't fill main counter or count time. */
					is_in_spill = false;
					ts_eos = event.VULOM_CLOCK;

					/* Handle last remaining hits. */
					counted[0] += event.ECL_INCREMENT;
					clock.assign(ts_eos);
					ts_inc += clock.calc_increment();

					while(is_printing.exchange(true)) std::this_thread::sleep_for(milliseconds(1));

					printf("--MACRO ELAPSED_TIME:%.3f", ts_inc/1e8);
					FOR(i,4) printf(" INDEX:%d:COUNTED:%d", i+1, counted[i]);
					printf("\n");
					printf("--MACRO EOS SPILL_TIME:%.4f", Scaler<>::calc_diff(ts_eos, ts_bos)/1e8);
					FOR(j,4) printf(" INDEX:%d:OFFSPILL_COUNTED:%d", j+1, offspill_counted[j]);
					printf("\n");	
					
					is_printing.store(false);

					memset(counted, 0, sizeof(counted));
					ts_inc = 0;
					
					/* Start counting offspills from this point on. */
					memset(offspill_counted, 0, sizeof(offspill_counted));
					continue;
				 }
				 if(!is_in_spill) {
					 offspill_counted[i] += event.ECL_INCREMENT;
					 continue;
				 }
				 else {
					 if(is_first_after_bos[i]) {
						/* Check in the FIFO, roughly which hits got received after BoS. */
						uint32_t ts_trig = event.VULOM_CLOCK;
						int ahead = Scaler<>::calc_diff(ts_trig, ts_bos);
						for(int j=event.DELTA_T-1; j>=0; --j) { /* Don't judge me, friends. */
							ahead -= (int)event.DELTA_Tv[j];
							if(ahead > 0) counted[i]++;
							else offspill_counted[i]++;
						}
						is_first_after_bos[i] = false;
					 }
					 else {
						counted[i] += event.ECL_INCREMENT;
					 }
				 }
				/* event.TS_INCRMENT counts elapsed since last of a specific trigger type. 
				 * Instead, count time directly from any trigger. Based on `event.VULOM_CLOCK`. */
				 clock.assign(event.VULOM_CLOCK);
				 if(clock.is_in_init()) continue;

				 ts_inc += clock.calc_increment();

				 if(ts_inc > print_every) { 
					 while(is_printing.exchange(true)) std::this_thread::sleep_for(milliseconds(1));
					 printf("--MACRO ELAPSED_TIME:%.3f", ts_inc/1e8);
					 FOR(i,4) printf(" INDEX:%d:COUNTED:%d", i+1, counted[i]);
					 printf("\n");
					 is_printing.store(false);

					 memset(counted, 0, sizeof(counted));
					 ts_inc = 0;
				 }
			}
		}
	);

	t1.join();
	t2.join();
	t3.join();
	
	client.close();
	return 0;
}

bool IsCmdArg(const char* line, int argc, char** argv) {
	char *line1 = (char*)malloc(strlen(line)+3);
	strcpy(line1, "--");
	strcat(line1, line);
	for(int i(1); i<argc; ++i) {
		if(!strcmp(argv[i], line1)) {
			memset(argv[i], '_', strlen(argv[i]));
			free(line1); return 1;
		}
	}

	for(int i(1); i<argc; ++i) {
		if(argv[i][0] != '-') continue;
		if(!strcmp((char*)(argv[i]+1), line)) {
			memset(argv[i], '_', strlen(argv[i]));
			free(line1); return 1;
		}
	}

	free(line1); return 0;
}

/* Cmd args have to be: --tag0=identifier0 --tag1=identifier1 ... 
 * Kept tag0=ident also as an option. */
bool ParseCmdLine(const char* line, std::string& parsed, int argc, char** argv) {
	std::cmatch m;
	std::regex r("^(?:--)([^=]+)[=](.+)$");
	for(int i(1); i<argc; ++i) {
		if(std::regex_match(argv[i], m, r) && !strcmp(m[1].str().c_str(), line)) {
			parsed = m[2].str(); 
			
			// Set argv[i] to be something redundant.
			memset(argv[i], '_', strlen(argv[i]));
			return 1;
		}
	}

	/* Handle the `-tag value` case. */
	for(int i(1); i<argc-1; ++i) {
		if(argv[i][0] != '-') continue;

		if(!strcmp((char*)(argv[i]+1), line)) {
			parsed = std::string(argv[i+1]);

			// Set argv[i] and argv[i+1] to be something redundant.
			memset(argv[i], '_', strlen(argv[i]));
			memset(argv[i+1], '_', strlen(argv[i+1]));
			return 1;
		}
	}
	return 0;
}

// All args except mandatory one, must be deconstructed to '_' only args.
void verify_no_arguments_left(int argc, char** argv) {
	for(int i(2); i<argc-1; ++i) {
		for(int j=0; j<(int)strlen(argv[i]); ++j) {
			if(argv[i][j] != '_') {
				WARN("Argument left unparsed: %s\n", argv[i]);
				println("Exiting the program.\n");
				exit(1);
			}
		}
	}
}
