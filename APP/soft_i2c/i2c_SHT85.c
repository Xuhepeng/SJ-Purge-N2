/********************************************
*Filename:       SHT85.c
*Revised:        Date: 06-22 14:42
*Author:         SYMBEL
*Description:    SHT85驱动
*********************************************/
#include "i2c_SHT85.h"
#include <stdbool.h>
#include "stdio.h" 

/****************************IIC引脚定义**************************/
#define SHT85_SCK_GPIO_Port GPIOB
#define SHT85_SCK_Pin GPIO_PIN_10
#define SHT85_SDA_GPIO_Port GPIOB
#define SHT85_SDA_Pin GPIO_PIN_11
#define SHT85_SCL(x) HAL_GPIO_WritePin(SHT85_SCK_GPIO_Port, SHT85_SCK_Pin, x?GPIO_PIN_SET:GPIO_PIN_RESET)
#define SHT85_SDA(x) HAL_GPIO_WritePin(SHT85_SDA_GPIO_Port, SHT85_SDA_Pin, x?GPIO_PIN_SET:GPIO_PIN_RESET)
#define IS_SHT85_SDA() HAL_GPIO_ReadPin(SHT85_SDA_GPIO_Port, SHT85_SDA_Pin) //读取SDA脚电平
/****************************IIC引脚定义**************************/
 
/*****************************函数声明***************************/
static uint8_t StartWriteAccess(void);
static uint8_t StartReadAccess(void);
static void StopAccess(void);
static uint8_t WriteCommand(etCommands command);
static uint8_t Read2BytesAndCrc(uint16_t* data, bool finAck, uint8_t timeout);
static uint8_t CalcCrc(uint8_t data[], uint8_t nbrOfBytes);
static uint8_t CheckCrc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum);
static float CalcTemperature(uint16_t rawValue);
static float CalcHumidity(uint16_t rawValue);
void SHT85_Init(void);
void SHT85_FSM(void);
 
/****************************结构体初始化**************************/
TH_Class_t TH_Class_SHT85 = {
  .SerialNumber = 0,
  .Temperature = 0.0f,
  .Humidity = 0.0f,
  .H2O = 0.0f,
  .init = SHT85_Init,
  .loop = SHT85_FSM,
};
 
 
/**********************************************************
*函数：DelayUs
*功能：延时1us
*参数：us:延时时间 单位：us
*返回：无
*描述：无
**********************************************************/
static void DelayUs(uint32_t us)
{
  for(int i=0; i<us; i++){
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
    __NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();__NOP();
  }
}
 
static void I2c_StartCondition(void)
{
	SHT85_SDA(1);
	SHT85_SCL(1);
	DelayUs(3);
	SHT85_SDA(0);
	DelayUs(3);
	SHT85_SCL(0);
	DelayUs(3);
}
 
static void I2c_StopCondition(void)
{
	SHT85_SDA(0);
	SHT85_SCL(0);
	DelayUs(2);
	SHT85_SCL(1);
	DelayUs(3);
	SHT85_SDA(1);
	DelayUs(3);
}
 
static uint8_t I2c_WriteByte(uint8_t txByte)
{
  uint8_t mask,error=0,i;
//  SHT85_SDA(0);
  for(i=1;i<20;i++); 
  for(mask=0x80; mask>0; mask>>=1)//shift bit for masking (8 times)
  { 
    if ((mask & txByte) == 0) 
      SHT85_SDA(0);        //masking txByte, write bit to SDA-Line
    else 
      SHT85_SDA(1);
    DelayUs(1);     //data hold time(t_HD;DAT)
    SHT85_SCL(1);           //generate clock pulse on SCL
    DelayUs(1);     //data set-up time (t_SU;DAT)
    SHT85_SCL(0);
    DelayUs(1);     //SCL high time (t_HIGH)
  }
  error = IS_SHT85_SDA();             //release SDA-line
  SHT85_SCL(1);             //clk #9 for ack
  DelayUs(1);       //data set-up time (t_SU;DAT)
 
  error = IS_SHT85_SDA();
 
  SHT85_SCL(0);
  DelayUs(3);       //wait time to see byte package on scope
  return error;           //return error code
}
 
