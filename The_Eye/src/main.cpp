/**
  ******************************************************************************
  * @file    main.cpp
  * @author  Michal Prevratil
  * @version V1.0
  * @date    01-December-2013
  * @brief   Default main function.
  ******************************************************************************
*/

#include "main.h"

using namespace flyhero;

#ifdef LOG
extern "C" void initialise_monitor_handles(void);
#endif

ESP *esp = ESP8266::Instance();
PWM_Generator *pwm = PWM_Generator::Instance();
MPU6050 *mpu = MPU6050::Instance();
MS5611 *ms5611 = MS5611::Instance();
NEO_M8N *neo = NEO_M8N::Instance();
Logger *logger = Logger::Instance();

void Arm_Callback();
void IPD_Callback(uint8_t link_ID, uint8_t *data, uint16_t length);

PID PID_Roll, PID_Pitch, PID_Yaw;
bool connected = false;
bool log_data = false;
bool start = false;
bool data_received = false;
bool inverse_yaw = false;
uint16_t throttle = 0;
IWDG_HandleTypeDef hiwdg;

MPU6050::Raw_Data gyroData, accelData;

int main(void)
{
	HAL_Init();
#ifdef LOG
	initialise_monitor_handles();
#endif

	uint8_t status;
	uint32_t timestamp;
	float data[3];
	long FL, BL, FR, BR;
	FL = BL = FR = BR = 0;
	long pitchCorrection, rollCorrection, yawCorrection;

	PID_Roll.imax(50);
	PID_Pitch.imax(50);
	PID_Yaw.imax(50);

	LEDs::Init();

	hiwdg.Instance = IWDG;
	hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
	hiwdg.Init.Reload = 480;
	// timeout after 2 s

	// reset gyro
	if (mpu->Init()) {
		LEDs::TurnOn(LEDs::Yellow);
		while (true);
	}

	logger->Init();
	esp->Init(&IPD_Callback);

	timestamp = HAL_GetTick();
	// not yet implemented
	//Timer::Start_Task([]{ LEDs::Toggle(LEDs::Green); }, 750);

	while (!connected) {
		if (HAL_GetTick() - timestamp >= 750) {
			LEDs::Toggle(LEDs::Green);

			timestamp = HAL_GetTick();
		}
		esp->Process_Data();
	}
	LEDs::TurnOff(LEDs::Green);

	pwm->Init();
	pwm->Arm(&Arm_Callback);

	if (mpu->Calibrate() != HAL_OK) {
		LEDs::TurnOn(LEDs::Yellow);
		while (true);
	}

	pwm->SetPulse(1100, 1);
	pwm->SetPulse(1100, 2);
	pwm->SetPulse(1100, 3);
	pwm->SetPulse(1100, 4);

	Timer::Delay_ms(250);

	pwm->SetPulse(940, 4);
	pwm->SetPulse(940, 1);
	pwm->SetPulse(940, 3);
	pwm->SetPulse(940, 2);

	while (!start) {
		if (HAL_GetTick() - timestamp >= 750) {
			LEDs::Toggle(LEDs::Green);

			timestamp = HAL_GetTick();
		}
		esp->Process_Data();
	}

	LEDs::TurnOn(LEDs::Green);

#ifndef LOG
	HAL_IWDG_Init(&hiwdg);
#endif

	throttle = 0;
	mpu->ready = true;

	while (true) {
		if (mpu->Data_Ready()) {

			if (mpu->Start_Read_Raw() != HAL_OK) {
				LEDs::TurnOn(LEDs::Orange);
			}
		}

		if (mpu->Data_Read()) {
			if (data_received)
				HAL_IWDG_Refresh(&hiwdg);
			data_received = false;

			if (log_data) {
				mpu->Complete_Read_Raw(&gyroData, &accelData);

				uint8_t tmp[15];
				tmp[0] = accelData.x & 0xFF;
				tmp[1] = accelData.x >> 8;
				tmp[2] = accelData.y & 0xFF;
				tmp[3] = accelData.y >> 8;
				tmp[4] = accelData.z & 0xFF;
				tmp[5] = accelData.z >> 8;
				tmp[6] = gyroData.x & 0xFF;
				tmp[7] = gyroData.x >> 8;
				tmp[8] = gyroData.y & 0xFF;
				tmp[9] = gyroData.y >> 8;
				tmp[10] = gyroData.z & 0xFF;
				tmp[11] = gyroData.z >> 8;
				tmp[12] = throttle & 0xFF;
				tmp[13] = throttle >> 8;
				tmp[14] = 0;

				for (uint8_t i = 0; i <= 13; i++)
					tmp[14] ^= tmp[i];

				logger->Print(tmp, 15);
			}
			else {
				// 157 us
				mpu->Get_Euler(data, data + 1, data + 2);
			}

			// 73 us
			if (throttle >= 1050) {
				// 50 us
				pitchCorrection = PID_Pitch.get_pid(data[1], 1);
				rollCorrection = PID_Roll.get_pid(data[0], 1);
				yawCorrection = PID_Yaw.get_pid(data[2], 1);

				// not sure about yaw signs
				if (!inverse_yaw) {
					FL = throttle + rollCorrection + pitchCorrection + yawCorrection; // PB2
					BL = throttle + rollCorrection - pitchCorrection - yawCorrection; // PA15
					FR = throttle - rollCorrection + pitchCorrection - yawCorrection; // PB10
					BR = throttle - rollCorrection - pitchCorrection + yawCorrection; // PA1
				}
				else {
					FL = throttle + rollCorrection + pitchCorrection - yawCorrection; // PB2
					BL = throttle + rollCorrection - pitchCorrection + yawCorrection; // PA15
					FR = throttle - rollCorrection + pitchCorrection + yawCorrection; // PB10
					BR = throttle - rollCorrection - pitchCorrection - yawCorrection; // PA1
				}

				if (FL > 2000)
					FL = 2000;
				else if (FL < 1050)
					FL = 940;

				if (BL > 2000)
					BL = 2000;
				else if (BL < 1050)
					BL = 940;

				if (FR > 2000)
					FR = 2000;
				else if (FR < 1050)
					FR = 940;

				if (BR > 2000)
					BR = 2000;
				else if (BR < 1050)
					BR = 940;

				// 17 us
				pwm->SetPulse(FL, 3);
				pwm->SetPulse(BL, 2);
				pwm->SetPulse(FR, 4);
				pwm->SetPulse(BR, 1);
			}
			else {
				pwm->SetPulse(940, 4);
				pwm->SetPulse(940, 1);
				pwm->SetPulse(940, 3);
				pwm->SetPulse(940, 2);
			}

			continue;
		}

		esp->Process_Data();
	}
}

