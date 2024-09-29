#include "OV2640_sccb.h"

#define sda_port        GPIO(GPIO_PORTA, 8)
#define scl_port        GPIO(GPIO_PORTA, 9)


#define SCCB_SIC_H()      digitalWrite(scl_port,HIGH)	 	//SCL H
#define SCCB_SIC_L()      digitalWrite(scl_port,LOW)		 	//SCL H

#define SCCB_SID_H()      digitalWrite(sda_port,HIGH)   //SDA	H
#define SCCB_SID_L()      digitalWrite(sda_port,LOW)    //SDA	H
#define SCCB_DATA_IN      pinMode(sda_port, INPUT);
#define SCCB_DATA_OUT     pinMode(sda_port, OUTPUT);

#define SCCB_SID_STATE	  digitalRead(sda_port)

void sccb_bus_init(void);
void sccb_bus_start(void);
void sccb_bus_stop(void);
void sccb_bus_send_noack(void);
void sccb_bus_send_ack(void);
unsigned char sccb_bus_write_byte(unsigned char data);
unsigned char sccb_bus_read_byte(void);

extern unsigned char I2C_TIM;



#define INPUT GPIO_DIRECTION_IN
#define OUTPUT GPIO_DIRECTION_OUT

#define HIGH  1
#define LOW   0

#define digitalWrite(pin, val) gpio_set_pin_level(pin, val)
#define digitalRead(pin) gpio_get_pin_level(pin)
#define pinMode(pin, val) gpio_set_pin_direction(pin, val)

#define SCCB_SIC_H()      digitalWrite(scl_port,HIGH)        //SCL H
#define SCCB_SIC_L()      digitalWrite(scl_port,LOW)            //SCL H

#define SCCB_SID_H()      digitalWrite(sda_port,HIGH)   //SDA	H
#define SCCB_SID_L()      digitalWrite(sda_port,LOW)    //SDA	H


#define SCCB_DATA_IN      pinMode(sda_port, INPUT);
#define SCCB_DATA_OUT     pinMode(sda_port, OUTPUT);

#define SCCB_SID_STATE      digitalRead(sda_port)


unsigned char I2C_TIM = 30;

void sccb_bus_init(void) {
    pinMode(sda_port, OUTPUT);
    pinMode(scl_port, OUTPUT);
    digitalWrite(sda_port, HIGH);
    digitalWrite(scl_port, HIGH);
}

void sccb_bus_start(void) {
    SCCB_SID_H();
    delay_us(I2C_TIM);
    SCCB_SIC_H();
    delay_us(I2C_TIM);
    SCCB_SID_L();
    delay_us(I2C_TIM);
    SCCB_SIC_L();
    delay_us(I2C_TIM);
}

void sccb_bus_stop(void) {
    SCCB_SID_L();
    delay_us(I2C_TIM);
    SCCB_SIC_H();
    delay_us(I2C_TIM);
    SCCB_SID_H();
    delay_us(I2C_TIM);
}

void sccb_bus_send_noack(void) {
    SCCB_SID_H();
    delay_us(I2C_TIM);
    SCCB_SIC_H();
    delay_us(I2C_TIM);
    SCCB_SIC_L();
    delay_us(I2C_TIM);
    SCCB_SID_L();
    delay_us(I2C_TIM);
}

void sccb_bus_send_ack(void) {
    SCCB_SID_L();
    delay_us(I2C_TIM);
    SCCB_SIC_L();
    delay_us(I2C_TIM);
    SCCB_SIC_H();
    delay_us(I2C_TIM);
    SCCB_SIC_L();
    delay_us(I2C_TIM);
    SCCB_SID_L();
    delay_us(I2C_TIM);
}

unsigned char sccb_bus_write_byte(unsigned char data) {
    unsigned char i;
    unsigned char tem;
    for (i = 0; i < 8; i++) {
        if ((data << i) & 0x80) {
            SCCB_SID_H();
        } else {
            SCCB_SID_L();
        }
        delay_us(I2C_TIM);
        SCCB_SIC_H();
        delay_us(I2C_TIM);
        SCCB_SIC_L();
    }
    SCCB_DATA_IN;
    delay_us(I2C_TIM);
    SCCB_SIC_H();
    delay_us(I2C_TIM);
    if (SCCB_SID_STATE) {
        tem = 0;
    } else {
        tem = 1;
    }

    SCCB_SIC_L();
    delay_us(I2C_TIM);
    SCCB_DATA_OUT;
    return tem;
}

