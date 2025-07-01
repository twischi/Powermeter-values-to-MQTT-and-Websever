/*===========================================================================================
 * @file        worker_modbus_rtu.c
 * @author      Thomas Wisniewski
 * @date        2025-05-01
 * @brief       Component to establish a Modbus RTU connection 
 *
 * @menuconfig  YES
 * (includes)   YES 
 * 
 * Startpoint was example from Espressif: 
 *    https://github.com/espressif/esp-modbus/tree/v2.0.2/examples/serial/mb_serial_master
 * 
 * How does this file work?
 *    ...
 * 
========================================================================================================*/
/*----------
   INCLUDES
-----------*/
#include "Modbus_UART_RTU.h"  // For THIS component
#include <stdio.h>            // For printf
#include "driver/uart.h"      // UART driver needed for Serial MODBUS
#include "esp_err.h"          // For ESP error codes
/*------------------
  ESP Logging: TAG 
------------------*/
#include <esp_log.h>                 // For ESP-logging
static const char *TAG = "UART-MoB"; // TAG for logging
/*                        12345678 */
/*----------------------------
   VARIABLES: Whole Component 
------------------------------*/
/*static*/ void *MB_master_handle = NULL;                   // Modbus handle for all Modbus communications
mb_parameter_descriptor_t *MB_param_descriptors = NULL; // Pointer to the parameter descriptor table
size_t MB_num_descriptors = 0;                          // Number of descriptors in the table
/*----------------------------
   Constants via #define 
------------------------------*/
#define UART_BUF_SIZE                   (1024)
#define PRINT_TEST_TO_UART_CYLES        (10) // Number of cycles to print to UART
// Timeout to update cid over Modbus
#define UPDATE_CIDS_TIMEOUT_MS          (500)
#define UPDATE_CIDS_TIMEOUT_TICS        (UPDATE_CIDS_TIMEOUT_MS / portTICK_PERIOD_MS)
// Timeout between polls
#define POLL_TIMEOUT_MS                 (1)
#define POLL_TIMEOUT_TICS               (POLL_TIMEOUT_MS / portTICK_PERIOD_MS)
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  Example Data (Object) Dictionary for Modbus parameters: 
  * The CID field:            in the table must be UNIQUE.
  * Modbus Slave Addr field:  Defines slave address of the device with correspond parameter.
  * Modbus Reg Type:          Type of Modbus register area (Holding register, Input Register and such).
  * Reg Start field:          Defines the start Modbus register number and 
                AND           Reg Size defines the number of registers for the characteristic accordingly.
  * The Instance Offset:      Defines POINTER-Offset in the appropriate Parameter structure,
                              that will be used as instance to save parameter value.
  * Data Type, Data Size:     Specify type of the characteristic and its data size.
  * Parameter Options field:  Specifies the options that can be used to process parameter value (limits or masks).
  * Access Mode:              Can be used to implement custom options for processing of characteristic 
                              (Read/Write restrictions, factory mode values and etc).
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
#define MB_DEVICE_ADDR1  1 // Modbus Slave address of the device
input_reg_params_t input_reg_params = { 0 };
/*--------------------------------------------------------------------------------------------------
  Init of Modubus Controller for Serial RTU
    source: https://docs.espressif.com/projects/esp-modbus/en/stable/esp32/port_initialization.html
----------------------------------------------------------------------------------------------------*/
static esp_err_t Init_MB_Controller_SerialRTU(void)
{ esp_err_t err= ESP_OK; // Set default error code
  // --------------------------------------------------------
  //  SETUP UART-Serial without RTS needed MODBUS Serial-Mode 
  // --------------------------------------------------------
  ESP_LOGI(TAG, "--  Initialize UART Serial.... '");
  ESP_LOGI(TAG, "--    Uses this UART Pin numbers...");
  // Set UART pins: TX and RX only
  ESP_LOGI(TAG, "--    * TXD         GPIO: %d", CONFIG_MY_MB_UART_TXD);
  ESP_LOGI(TAG, "--    * RXD         GPIO: %d", CONFIG_MY_MB_UART_RXD);
  err = uart_set_pin(CONFIG_MY_MB_UART_PORT_NUM, CONFIG_MY_MB_UART_TXD, CONFIG_MY_MB_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  ESP_ERROR_CHECK(err); // Abort if error
  ESP_LOGI(TAG, "--    UART set pin result: %s", esp_err_to_name(err));
  // --------------------------------------------------------
  //  INITIALIZE MODBUS CONTROLLER 
  // --------------------------------------------------------
  ESP_LOGI(TAG, "--  Initialize Modbus CONTROLLER...'");
  // Create and fill structure for Controller >> esp_modbus_common.h
  mb_communication_info_t mbc_config = {
    .ser_opts.port = CONFIG_MY_MB_UART_PORT_NUM,      // Use this UART Communication port number
    .ser_opts.mode = MB_RTU,                          // Use this MODE of Communication (MB_RTU, MB_ASCII)
    .ser_opts.baudrate = CONFIG_MY_MB_UART_BAUD_RATE, // Use thse BAUD RATE for communication
    .ser_opts.parity = UART_PARITY_DISABLE,           // parity option for the port
    .ser_opts.uid = 0,                                // Modbus Comm. SLAVE-ID >> Unused for Master-Mode
    .ser_opts.response_tout_ms =
            CONFIG_MY_MB_REGISTER_REPONSE_TIMEOUT,    // slave response time-out 
    .ser_opts.data_bits = UART_DATA_8_BITS,           // number of data bits for communication port
    .ser_opts.stop_bits = UART_STOP_BITS_1,           // number of stop bits for the communication port
  };
  // Convert for log: Communication mode
  const char *commMode;
  if        (mbc_config.ser_opts.mode == MB_ASCII) { commMode = "ASCII";
  } else if (mbc_config.ser_opts.mode == MB_RTU)   { commMode = "RTU";}
                                              else { commMode = "Unknown"; }
  // Convert for log: Parity
  char *parity;
  switch (mbc_config.ser_opts.parity) {
      case UART_PARITY_DISABLE: parity = "DISABLED"; break;
      case UART_PARITY_EVEN: parity =    "Even"; break;
      case UART_PARITY_ODD: parity =     "Odd"; break;
      default: parity =                  "None"; break;
  }
  // Convert for log: Number of DATA-bits
  int data_bits; 
  switch (mbc_config.ser_opts.data_bits) {
      case UART_DATA_5_BITS: data_bits = 5; break;
      case UART_DATA_6_BITS: data_bits = 6; break;
      case UART_DATA_7_BITS: data_bits = 7; break;
      case UART_DATA_8_BITS: data_bits = 8; break;
      default: data_bits = 8; break;
  }
  // Convert for log: Number of STOP-bits
  char *stop_bits;
  switch (mbc_config.ser_opts.stop_bits) {
    case UART_STOP_BITS_1: stop_bits   = "1"; break;
    case UART_STOP_BITS_1_5: stop_bits = "1.5"; break;
    case UART_STOP_BITS_2: stop_bits   = "2"; break;
    default: stop_bits                 = "1"; break;
  }
  // Convert for log: Number of RESONSE-timeout 
  int response_timeout = (int)mbc_config.ser_opts.response_tout_ms;
  // Show the parameters
  ESP_LOGI(TAG, "--  Use this Controller parameters:");
  ESP_LOGI(TAG, "--    * Port:         UART%d", mbc_config.ser_opts.port);
  ESP_LOGI(TAG, "--    * Comm. Mode:       %s", commMode);
  ESP_LOGI(TAG, "--    * Baudrate:         %d", CONFIG_MY_MB_UART_BAUD_RATE);
  ESP_LOGI(TAG, "--    * Data bits:        %d", data_bits);
  ESP_LOGI(TAG, "--    * Stop bits:        %s", stop_bits);
  ESP_LOGI(TAG, "--    * Parity:           %s", parity);
  ESP_LOGI(TAG, "--    * Response timeout: %d ms", response_timeout);
  // Do the initialization
  err = mbc_master_create_serial(&mbc_config, &MB_master_handle);
  ESP_ERROR_CHECK(err); // Abort if error  
      MB_RETURN_ON_FALSE((MB_master_handle != NULL), ESP_ERR_INVALID_STATE, TAG, "-- MB-Handler is NULL! > Controller initialization fail!");
      MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG,            "-- MB-Controller initialization fail: Returns(0x%x).", (int)err);
  return err;
}


