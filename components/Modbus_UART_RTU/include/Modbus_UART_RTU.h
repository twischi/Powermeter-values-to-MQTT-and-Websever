/*----------
   INCLUDES
------------*/
#include "esp_modbus_common.h"
#include "esp_modbus_master.h"
/*----------
   STRUCTURES
------------*/
#pragma pack(push, 1)
typedef struct
{
    float input_data0; // 0
    float input_data1; // 2
    float input_data2; // 4
    float input_data3; // 6
    uint16_t data[150]; // 8 + 150 = 158
    float input_data4; // 158
    float input_data5;
    float input_data6;
    float input_data7;
    uint16_t data_block1[150];
} input_reg_params_t;
#pragma pack(pop)
/*------------------
  Define FUNCTIONS
-------------------*/
esp_err_t Start_Modbus_RTU_Workers(void **mb_handle_out, mb_parameter_descriptor_t *mb_descriptors_in, size_t num_descriptors_in);