unsigned char sccb_bus_read_byte(void) {
    unsigned char i;
    unsigned char read = 0;
    SCCB_DATA_IN;
    for (i = 8; i > 0; i--) {
        delay_us(I2C_TIM);
        SCCB_SIC_H();
        delay_us(I2C_TIM);
        read = read << 1;
        if (SCCB_SID_STATE) {
            read += 1;
        }
        SCCB_SIC_L();
        delay_us(I2C_TIM);
    }
    SCCB_DATA_OUT;
    return read;
}

void OV2640_sccb_write_8bit_reg(uint8_t regID, uint8_t regDat) {
    delay_us(10);
    sccb_bus_start();
    if(sccb_bus_write_byte(OV2640_I2C_ADDR) == 0)
    {
        sccb_bus_stop();
//        return 1;
    }
    delay_us(10);
    if(sccb_bus_write_byte(regID) == 0)
    {
        sccb_bus_stop();
//        return 2;
    }
    delay_us(10);
    if(sccb_bus_write_byte(regDat)==0)
    {
        sccb_bus_stop();
//        return 3;
    }
    sccb_bus_stop();
//    return 0;
}


void OV2640_sccb_read_8bit_reg(unsigned char regID, unsigned char* regDat) {
    delay_us(10);

    sccb_bus_start();
    if(sccb_bus_write_byte(OV2640_I2C_ADDR) == 0)
    {
        sccb_bus_stop();
        //goto start;
//        return 1;
    }
    delay_us(10);
    if(sccb_bus_write_byte(regID)==0)//ID
    {
        sccb_bus_stop();
        //goto start;
//        return 2;
    }
    sccb_bus_stop();
    delay_us(10);
    sccb_bus_start();
    if(sccb_bus_write_byte(OV2640_I2C_ADDR|0x01)==0)
    {
        sccb_bus_stop();
        //goto start;
//        return 3;
    }
    delay_us(10);
    *regDat = sccb_bus_read_byte();
    sccb_bus_send_noack();
    sccb_bus_stop();
//    return 0;
}

//I2C Array Write 8bit address, 8bit data
void OV2640_sccb_write_8bit_reg_array(const struct sensor_reg reglist[]) {
    unsigned int reg_addr = 0;
    unsigned int reg_val = 0;
    const struct sensor_reg *next = reglist;
    while ((reg_addr != 0xff) | (reg_val != 0xff))
    {
        reg_addr =next->reg;
        reg_val = next->val;
        OV2640_sccb_write_8bit_reg(reg_addr, reg_val);
        delay_ms(10);
        next++;
    }

//    return err;
}

void OV2640_sccb_write_16bit_reg(uint16_t regID, uint8_t regDat) {
    sccb_bus_start();
    if(0==sccb_bus_write_byte(OV2640_I2C_ADDR))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(10);
    if(0==sccb_bus_write_byte(regID>>8))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(10);
    if(0==sccb_bus_write_byte(regID))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(10);
    if(0==sccb_bus_write_byte(regDat))
    {
        sccb_bus_stop();
//        return(0);
    }
    sccb_bus_stop();

//    return(1);
}

void OV2640_sccb_read_16bit_reg(uint16_t regID, uint8_t* regDat) {
    sccb_bus_start();
    if(0==sccb_bus_write_byte(0x78))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(20);
    delay_us(20);
    if(0==sccb_bus_write_byte(regID>>8))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(20);
    if(0==sccb_bus_write_byte(regID))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(20);
    sccb_bus_stop();

    delay_us(20);


    sccb_bus_start();
    if(0==sccb_bus_write_byte(0x79))
    {
        sccb_bus_stop();
//        return(0);
    }
    delay_us(20);
    *regDat=sccb_bus_read_byte();
    sccb_bus_send_noack();
    sccb_bus_stop();
//    return(1);
}