/*--------------------------------------------------------
  Read_Registers_Once:
  Request to read Modbus parameters from slave device
---------------------------------------------------------*/ 
#if CONFIG_MY_MB_READ_REGS_ONCE_WHEN_DESC_SET // menu config option
static void Read_Registers_Once() { // Read_Registers_Once 
  // Set StartPoint for variable and constant
  esp_err_t err = ESP_OK;
  bool errorRead_DataSet = false; // Error-Flag, when at least one Register fails
  const mb_parameter_descriptor_t *param_descriptor = NULL;
  ESP_LOGI(TAG, "####################################################################################");
  ESP_LOGI(TAG, "##  Read and Print all Modbus-Registers once ..."); // Intro message
  for (uint16_t cid = 0; cid < MB_num_descriptors; cid++) {
      //...........................
      // Get parameter descriptor
      //...........................
      err = mbc_master_get_cid_info(MB_master_handle, cid, &param_descriptor);
      if (err == ESP_ERR_NOT_FOUND || param_descriptor == NULL) 
          { continue; }
      // When it is the first cid, then log the received parameter descriptor
      if (cid == 0 ) {
          ESP_LOGI(TAG, "##  CID=%d > Register=0x%04X > Register-Name='%s' : Unit='%s'", cid,(uint16_t) param_descriptor->mb_reg_start, (char *)param_descriptor->param_key, (char *)param_descriptor->param_units);
          ESP_LOGI(TAG, "##  - Slave-Address=%u", param_descriptor->mb_slave_addr);   
          ESP_LOGI(TAG, "##  - Register-Size=%d", (uint16_t) param_descriptor->mb_size);
          ESP_LOGI(TAG, "##  - Parameter-Offset=%" PRIu32, (uint32_t) param_descriptor->param_offset);
          ESP_LOGI(TAG, "##  - Parameter-Type=0x%02X (%d)", param_descriptor->param_type,param_descriptor->param_type);
          ESP_LOGI(TAG, "##  - Parameter-Size=0x%02X (%d)", param_descriptor->param_size,param_descriptor->param_size);
      }
      //........................................
      // Get Pointer to the Parameter Date-Set
      //........................................
      // Check if (cid it not 0) then offset of = 0 is not allowed
      if (param_descriptor->param_offset == 0 && cid > 0) {              
          ESP_LOGE(TAG, "!!   Wrong parameter offset for CID #%u  !!", (unsigned)param_descriptor->cid); 
          continue;                                                               // !!!! SKIP this parameter
      }
      // OK, fine :-) to ahead 
      void *temp_data_ptr = NULL;                                                 // Init a empty pointer
      temp_data_ptr = (void *)&input_reg_params + param_descriptor->param_offset; // Get Pointer to in scope Parameter Data-Set
      assert(temp_data_ptr);                                                      // Abbort if pointer is NULL 
      // Read parameter value
      uint8_t type = 0;
      err = mbc_master_get_parameter(MB_master_handle, cid, (uint8_t *)temp_data_ptr, &type);
      if (err != ESP_OK) {
          ESP_LOGE(TAG, "!!   CID=%2d , (%s) read fail, err = 0x%x (%s).", (int)param_descriptor->cid, (char *)param_descriptor->param_key, (int)err, (char *)esp_err_to_name(err));
          break; // Skip the rest of the loop
      } else {    
          // Log parameter value
          float value = *(float *)temp_data_ptr;
          ESP_LOGI(TAG, "##  - CID=%2d : %s (%.15s) value = %f (0x%" PRIx32 ")", (int)param_descriptor->cid, (char *)param_descriptor->param_key, (char *)param_descriptor->param_units, value, *(uint32_t *)temp_data_ptr);
      };
    vTaskDelay(POLL_TIMEOUT_TICS); // Timeout between polls
  }
  vTaskDelay(UPDATE_CIDS_TIMEOUT_TICS); // Timeout to update CIDs
  ESP_LOGI(TAG, "##  Read all Modbus-Registers DONE,"); // Intro message
  ESP_LOGI(TAG, "####################################################################################");
  // ESP_LOGI(TAG, "!!  Destroying Modus-Master-Controller...");
  // ESP_ERROR_CHECK(mbc_master_delete(MB_master_handle));
}
#endif