static uint8_t I2c_ReadByte(etI2cAck ack)
{
	uint8_t mask,rxByte=0,i,rxb;
 
  SHT85_SDA(1);
 
	rxb = IS_SHT85_SDA(); 						//release SDA-line
	for(i=1;i<20;i++); 
	for(mask=0x80; mask>0; mask>>=1)//shift bit for masking (8 times)
	{ 
		SHT85_SCL(1); 					//start clock on SCL-line
		DelayUs(2); 		//data set-up time (t_SU;DAT)
		rxb = IS_SHT85_SDA();
		if(rxb) 
			rxByte=(rxByte | mask); //read bit
		SHT85_SCL(0);
		DelayUs(2); 		//data hold time(t_HD;DAT)
	}
	if(ack == ACK)
		SHT85_SDA(0);
	else
		SHT85_SDA(1);
	DelayUs(2); 			//data set-up time (t_SU;DAT)
	SHT85_SCL(1); 						//clk #9 for ack
	DelayUs(5); 			//SCL high time (t_HIGH)
	SHT85_SCL(0);
	//SHT85_SDA(1); 						//release SDA-line
	DelayUs(2); 			//wait time to see byte package on scope
	
	return rxByte; //return error code
}
 
uint8_t I2c_GeneralCallReset(void)
{
  uint8_t error;
  
  I2c_StartCondition();
  error = I2c_WriteByte(0x00);
  if(error == NO_ERROR) {
    error = I2c_WriteByte(0x06);
  }
  return error;
}
 
uint8_t SHT85_ReadSerialNumber(uint32_t* serialNumber)
{
  uint8_t error; // error code
  uint16_t serialNumWords[2];
  error = StartWriteAccess();
  // write "read serial number" command
  if(error == NO_ERROR) {
    error = WriteCommand(CMD_READ_SERIALNBR);
  }
  // if no error, start read access
  if(error == NO_ERROR) {
    error = StartReadAccess();
  }
  // if no error, read first serial number word
  if(error == NO_ERROR) {
    error = Read2BytesAndCrc(&serialNumWords[0], true, 100);
  }
  // if no error, read second serial number word
  if(error == NO_ERROR) {
    error = Read2BytesAndCrc(&serialNumWords[1], false, 0);
  }
  StopAccess();
  // if no error, calc serial number as 32-bit integer
  if(error == NO_ERROR) {
    *serialNumber = (serialNumWords[0] << 16) | serialNumWords[1];
  }
  return error;
}
 
uint8_t SHT85_ReadStatus(uint16_t* status)
{
  uint8_t error; // error code
  error = StartWriteAccess();
  // if no error, write "read status" command
  if(error == NO_ERROR) {
    error = WriteCommand(CMD_READ_STATUS);
  }
  // if no error, start read access
  if(error == NO_ERROR) {
    error = StartReadAccess();
  }
  // if no error, read status
  if(error == NO_ERROR) {
    error = Read2BytesAndCrc(status, false, 0);
  }
  StopAccess();
  return error;
}
 
uint8_t SHT85_ClearAllAlertFlags(void)
{
  uint8_t error; // error code
  error = StartWriteAccess();
  // if no error, write clear status register command
  if(error == NO_ERROR) {
    error = WriteCommand(CMD_CLEAR_STATUS);
  }
  StopAccess();
  return error;
}
 
uint8_t SHT85_SingleMeasurment(float* temperature, float* humidity, etSingleMeasureModes measureMode, uint8_t timeout)
{
  uint8_t  error;           // error code
  uint16_t rawValueTemp;    // temperature raw value from sensor
  uint16_t rawValueHumi;    // humidity raw value from sensor
  error  = StartWriteAccess();
  // if no error
  if(error == NO_ERROR) {
    // start measurement
    error = WriteCommand((etCommands)measureMode);
  }
  // if no error, wait until measurement ready
  if(error == NO_ERROR) {
    // poll every 1ms for measurement ready until timeout
    while(timeout--) {
      // check if the measurement has finished
      error = StartReadAccess();
      // if measurement has finished -> exit loop
      if(error == NO_ERROR) break;
      // delay 1ms
      DelayUs(1000);
    }
    // check for timeout error
    if(timeout == 0) {
      error = TIMEOUT_ERROR;
    }
  }
  // if no error, read temperature and humidity raw values
  if(error == NO_ERROR) {
    error |= Read2BytesAndCrc(&rawValueTemp, true, 0);
    error |= Read2BytesAndCrc(&rawValueHumi, false, 0);
  }
  StopAccess();
  // if no error, calculate temperature in °C and humidity in %RH
  if(error == NO_ERROR) {
    *temperature = CalcTemperature(rawValueTemp);
    *humidity = CalcHumidity(rawValueHumi);
  }
  return error;
}
 
