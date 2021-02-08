/****************************************************************************
 *
 *   Copyright (c) 2019 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file ISM330DLC.hpp
 *
 * Driver for the ST ISM330DLC connected via SPI.
 *
 */

#pragma once

#include "ST_ISM330DLC_Registers.hpp"

#include <drivers/drv_hrt.h>
#include <lib/drivers/accelerometer/PX4Accelerometer.hpp>
#include <lib/drivers/device/spi.h>
#include <lib/drivers/gyroscope/PX4Gyroscope.hpp>
#include <lib/ecl/geo/geo.h>
#include <lib/perf/perf_counter.h>
#include <px4_platform_common/i2c_spi_buses.h>

using namespace ST_ISM330DLC;

class ISM330DLC : public device::SPI, public I2CSPIDriver<ISM330DLC>
{
public:
	ISM330DLC(I2CSPIBusOption bus_option, int bus, uint32_t device, enum Rotation rotation, int bus_frequency,
		  spi_mode_e spi_mode, spi_drdy_gpio_t drdy_gpio);
	~ISM330DLC() override;

	static I2CSPIDriverBase *instantiate(const BusCLIArguments &cli, const BusInstanceIterator &iterator,
					     int runtime_instance);
	static void print_usage();

	void print_status() override;

	void RunImpl();

	int init() override;

	void Start();
	bool Reset();

protected:
	void custom_method(const BusCLIArguments &cli) override;
	void exit_and_cleanup() override;
private:

	// Sensor Configuration
	static constexpr uint32_t GYRO_RATE{ST_ISM330DLC::G_ODR};
	static constexpr uint32_t ACCEL_RATE{ST_ISM330DLC::LA_ODR};
	static constexpr uint32_t FIFO_MAX_SAMPLES{ math::min(math::min(FIFO::SIZE / sizeof(FIFO::DATA) + 1,
			sizeof(sensor_gyro_fifo_s::x) / sizeof(sensor_gyro_fifo_s::x[0])),
			sizeof(sensor_accel_fifo_s::x) / sizeof(sensor_accel_fifo_s::x[0]))
						  };

	// Transfer data
	struct FIFOTransferBuffer {
		uint8_t cmd{static_cast<uint8_t>(Register::FIFO_DATA_OUT_L) | DIR_READ};
		FIFO::DATA f[FIFO_MAX_SAMPLES] {};
	};
	// ensure no struct padding
	static_assert(sizeof(FIFOTransferBuffer) == (sizeof(uint8_t) + FIFO_MAX_SAMPLES *sizeof(FIFO::DATA)));

	struct register_config_t {
		Register reg;
		uint8_t set_bits{0};
		uint8_t clear_bits{0};
	};

	int probe() override;

	void ConfigureSampleRate(int sample_rate);

	static int DataReadyInterruptCallback(int irq, void *context, void *arg);
	void DataReady();

	bool RegisterCheck(const register_config_t &reg_cfg, bool notify = false);
	uint8_t RegisterRead(Register reg);
	void RegisterWrite(Register reg, uint8_t value);
	void RegisterSetBits(Register reg, uint8_t setbits);
	void RegisterClearBits(Register reg, uint8_t clearbits);
	void RegisterSetAndClearBits(Register reg, uint8_t setbits, uint8_t clearbits);

	bool Configure();

	uint16_t FIFOReadCount();
	bool FIFORead(const hrt_abstime &timestamp_sample, uint16_t samples);
	void ResetFIFO();

	const spi_drdy_gpio_t _drdy_gpio;

	PX4Accelerometer _px4_accel;
	PX4Gyroscope _px4_gyro;

	uint16_t _fifo_empty_interval_us{1000}; // 1000 us / 1000 Hz transfer interval
	uint8_t _fifo_gyro_samples{static_cast<uint8_t>(_fifo_empty_interval_us / (1000000 / GYRO_RATE))};
	uint8_t _fifo_accel_samples{static_cast<uint8_t>(_fifo_empty_interval_us / (1000000 / ACCEL_RATE))};

	perf_counter_t _bad_transfer_perf{perf_alloc(PC_COUNT, MODULE_NAME": bad transfer")};
	perf_counter_t _interval_perf{perf_alloc(PC_INTERVAL, MODULE_NAME": run interval")};
	perf_counter_t _transfer_perf{perf_alloc(PC_ELAPSED, MODULE_NAME": transfer")};
	perf_counter_t _fifo_empty_perf{perf_alloc(PC_COUNT, MODULE_NAME": fifo empty")};
	perf_counter_t _fifo_overflow_perf{perf_alloc(PC_COUNT, MODULE_NAME": fifo overflow")};
	perf_counter_t _fifo_reset_perf{perf_alloc(PC_COUNT, MODULE_NAME": fifo reset")};
	perf_counter_t _drdy_count_perf{perf_alloc(PC_COUNT, MODULE_NAME": drdy count")};
	perf_counter_t _drdy_interval_perf{perf_alloc(PC_INTERVAL, MODULE_NAME": drdy interval")};
	perf_counter_t _bad_register_perf{perf_alloc(PC_COUNT, MODULE_NAME": bad register")};

	hrt_abstime _last_config_check_timestamp{0};
	hrt_abstime _fifo_watermark_interrupt_timestamp{0};
	hrt_abstime _temperature_update_timestamp{0};

	px4::atomic<uint8_t> _fifo_read_samples{0};
	bool _data_ready_interrupt_enabled{false};
	uint8_t _checked_register{0};

	static constexpr uint8_t size_register_cfg{5};
	register_config_t _register_cfg[size_register_cfg] {
		// Register               | Set bits, Clear bits

		// Accelerometer configuration
		// Accel has an analog anti-aliasing filter (BW @ 1.5kHz, if BW0_XL=1: BW @ 400Hz)
		// CTRL1_XL: Accelerometer 16 G range and ODR 6.66 kHz, LPF1_BW_SEL=0
		{ Register::CTRL1_XL,      CTRL1_XL_BIT::ODR_XL_6_66KHZ | CTRL1_XL_BIT::FS_XL_16, CTRL1_XL_BIT::LPF1_BW_SEL},
		{ Register::CTRL8_XL,      0, 0xff}, // disable additional fitering (LPF2, HP)

		// Gyroscope configuration
		{ Register::CTRL4_C,       CTRL4_C_BIT::LPF1_SEL_G, 0}, // enable LPF1 (disabling it adds too much noise)
		// CTRL2_G: Gyroscope 2000 degrees/second and ODR 6.66 kHz
		{ Register::CTRL2_G,       CTRL2_G_BIT::ODR_G_6_66KHZ | CTRL2_G_BIT::FS_G_2000, 0},
		// CTRL6_C: Gyroscope low-pass filter (LPF1) bandwidth 937 Hz (maximum)
		{ Register::CTRL6_C,       CTRL6_C_BIT::FTYPE_GYRO_LPF_BW_937_HZ, 0},
	};
};