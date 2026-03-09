#include <cstdint>
#include "core/app_state.h"
extern AppState g_app_state;
extern const char* o_exception;
extern int init_stage;
extern uint64_t fblk_size;
extern char str1[2688];
extern char *temp;
extern int isKickMode;
extern bool isUseCptable;
extern const char* Version;
extern int fdl1_loaded;
extern int fdl2_executed;
extern char mode_str[256];
extern int in_quote;
extern char **str2;
extern int no_fdl_mode;
extern int& m_bOpened;
int main_console(int argc, char** argv);
