#pragma once

#define println(...) \
	do { \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while(0)

// Regular text.
#define KBLK "\e[0;30m"
#define KRED "\e[0;31m"
#define KGRN "\e[0;32m"
#define KYEL "\e[0;33m"
#define KBLU "\e[0;34m"
#define KMAG "\e[0;35m"
#define KCYN "\e[0;36m"
#define KWHT "\e[0;37m"

// Regular bold text.
#define KB_BLK "\e[1;30m"
#define KB_RED "\e[1;31m"
#define KB_GRN "\e[1;32m"
#define KB_YEL "\e[1;33m"
#define KB_BLU "\e[1;34m"
#define KB_MAG "\e[1;35m"
#define KB_CYN "\e[1;36m"
#define KB_WHT "\e[1;37m"

// Regular underline text.
#define KU_BLK "\e[4;30m"
#define KU_RED "\e[4;31m"
#define KU_GRN "\e[4;32m"
#define KU_YEL "\e[4;33m"
#define KU_BLU "\e[4;34m"
#define KU_MAG "\e[4;35m"
#define KU_CYN "\e[4;36m"
#define KU_WHT "\e[4;37m"

// High intensty text.
#define KH_BLK "\e[0;90m"
#define KH_RED "\e[0;91m"
#define KH_GRN "\e[0;92m"
#define KH_YEL "\e[0;93m"
#define KH_BLU "\e[0;94m"
#define KH_MAG "\e[0;95m"
#define KH_CYN "\e[0;96m"
#define KH_WHT "\e[0;97m"

// Bold high intensity text.
#define KBH_BLK "\e[1;90m"
#define KBH_RED "\e[1;91m"
#define KBH_GRN "\e[1;92m"
#define KBH_YEL "\e[1;93m"
#define KBH_BLU "\e[1;94m"
#define KBH_MAG "\e[1;95m"
#define KBH_CYN "\e[1;96m"
#define KBH_WHT "\e[1;97m"

// Reset.
#define KRNM "\e[0m"
#define KNRM "\e[0m"
#define COLOR_RESET "\e[0m"

// Bold optional.
#define BOLD "\e[1m"

bool parse_colour(const char* query, char* out) {
	if(strlen(query) > 9) return 0;
	char str[10] = {'\0'};
	str[0] = 'K';
	strcpy(&str[1], query);
	for(int i=1; i < (int)strlen(str); ++i) str[i] = std::toupper(str[i]);
	
#define TRY(COL) \
	if(!strcmp(str, #COL)) { \
		strcpy(out, COL); \
		return 1; \
	}

	TRY(KBLK)
	TRY(KRED)
	TRY(KGRN)
	TRY(KYEL)
	TRY(KBLU)
	TRY(KMAG)
	TRY(KCYN)
	TRY(KWHT)

	TRY(KB_BLK)
	TRY(KB_RED)
	TRY(KB_GRN)
	TRY(KB_YEL)
	TRY(KB_BLU)
	TRY(KB_MAG)
	TRY(KB_CYN)
	TRY(KB_WHT)

	TRY(KU_BLK)
	TRY(KU_RED)
	TRY(KU_GRN)
	TRY(KU_YEL)
	TRY(KU_BLU)
	TRY(KU_MAG)
	TRY(KU_CYN)
	TRY(KU_WHT)

	TRY(KH_BLK)
	TRY(KH_RED)
	TRY(KH_GRN)
	TRY(KH_YEL)
	TRY(KH_BLU)
	TRY(KH_MAG)
	TRY(KH_CYN)
	TRY(KH_WHT)

	TRY(KBH_BLK)
	TRY(KBH_RED)
	TRY(KBH_GRN)
	TRY(KBH_YEL)
	TRY(KBH_BLU)
	TRY(KBH_MAG)
	TRY(KBH_CYN)
	TRY(KBH_WHT)

	return 0;
	#undef TRY
}

#define PRINT_SAMPLE(COL) \
	do { \
		char code[10]; \
		strcpy(code, #COL); \
		println("%sCOLOR%s  : %6s", COL, KNRM, &code[1]); \
	} while(0);

void sample_colour_text() {
	println("SAMPLE : CODE");
	println("---------------");

	PRINT_SAMPLE(KBLK)
	PRINT_SAMPLE(KRED)
	PRINT_SAMPLE(KGRN)
	PRINT_SAMPLE(KYEL)
	PRINT_SAMPLE(KBLU)
	PRINT_SAMPLE(KMAG)
	PRINT_SAMPLE(KCYN)
	PRINT_SAMPLE(KWHT)

	PRINT_SAMPLE(KB_BLK)
	PRINT_SAMPLE(KB_RED)
	PRINT_SAMPLE(KB_GRN)
	PRINT_SAMPLE(KB_YEL)
	PRINT_SAMPLE(KB_BLU)
	PRINT_SAMPLE(KB_MAG)
	PRINT_SAMPLE(KB_CYN)
	PRINT_SAMPLE(KB_WHT)

	PRINT_SAMPLE(KU_BLK)
	PRINT_SAMPLE(KU_RED)
	PRINT_SAMPLE(KU_GRN)
	PRINT_SAMPLE(KU_YEL)
	PRINT_SAMPLE(KU_BLU)
	PRINT_SAMPLE(KU_MAG)
	PRINT_SAMPLE(KU_CYN)
	PRINT_SAMPLE(KU_WHT)

	PRINT_SAMPLE(KH_BLK)
	PRINT_SAMPLE(KH_RED)
	PRINT_SAMPLE(KH_GRN)
	PRINT_SAMPLE(KH_YEL)
	PRINT_SAMPLE(KH_BLU)
	PRINT_SAMPLE(KH_MAG)
	PRINT_SAMPLE(KH_CYN)
	PRINT_SAMPLE(KH_WHT)

	PRINT_SAMPLE(KBH_BLK)
	PRINT_SAMPLE(KBH_RED)
	PRINT_SAMPLE(KBH_GRN)
	PRINT_SAMPLE(KBH_YEL)
	PRINT_SAMPLE(KBH_BLU)
	PRINT_SAMPLE(KBH_MAG)
	PRINT_SAMPLE(KBH_CYN)
	PRINT_SAMPLE(KBH_WHT)
}