void IPD_Callback(uint8_t link_ID, uint8_t *data, uint16_t length) {
	switch (length) {
	case 22:
		// around 75 us
		if (data[0] == 0x5D) {
			uint16_t roll_kP, pitch_kP, yaw_kP;
			uint16_t roll_kI, pitch_kI, yaw_kI;
			uint16_t roll_kD, pitch_kD, yaw_kD;

			data_received = true;

			// 3 us
			throttle = data[1] << 8;
			throttle |= data[2];

			// 3 us
			roll_kP = data[3] << 8;
			roll_kP |= data[4];

			roll_kI = data[5] << 8;
			roll_kI |= data[6];

			roll_kD = data[7] << 8;
			roll_kD |= data[8];

			// 29 us
			PID_Roll.kP(roll_kP * 0.01f);
			PID_Roll.kI(roll_kI * 0.01f);
			PID_Roll.kD(roll_kD * 0.01f);

			pitch_kP = data[9] << 8;
			pitch_kP |= data[10];

			pitch_kI = data[11] << 8;
			pitch_kI |= data[12];

			pitch_kD = data[13] << 8;
			pitch_kD |= data[14];

			PID_Pitch.kP(pitch_kP * 0.01f);
			PID_Pitch.kI(pitch_kI * 0.01f);
			PID_Pitch.kD(pitch_kD * 0.01f);

			yaw_kP = data[15] << 8;
			yaw_kP |= data[16];

			yaw_kI = data[17] << 8;
			yaw_kI |= data[18];

			yaw_kD = data[19] << 8;
			yaw_kD |= data[20];

			PID_Yaw.kP(yaw_kP * 0.01f);
			PID_Yaw.kI(yaw_kI * 0.01f);
			PID_Yaw.kD(yaw_kD * 0.01f);


			inverse_yaw = (data[21] == 0x01);

			throttle += 1000;
		}
		break;
	case 3:
		if (data[0] == 0x3D) {
			start = true;
		}
		if (data[0] == 0x5D) {
			connected = true;
			log_data = (data[1] == 0x01 ? true : false);
		}
		break;
	}
}

void Arm_Callback() {
	esp->Process_Data();
	//HAL_IWDG_Refresh(&hiwdg);
}
