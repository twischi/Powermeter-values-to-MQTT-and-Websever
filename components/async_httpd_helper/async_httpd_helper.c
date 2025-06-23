/*-------------------------------
   INCLUDES
--------------------------------*/
#include "async_httpd_helper.h"   // for THIs (own component)
//#include "freertos/FreeRTOS.h"    // For FreeRTOS functions
//#include "freertos/queue.h"     // For FreeRTOS queue functions

/*-------------------------------------------------------- 
  CONFIGURATION <async_httpd_helper> 
*--------------------------------------------------------*/
/* !!! IN MENUCONFIG !!!:
#define CONFIG_MAX_ASYNC_HTTPD_REQUESTS         2       // Max number of async requests
#define CONFIG_ASYNC_WORKER_TASK_PRIORITY       5       // Priority of the worker tasks
#define CONFIG_ASYNC_WORKER_TASK_STACK_SIZE_KB  4096    // for esp32s3 doubled from 2048 to 4096
*/

/*---------------------------------------------------- 
  VARIABLES used in the componente X-function 
*----------------------------------------------------*/
static TaskHandle_t arrOf_aycnWorkerTaskHandles[CONFIG_MAX_ASYNC_HTTPD_REQUESTS]; // Array with handles to ech async worker task
static SemaphoreHandle_t handle_to_worker_ready_count;     // Handle to SEMAPHORE-Counter
static QueueHandle_t handle_to_async_req_queue;           // Handle to QUEUE with requests for async processing by worker tasks
/*---------------------------------------------------- 
  STRUCTs used in the componente X-function 
*----------------------------------------------------*/

typedef struct { // Structure definned data for ITEMS in the async request queue -> 'handle_to_async_req_queue'
    httpd_req_t* req;             // Pointer to a HTTPD-request to be processed
    httpd_req_handler_t handler;  // Pointer to the handler function to process the request
} httpd_async_req_t;

/*-------------------------------
  ESP Logging: TAG 
--------------------------------*/
#include <esp_log.h>                 // For ESP-logging
//                       "12345678"
static const char *TAG = "ASYN⚒︎WRK"; // TAG for logging

/*================================================================================
  sumit_req_to_async_workers_queue():  Submit an HTTP-Req to a QUEUE 
                                       for async processing by a worker threads.
  used by: Handle_WebServer_Logging_ServerSentEvents_GET()
=================================================================================*/
esp_err_t sumit_req_to_async_workers_queue(httpd_req_t *received_req, httpd_req_handler_t received_handler)
{   httpd_req_t* created_copy_of_req = NULL;
    esp_err_t err;
    //...............................................................................................................
    // (1) create a copy of the request that we own 
    //...............................................................................................................      
    //  START an asynchronous request:
    //  > This function can be called in a request handler to get a request copy that can be used on a async thread.
    //............................................................................................................... 
    err = httpd_req_async_handler_begin(    // Begin a async request handler:
                              received_req, // <r>   The request to create an async copy of
                      &created_copy_of_req);// <out> A newly allocated request which can be used on an async thread 
    // !! EXIT HERE !! if fails. NO further processing // NO NEED to free 'created_copy_of_req' as its a empty pointer 
    if (err != ESP_OK) { return err; } 
    //...............................................................................................................
    // (2) CHECK if aycn-Workers available?: IF NOT available then return an error and EXIT
    //...............................................................................................................
    int block_time_ticks = 0;   // Immediately respond, The time in ticks to wait for the semaphore to become available
    BaseType_t the_answer;      // Just for more readable code
    the_answer= xSemaphoreTake( // Check happens with THIS  
                handle_to_worker_ready_count,
                block_time_ticks /* xBlockTime*/);
    if (the_answer != pdTRUE) { // Means no free Worker       
        ESP_LOGW(TAG, "--  🚨 No 'more' async Worker-Task are available - regretted Request 🚨");
        httpd_req_async_handler_complete(created_copy_of_req); // Cleanup the request copy to have no memory leak
        return ESP_FAIL;} // Respond with an http-ERROR if no Worker-Task are available.
    else { // Means a Worker is available
        ESP_LOGD(TAG, "--  ✅ A async Worker-Task is available");} // Log that a worker is available
    //...............................................................................................................
    // (3) REACH to POINT to SEND(>>Hand over)the REQUEST TO the Worker-Tasks-QUEUE
    //...............................................................................................................
    // (a) Create the CONFIG for the async request
    httpd_async_req_t config_for_async_req = {
        .req = created_copy_of_req,   // The request to be processed as copy created with httpd_req_async_handler_begin()  
        .handler = received_handler,};// The handler function received from caller
    // (b) Try to send the async request to Worker-Tasks-QUEUE
    the_answer= xQueueSend( // Send the async request to the worker queue
                handle_to_async_req_queue,      // <xQueue> The handle to the Queue where the request is ADDED.
                &config_for_async_req,// <pvItemToQueue> A pointer to the DATA to be placed on the queue. Here a config_async_req struct-element.
                pdMS_TO_TICKS(100));  // Give max 100ms (max)time to be done.
    if (the_answer != pdTRUE) { // Means the queue is full
        ESP_LOGE(TAG, "!! ❌ async Workers-queue is FULL, cannot send request");
        httpd_req_async_handler_complete(created_copy_of_req); // Cleanup the request copy to have no memory leak
        return ESP_FAIL;} // Respond with an http-ERROR if the queue is full.
    //...............................................................................................................
    // RETURN POINT EVERTHINK WENT FINE
    //...............................................................................................................
    ESP_LOGD(TAG, "--  ✅ Async request submitted to worker queue for processing");
    return ESP_OK;
}

