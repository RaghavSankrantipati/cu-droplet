#ifndef IR_sensor_h
#define IR_sensor_h

#include <avr/io.h>
#include "scheduler.h"
#include "delay_x.h"
#include "i2c.h"
#define IR_SENSOR_PORT PORTB

#define IR_SENSOR_0_PIN_bm		PIN5_bm
#define IR_SENSOR_1_PIN_bm		PIN6_bm
#define IR_SENSOR_2_PIN_bm		PIN7_bm
#define IR_SENSOR_3_PIN_bm		PIN4_bm
#define IR_SENSOR_4_PIN_bm		PIN2_bm
#define IR_SENSOR_5_PIN_bm		PIN3_bm
#define ALL_IR_SENSOR_PINS_bm (PIN2_bm | PIN3_bm | PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm)

#define ALL_EMITTERS_CARWAV_bm (PIN0_bm | PIN1_bm | PIN4_bm | PIN5_bm | PIN7_bm | PIN6_bm)

#define MUX_IR_SENSOR_0		ADC_CH_MUXPOS_PIN5_gc		// IR0 sensor on PB5
#define MUX_IR_SENSOR_1		ADC_CH_MUXPOS_PIN6_gc		// IR1 sensor on PB6
#define MUX_IR_SENSOR_2		ADC_CH_MUXPOS_PIN7_gc		// IR2 sensor on PB7
#define MUX_IR_SENSOR_3		ADC_CH_MUXPOS_PIN4_gc		// IR3 sensor on PB4
#define MUX_IR_SENSOR_4		ADC_CH_MUXPOS_PIN2_gc		// IR4 sensor on PB2
#define MUX_IR_SENSOR_5		ADC_CH_MUXPOS_PIN3_gc		// IR5 sensor on PB3
#define MUX_SENSOR_CLR		0b10000111

void IR_sensor_init();
uint8_t get_IR_sensor(uint8_t sensor_num);
int8_t find_median(int8_t* meas); // Helper function for getting the middle of the 3 measurements
uint8_t check_collisions();
inline void IR_sensor_enable(){ ADCB.CTRLA |= ADC_ENABLE_bm; }
inline void IR_sensor_disable(){ ADCB.CTRLA &= ~ADC_ENABLE_bm; }

#endif
