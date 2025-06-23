/*-------------------------------
   INCLUDES
--------------------------------*/
#include <esp_http_server.h>      // For HTTP server functions

/*-------------------------------
   STRUCTs
--------------------------------*/
typedef esp_err_t (*httpd_req_handler_t)(httpd_req_t *req);

/*-------------------------------
   PUBLIC / EXPOSE FUNCTIONS
--------------------------------*/
void start_async_req_workers(void);
bool is_on_async_worker_thread(void);
esp_err_t sumit_req_to_async_workers_queue(httpd_req_t *received_req, httpd_req_handler_t received_handler);