uint8_t SHT85_StartPeriodicMeasurment(etPeriodicMeasureModes measureMode)
{
  uint8_t error; // error code
  
  error = StartWriteAccess();
  // if no error, start periodic measurement 
  if(error == NO_ERROR) {
    error = WriteCommand((etCommands)measureMode);
  }
  StopAccess();
  return error;
}
 
uint8_t SHT85_StopPeriodicMeasurment(void)
{
  uint8_t error; // error code
  error = StartWriteAccess();
  // if no error, write breake command
  if(error == NO_ERROR) {
    error = WriteCommand(CMD_BREAK);
  }
  StopAccess();
  return error;
}
 
uint8_t SHT85_ReadMeasurementBuffer(float* temperature, float* humidity)
{
  uint8_t  error;        // error code
  uint16_t rawValueTemp = 0; // raw temperature from sensor
  uint16_t rawValueHumi = 0; // raw humidity from sensor
  
  printf("开始SHT85测量...\r\n");
  
  error = StartWriteAccess();
  if(error != NO_ERROR) {
    printf("StartWriteAccess失败: error=0x%02X\r\n", error);
    return error;
  }
  
  // if no error, read measurements
  if(error == NO_ERROR) {
    printf("发送CMD_FETCH_DATA命令...\r\n");
    error = WriteCommand(CMD_FETCH_DATA);
  }
  if(error == NO_ERROR) {
    printf("发送CMD_FETCH_DATA成功，等待15ms...\r\n");
    HAL_Delay(15); // 等待传感器响应
    error = StartReadAccess();  
  }
  if(error == NO_ERROR) {
    printf("StartReadAccess成功，读取温度数据...\r\n");
    error = Read2BytesAndCrc(&rawValueTemp, true, 0);
  }
  if(error == NO_ERROR) {
    printf("读取温度数据成功: rawTemp=%d\r\n", rawValueTemp);
    error = Read2BytesAndCrc(&rawValueHumi, false, 0);
  }
  if(error == NO_ERROR) {
    printf("读取湿度数据成功: rawHumi=%d\r\n", rawValueHumi);
    // if no error, calculate temperature in °C and humidity in %RH
    *temperature = CalcTemperature(rawValueTemp);
    *humidity = CalcHumidity(rawValueHumi);
    printf("计算结果: temp=%.2f, humi=%.2f\r\n", *temperature, *humidity);
  }
  StopAccess();
  
  // 添加调试信息输出
  if(error == NO_ERROR) {
    printf("SHT85测量成功: rawTemp=%d, rawHumi=%d, temp=%.2f, humi=%.2f\r\n", 
           rawValueTemp, rawValueHumi, *temperature, *humidity);
  } else {
    printf("SHT85测量失败: error=0x%02X\r\n", error);
  }
  
  return error;
}
 
uint8_t SHT85_EnableHeater(void)
{
  uint8_t error; // error code
  error = StartWriteAccess();
  // if no error, write heater enable command
  if(error == NO_ERROR) {
    error = WriteCommand(CMD_HEATER_ENABLE);
  }
  StopAccess();
  return error;
}
 
uint8_t SHT85_DisableHeater(void)
{
  uint8_t error; // error code
  error = StartWriteAccess();
  // if no error, write heater disable command
  if(error == NO_ERROR) {
    error = WriteCommand(CMD_HEATER_DISABLE);
  }
  StopAccess();
  return error;
}
 
uint8_t SHT85_SoftReset(void)
{
  uint8_t error; // error code
  error = StartWriteAccess();
  // write reset command
  if(error == NO_ERROR) {
    error  = WriteCommand(CMD_SOFT_RESET);
  }
  StopAccess();
  // if no error, wait 50 ms after reset
  if(error == NO_ERROR) {
    DelayUs(50000);
  }
  return error;
}
 
static uint8_t StartWriteAccess(void)
{
  uint8_t error; // error code
  // write a start condition
  I2c_StartCondition();
  // write the sensor I2C address with the write flag
  error = I2c_WriteByte(I2C_ADDR << 1);
  return error;
}
 
static uint8_t StartReadAccess(void)
{
  uint8_t error; // error code
  // write a start condition
  I2c_StartCondition();
  // write the sensor I2C address with the read flag
  error = I2c_WriteByte(I2C_ADDR << 1|0x01);
  return error;
}
 
static void StopAccess(void)
{
  // write a stop condition
  I2c_StopCondition();
}
 
static uint8_t WriteCommand(etCommands command)
{
  uint8_t error; // error code
  // write the upper 8 bits of the command to the sensor
  error = I2c_WriteByte(command >> 8);
  // write the lower 8 bits of the command to the sensor
  error |= I2c_WriteByte(command & 0xFF);
  return error;
}
 