/*--------------------------------------------------------------------------------------------------
  Write_bytes_to_UART: Test Adapter to RS485 connected to UART
  Does the TX-Blink on the RS485-Adapter?
----------------------------------------------------------------------------------------------------*/
void Write_bytes_to_UART(void) {
    // Test output
    for (int i = 0; i < PRINT_TEST_TO_UART_CYLES; i++) {
      const char *test_str = "Hello UART Port!\r\n";
      uart_write_bytes(CONFIG_MY_MB_UART_PORT_NUM, test_str, strlen(test_str));
      ESP_LOGI(TAG, "~~~ Send Dummy-Message (%d of %d) to UART, Wait 1000ms before sending the next message...",i+1, PRINT_TEST_TO_UART_CYLES);
      vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait for 1 second before sending the next message
    }
}

/*--------------------------------------------------------------------------------------------------
  Test_UART_withConfig: Test Adapter to RS485 connected to UART
  Does the TX-Blink on the RS485-Adapter?
----------------------------------------------------------------------------------------------------*/
void Test_UART_withConfig(void)
{   // UART configuration
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_MY_MB_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  //      .source_clk = UART_SCLK_APB,
    };
    // Configure UART parameters
    ESP_LOGI(TAG, "~~~ !!! Initializing UART...");
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_MY_MB_UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_MY_MB_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CONFIG_MY_MB_UART_PORT_NUM, CONFIG_MY_MB_UART_TXD, CONFIG_MY_MB_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // Test output
    Write_bytes_to_UART();
    // Deinitialize UART (kill the UART port)
    ESP_LOGI(TAG, "~~~ !!! KILLED UART used for test.");
    ESP_ERROR_CHECK(uart_driver_delete(CONFIG_MY_MB_UART_PORT_NUM)); // Uninstall the UART driver
}