/*================================================================================
  is_on_async_worker_thread():     Is current handle already on a worker thread?
  used by: Handle_WebServer_Logging_ServerSentEvents_GET()
=================================================================================*/
bool is_on_async_worker_thread(void)
{   // is our handle one of the known async handles?
    TaskHandle_t current_task_handle = xTaskGetCurrentTaskHandle(); // Get the current task handle 
    //...........................................................................
    // For LOOP to SEARCH is current HANDLE is in the array with arrOf_aycnWorkerTaskHandles
    //...........................................................................
    for (int i = 0; i < CONFIG_MAX_ASYNC_HTTPD_REQUESTS; i++) { // Check if the handle is one of the known async handles         
      if (arrOf_aycnWorkerTaskHandles[i] == current_task_handle) {return true;}} // If found, return true                                                                 
    return false; // otherwise return false
}

/*================================================================================
  template_task_async_req_worker(): Template for all Async-HTTPD-Worker-Tasks 
        * Each worker task runs in its own thread using THIS Template.
        * Waits for a HTTPD-Request to be queued and then calls 
          the handler function provided in the request.           
  used by: start_async_req_workers()
=================================================================================*/
static void template_task_async_req_worker(void *p)
{   //............................................................................
    // (0) GET and report the current Worker-Task-Name with invoking 
    //............................................................................
    TaskHandle_t this_handle = xTaskGetCurrentTaskHandle(); // Get the current task handle
    const char* task_name = pcTaskGetName(this_handle);     // Set Pointer to the task name
    ESP_LOGI(TAG, "       🚀 Async Worker-Task '%s' invoked with endless loop.", task_name);
    /* ++++++++++++++++++++++++++++++  ENDLESS LOOP  ++++++++++++++++++++++++++++++ */
    while (true) {
        // Execute the Counting: Semaphore signals if worker is ready 
        xSemaphoreGive(handle_to_worker_ready_count); // counting semaphore - this signals that a worker
        //............................................................................
        // (2) CHECK for NEXT aync HTTPD-Request in QUEUE
        //............................................................................
        httpd_async_req_t received_async_req; // Declare a variable to hold the received async request
        BaseType_t is_NewItem;                // Just for more readable code
        is_NewItem = xQueueReceive(    //  CHECK if  RECEIVE an aync HTTPD-Request 
                      handle_to_async_req_queue, // <xQueue> The handle to the Queue from which ITEM go be received.
                      &received_async_req,       // <pvBuffer> POINTER to ITEM received (Will be copied).
                      portMAX_DELAY);            // <xTicksToWait> The maximum amount of time the task should block waiting for an item to receive.
        //............................................................................
        // (3) PROCESS if aync HTTPD-Request is received
        //............................................................................                      
        if (is_NewItem == pdTRUE) { // Only if a new item was received 
            ESP_LOGD(TAG, "    🚀 Proccessing uri '%s' with async-Worker-Task '%s'", received_async_req.req->uri,task_name);
            // Use the received-struct to process the request
            received_async_req.handler(         // CALL HANDLER of received ITEM 'received_async_req'
                      received_async_req.req);  // Hand over the REQUEST (stored in the queue) 
            // Inform the server that it can purge the socket used for this request, if needed.
            if (httpd_req_async_handler_complete(received_async_req.req) != ESP_OK) {
                ESP_LOGE(TAG, " ❌ Failed to complete a HTTPD-Request in Async-Worker-Task '%s'", task_name);
            }
        }
    } 
    /* ++++++++++++++++++++++++++++++  ENDLESS LOOP  ++++++++++++++++++++++++++++++ */  
    ESP_LOGW(TAG, "--     * Stopp Tasks of workers for async requests");
    vTaskDelete(NULL);
}