static uint8_t Read2BytesAndCrc(uint16_t* data, bool finAck, uint8_t timeout)
{
  uint8_t error;    // error code
  uint8_t bytes[2]; // read data array
  uint8_t checksum; // checksum byte
  // read two data bytes and one checksum byte
  bytes[0] = I2c_ReadByte(ACK);
  bytes[1] = I2c_ReadByte(ACK);
  checksum = I2c_ReadByte(finAck ? ACK : NO_ACK);
  // verify checksum
  error = CheckCrc(bytes, 2, checksum);
  // combine the two bytes to a 16-bit value
  *data = (bytes[0] << 8) | bytes[1];
  return error;
}
 
static uint8_t CalcCrc(uint8_t data[], uint8_t nbrOfBytes)
{
  uint8_t bit;        // bit mask
  uint8_t crc = 0xFF; // calculated checksum
  uint8_t byteCtr;    // byte counter
  
  // calculates 8-Bit checksum with given polynomial
  for(byteCtr = 0; byteCtr < nbrOfBytes; byteCtr++) {
    crc ^= (data[byteCtr]);
    for(bit = 8; bit > 0; --bit) {
      if(crc & 0x80) {
        crc = (crc << 1) ^ CRC_POLYNOMIAL;
      } else {
        crc = (crc << 1);
      }
    }
  }
  
  return crc;
}
 
static uint8_t CheckCrc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum)
{
  // calculates 8-Bit checksum
  uint8_t crc = CalcCrc(data, nbrOfBytes);
  // verify checksum
  return (crc != checksum) ? CHECKSUM_ERROR : NO_ERROR;
}
 
 
/**********************************************************
*函数：CalcTemperature
*功能：温度转换
*参数：rawValue：温度采样值
*返回：温度，单位℃
*描述：
**********************************************************/
static float CalcTemperature(uint16_t rawValue)
{
  // calculate temperature [°C]
  // T = -45 + 175 * rawValue / (2^16-1)
  return 175.0f * (float)rawValue / 65535.0f - 45.0f;
}
 
/**********************************************************
*函数：CalcHumidity
*功能：相对湿度转换
*参数：rawValue：相对湿度采样值
*返回：相对湿度，单位%
*描述：
**********************************************************/
static float CalcHumidity(uint16_t rawValue)
{
  // calculate relative humidity [%RH]
  // RH = rawValue / (2^16-1) * 100
  return 100.0f * (float)rawValue / 65535.0f;
}
 
/**********************************************************
*函数：FnTandRHToH2O
*功能：饱和水蒸气含量计算
*参数：nInTemp：温度，单位℃
*      nInRH：  相对湿度，单位%
*返回：饱和水蒸气含量，单位℃
*描述：
**********************************************************/
static float FnTandRHToH2O(float nInTemp, float nInRH)
{
	float fCnH2O; //H2O含量
	float fCnH2OMax;
	float fTemp; 
 
	fTemp = nInTemp;
	fCnH2OMax = 0.0000008734* fTemp* fTemp* fTemp 
						- 0.0000013617* fTemp* fTemp
						+ 0.0004784740* fTemp
						+ 0.0068091716;
	fCnH2O = nInRH*fCnH2OMax;
	return(fCnH2O);
}
 
/**********************************************************
*函数：SHT85_Init
*功能：SHT85初始化
*参数：无
*返回：无
*描述：IIC引脚初始化，读取SN，配置测量频率。
**********************************************************/
void SHT85_Init(void)
{
  /*IIC引脚初始化。此程序使用STM32 HAL库，已在main()初始化*/
  /*读取SN*/
  SHT85_ReadSerialNumber(&TH_Class_SHT85.SerialNumber);
  /*设置测量频率*/
  SHT85_StartPeriodicMeasurment(PERI_MEAS_MEDIUM_10_HZ);
}
 
/**********************************************************
*函数：SHT85_FSM
*功能：SHT85 loop函数
*参数：无
*返回：无
*描述：读取温度、相对湿度、计算饱和水蒸气含量。建议执行频率1Hz。
**********************************************************/
void SHT85_FSM(void)
{
  /*读取温度、相对湿度*/
  SHT85_ReadMeasurementBuffer(&TH_Class_SHT85.Temperature, &TH_Class_SHT85.Humidity);
  /*计算饱和水蒸气含量*/
  TH_Class_SHT85.H2O = FnTandRHToH2O(TH_Class_SHT85.Temperature, TH_Class_SHT85.Humidity);
}	