/*------------------------------------------------------------
  Public FUNCTION of this component: Start_Modbus_RTU_Workers
-------------------------------------------------------------*/
esp_err_t Start_Modbus_RTU_Workers(void **mb_handle_out, mb_parameter_descriptor_t *mb_descriptors_in, size_t num_device_parameters_in)
{ esp_err_t err= ESP_OK; // Set default error code
  /*------------------------------------------------------------------------
    Set Log-Levels for this component
  ------------------------------------------------------------------------*/  
  esp_log_level_set("uart",           ESP_LOG_WARN); // LogLevel WARN for the UART driver
  esp_log_level_set("mb_port.serial", ESP_LOG_WARN); // LogLevel WARN for the Modbus serial port
  esp_log_level_set("MB_CONTROLLER_MASTER", ESP_LOG_NONE);  // Log-Level for Modbus-Controller -- NEVER neded as errors grabed otherwise
  esp_log_level_set(TAG, CONFIG_MY_MB_LOG_LEVEL);             //  THSI: Log-Level
  // GO
  ESP_LOGI(TAG, "------------------------------------------------------------------------------------");
  ESP_LOGI(TAG, "--  Start Serial Modbus, myComponent: 'worker_modbus_rtu'");
  ESP_LOGI(TAG, "------------------------------------------------------------------------------------");
  // Check if given parameters are OK
  MB_RETURN_ON_FALSE((mb_descriptors_in != NULL), ESP_ERR_INVALID_STATE, TAG,    "-- MB-Descriptors is NULL! > Controller initialization fail!");
  MB_RETURN_ON_FALSE((num_device_parameters_in > 0), ESP_ERR_INVALID_STATE, TAG, "-- MB-Descriptors is empty! > Controller initialization fail!");
  MB_param_descriptors = mb_descriptors_in;      // Set the pointer to the parameter descriptor table
  MB_num_descriptors = num_device_parameters_in; // Set the number of descriptors in the table
//  Test_UART_withConfig(); // un-comment for testing UART with configuration
  /*-----------------------------
    Initialize Modbus controller
   *-----------------------------
    1. uart_set_pin
    2. mbc_master_create_serial
  *------------------------------*/
  err = Init_MB_Controller_SerialRTU(); // Initialize Modbus controller
//  Write_bytes_to_UART(); // un-comment for check if UART RS485 Interface TX LED is blinking? 
  /*-----------------------------
    3. mbc_master_set_descriptor
  *------------------------------*/
  ESP_LOGI(TAG, "--  Set Descriptors to MB-Controller:");
  ESP_LOGI(TAG, "--    * How many?:     %d CIDs", MB_num_descriptors);
  err= mbc_master_set_descriptor(MB_master_handle, &MB_param_descriptors[0], MB_num_descriptors ); // Use the Descriptors received from the caller of this function/component
  ESP_ERROR_CHECK(err); // Abort if error
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "--    * Set Descriptor failed: %d", err);
    return err;
  } else {  
    ESP_LOGI(TAG, "--    * Set Descriptor accepted.");
    ESP_LOGI(TAG, "--    * MB_master_handle: %p", MB_master_handle);
  } 
  /*-----------------------------
    4. mbc_master_start
  *------------------------------*/
  err = mbc_master_start(MB_master_handle);
  ESP_ERROR_CHECK(err); // Abort if error
  // Hand over the handle to the caller
  if (mb_handle_out != NULL) { *mb_handle_out = MB_master_handle;} // Set the handle to the caller
  vTaskDelay(50);
  MB_RETURN_ON_FALSE((err == ESP_OK), ESP_ERR_INVALID_STATE, TAG, "!!  MB Cntroller start fail, returned (0x%x).", (int)err);
  ESP_LOGI(TAG, "-- Modbus Master stack initialized, ready to be used...");
  /*-----------------------------
    5. Read_Registers_Once (OWN)
  *------------------------------*/ 
  #if CONFIG_MY_MB_READ_REGS_ONCE_WHEN_DESC_SET // menu config option
    // Read all Modbus registers once. The one defined in the descriptor table 'mb_descriptors_in'
    Read_Registers_Once(); // Read all Modbus registers once. The one defined in the descriptor table 'mb_descriptors_in'
  #endif

  // End
  ESP_LOGI(TAG, "------------------------------------------------------------------------------------");
  // END
  return err;
};


