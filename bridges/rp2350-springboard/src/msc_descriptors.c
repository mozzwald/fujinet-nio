/* Laboratory composite identity, configurable for an allocated production ID.
 * Distinct from Pico SDK CDC and BOOTSEL; physical unique ID remains stable. */
#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>
#ifndef USBD_VID
#define USBD_VID 0xcafe
#endif
#ifndef USBD_PID
#define USBD_PID 0x4013
#endif
static const tusb_desc_device_t device={
 .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
 .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
 .bMaxPacketSize0=64,.idVendor=USBD_VID,.idProduct=USBD_PID,.bcdDevice=0x0100,
 .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
};
#define TOTAL (TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN+TUD_MSC_DESC_LEN)
static const uint8_t config[]={
 TUD_CONFIG_DESCRIPTOR(1,3,0,TOTAL,0,100),
 TUD_CDC_DESCRIPTOR(0,4,0x81,8,0x02,0x82,64),
 TUD_MSC_DESCRIPTOR(2,5,0x03,0x83,64)
};
const uint8_t *tud_descriptor_device_cb(void){return (const uint8_t *)&device;}
const uint8_t *tud_descriptor_configuration_cb(uint8_t index){return index?NULL:config;}
const uint16_t *tud_descriptor_string_cb(uint8_t index,uint16_t language){
 (void)language;static uint16_t result[40];char serial[2*PICO_UNIQUE_BOARD_ID_SIZE_BYTES+1];
 const char *s[]={"","FujiNet","FujiNet ROM Drive v1",serial,"ROM diagnostics","Springboard ROM staging"};
 if(index>=sizeof s/sizeof s[0])return NULL;
 if(!index){result[0]=0x0304;result[1]=0x0409;return result;}
 if(index==3)pico_get_unique_board_id_string(serial,sizeof serial);
 size_t n=strlen(s[index]);if(n>39)n=39;
 for(unsigned i=0;i<n;i++)result[i+1]=(uint8_t)s[index][i];
 result[0]=(uint16_t)(0x0300|((n+1)*2));return result;
}
