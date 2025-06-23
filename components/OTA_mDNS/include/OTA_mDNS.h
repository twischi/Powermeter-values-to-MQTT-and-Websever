#include <stdbool.h>

void start_ota_task(void);
bool is_ota_update_in_progress(void);
const char *get_ota_url(void);