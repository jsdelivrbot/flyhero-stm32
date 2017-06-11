/**
  ******************************************************************************
  * @file    main.c
  * @author  Ac6
  * @version V1.0
  * @date    01-December-2013
  * @brief   Default main function.
  ******************************************************************************
*/


#include "stm32f4xx_hal.h"
#include "stm32f4xx_nucleo.h"
#include "MPU6050.h"
#include "LEDs.h"
#include "Timer.h"

using namespace flyhero;

extern "C" void initialise_monitor_handles(void);

MPU6050 *mpu = MPU6050::Instance();

extern "C" {
	void DMA1_Stream5_IRQHandler(void)
	{
		HAL_DMA_IRQHandler(mpu->Get_DMA_Rx_Handle());
	}

	void EXTI1_IRQHandler(void)
	{
		HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
	}

	void I2C1_EV_IRQHandler(void)
	{
		HAL_I2C_EV_IRQHandler(mpu->Get_I2C_Handle());
	}

	void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
		mpu->Data_Read_Callback();
	}

	void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
		mpu->Data_Ready_Callback();
	}

	void TIM5_IRQHandler(void)
		{
			TIM_HandleTypeDef *htim5 = Timer::Get_Handle();

			// Channel 2 for HAL 1 ms tick
			if (__HAL_TIM_GET_ITSTATUS(htim5, TIM_IT_CC2) == SET) {
				__HAL_TIM_CLEAR_IT(htim5, TIM_IT_CC2);
				uint32_t val = __HAL_TIM_GetCounter(htim5);
				//if ((val - prev) >= 1000) {
					HAL_IncTick();
					// Prepare next interrupt
					__HAL_TIM_SetCompare(htim5, TIM_CHANNEL_2, val + 1000);
					//prev = val;
				//}
				//else {
				//	printf("should not happen\n");
				//}
			}
			//HAL_TIM_IRQHandler(Timer::Get_Handle());
		}

}

int main(void)
{
	HAL_Init();

	initialise_monitor_handles();

	LEDs::Init();

	HAL_Delay(1000);

	if (mpu->Init(false) || mpu->Calibrate()) {
		while (true) {
			LEDs::Toggle(LEDs::Green);
			HAL_Delay(500);
		}
	}


	printf("init complete\n");

	MPU6050::Raw_Data gyro, accel;

	uint32_t ppos = 0;
	double p[100][6];

	mpu->ready = true;

	uint32_t ticks = Timer::Get_Tick_Count();
	double roll, pitch, yaw;

	while (true) {
		if (mpu->Data_Ready() && Timer::Get_Tick_Count() - ticks >= 1000000) {
			if (mpu->Start_Read_Raw() != HAL_OK) {
				printf("a");
			}
			ticks = Timer::Get_Tick_Count();
		}
		if (mpu->Data_Read()) {
			//mpu->Complete_Read_Raw(&gyro, &accel);
			//printf("%d %d %d %d %d %d\n", accel.x, accel.y, accel.z, gyro.x, gyro.y, gyro.z);
			mpu->Get_Euler(&roll, &pitch, &yaw);

			printf("%f %f %f\n", roll, pitch, yaw);
		}
	}
}
