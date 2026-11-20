#pragma once
#include "Arduino.h"
#include <cstddef>
#include <cstring>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_TIMEOUT=0x107, ESP_ERR_INVALID_STATE=0x103;
using TickType_t=uint32_t;
#define pdMS_TO_TICKS(t) (t)
constexpr int SPI2_HOST=1, SPI_DMA_CH_AUTO=3;
struct spi_bus_config_t {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num;int data4_io_num,data5_io_num,data6_io_num,data7_io_num;int max_transfer_sz;};
struct spi_device_interface_config_t {int clock_speed_hz,mode,spics_io_num,queue_size;};
struct spi_transaction_t {size_t length=0;const void* tx_buffer=nullptr;};
using spi_device_handle_t=void*;
inline int transferError=0;
inline bool completionTimeout=false;
inline bool queued=false;
inline spi_transaction_t* pending=nullptr;
inline std::vector<std::pair<int,std::vector<uint8_t>>> sent;
inline esp_err_t spi_bus_initialize(int,const spi_bus_config_t*,int) {return ESP_OK;}
inline esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t* p) {*p=reinterpret_cast<void*>(1);return ESP_OK;}
inline esp_err_t spi_bus_remove_device(spi_device_handle_t) {return ESP_OK;}
inline esp_err_t spi_bus_free(int) {return ESP_OK;}
inline esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t* t,TickType_t) {
 if(transferError) return transferError;
 queued=true;pending=t;
 auto b=static_cast<const uint8_t*>(t->tx_buffer);
 sent.push_back({fakePins[16],std::vector<uint8_t>(b,b+t->length/8)});
 if(fakePins[16]==0 && b[0]==0x20 && busyEnabled) busyUntil=fakeTime+25;
 return ESP_OK;
}
inline esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t** out,TickType_t) {
 if(!queued || completionTimeout)return ESP_ERR_TIMEOUT;
 *out=pending;queued=false;return ESP_OK;
}