/*================================================================================
  start_async_req_workers(): 
                    This function creates the worker tasks and the queue.
                    It also creates a counting semaphore to track the number
                    of available workers.                     
  used by: start_logwebserver()
=================================================================================*/
void start_async_req_workers(void)
{           ESP_LOGI(TAG, "--     * Create async Worker Tasks for async requests.");
    //............................................................................
    // (1) CREATE a COUNTING-SEMAPHORE INSTANCE & returns a handle to be used
    //............................................................................
    handle_to_worker_ready_count =
        xSemaphoreCreateCounting( CONFIG_MAX_ASYNC_HTTPD_REQUESTS, // <uxMaxCount> The maximum count value that can be reached. 
                                  0);                              // <uxInitialCount> The count value assigned to the semaphore when it is created.
    // Check if the semaphore-COUNTER was created successfully                              
    if (handle_to_worker_ready_count == NULL) {  // EXIT if fails
            ESP_LOGE(TAG, "--  ❌ Failed to Semahore-COUNTER for Async-Worker-Tasks"); return;  }
    //............................................................................
    // (2) CREATE a QUEUE to hold async requests
    //............................................................................
    handle_to_async_req_queue = // out= handle to the queue
            xQueueCreate( CONFIG_MAX_ASYNC_HTTPD_REQUESTS,  // <uxQueueLength> The maximum number of items that the queue can hold.
                          sizeof(httpd_async_req_t));       // <xItemSize> The size of each item in the queue.
    if (handle_to_async_req_queue == NULL){  // EXIT if fails
            ESP_LOGE(TAG, "--  ❌ Failed to create handle_to_async_req_queue"); vSemaphoreDelete(handle_to_worker_ready_count); return; }
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
    // ONLY REACHED if both above are OK
    //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //...........................................................................................
    // (3) CREATE serveral WORKER-TASKs to and store the in ARRAY: arrOf_aycnWorkerTaskHandles
    //...........................................................................................
    for (int i = 0; i < CONFIG_MAX_ASYNC_HTTPD_REQUESTS; i++) { // Loop to the number desired task
      // Try to create a NEW Task and add it to the ARRAY-list with tasks ready to run.
      bool success;
      // Create a individual task name for each worker task
      char task_name[25]; snprintf(task_name, sizeof(task_name), "async_wrkr_%d", i);
      success = xTaskCreate(
                      template_task_async_req_worker,// <pxTaskCode>   Pointer to the task entry function (with never return-loop)
                               task_name,            // <pcName>       Descriptive task name (for debugging)
        CONFIG_ASYNC_WORKER_TASK_STACK_SIZE_KB*1024, // <usStackDepth> The size of the task stack for task
                               /*NULL*/ (void *)0 ,  // <pvParameters> Pointer to Parameters for the task
                  CONFIG_ASYNC_WORKER_TASK_PRIORITY, // <uxPriority>  The priority of task
                   &arrOf_aycnWorkerTaskHandles[i]); // <pxCreatedTask> Pointer to the task handle to be created
      if (!success) {
          ESP_LOGE(TAG, "--     ❌ Failed to create Async-Worker-Task #%d of #%d", i+1,CONFIG_MAX_ASYNC_HTTPD_REQUESTS);
          continue;
      } else { 
          ESP_LOGI(TAG, "--     ✅ Async-Worker-Task #%d of #%d created ('%s')", i+1,CONFIG_MAX_ASYNC_HTTPD_REQUESTS,task_name);
      }  
    }
}