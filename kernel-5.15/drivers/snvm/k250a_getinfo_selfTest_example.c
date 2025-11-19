/*
 * Copyright (C) 2019 SAMSUNG S.LSI
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <time.h>

#include <ese_log.h>
#include <ese_time.h>
#include <ese_hal.h>

#define LOG_TAG "GET_INFO_HAL_TEST"

#define MAX_FILE_DATA_SIZE 256

#define CHIP_INFO_SIZE				(44)
#define KEY_FLAG_INFO_SIZE			(4)
#define PARTITION_INFO_SIZE			(4)
#define IMG_VERSION_SIZE			(4)
#define IMG_DATE_SIZE				(12)
#define VERSION_INFO_SIZE			(IMG_VERSION_SIZE + IMG_DATE_SIZE)
#define ADD_SEQUENCE(x) \
do {\
	(x) = (((x) + 1) % 2); \
} while(0);

#define SELFTEST_CMD_TEST_CHIP_SN	(0x01)
#define	SELFTEST_CMD_TIMER			(0x02)
#define SELFTEST_CMD_PROVISION_AK	(0x04)
#define	SELFTEST_CMD_NVM_CHECKSUM	(0x08)
#define SELFTEST_CMD_NVM_ACCESS		(0x10)
#define	SELFTEST_CMD_RAM_ACCESS		(0x20)
#define SELFTEST_CMD_ENC_CHECK		(0x40)
#define	SELFTEST_CMD_TEST_ALL		(0x7F)

typedef struct img_ver_s {
	uint8_t version[IMG_VERSION_SIZE];
	uint8_t build_date[IMG_DATE_SIZE];
} img_ver_t;

typedef struct {
	uint8_t chip[CHIP_INFO_SIZE];
	uint8_t key_flag[KEY_FLAG_INFO_SIZE];
	uint8_t partition[PARTITION_INFO_SIZE];
	img_ver_t bootloader;
	img_ver_t core;
	img_ver_t crypto;
} __attribute__((packed)) ese_info_t;

static void print_buffer(const char *buf_tag, uint8_t *buffer, uint32_t buffer_size)
{
	char tmp_buffer[49] = { 0 };
	char *tmp_p = tmp_buffer;
	uint32_t i = 0;
	LOG_D("%s : ", buf_tag);

	for (i = 0; i < buffer_size; i ++) {
		if (i != 0 && (i % 16) == 0) {
			LOG_D("%s", tmp_buffer);
			tmp_p = tmp_buffer;
		}
		sprintf(tmp_p, " %02x", buffer[i]);
		tmp_p += 3;
	}
	LOG_D("%s", tmp_buffer);
}

static int receive_data_with_polling(unsigned char *data, unsigned int data_size)
{
#define SLAVE_ADDRESS 0x21
#define MAX_POLLING_COUNT 100
	unsigned int data_len = 0, rec_len = 0;
	unsigned int poll_cnt = MAX_POLLING_COUNT;

	do {
		HAL_USLEEP(2 * 1000);
		data_len = ese_hal_receive(data, 1);
		if (data_len != 1) {
			LOG_E("mismatch receive size %u, %u", 1, data_len);
			return -1;
		}
	} while (data[0] == 0x0 && poll_cnt > 0);

	if (poll_cnt == 0) {
		LOG_E("response timeout");
		return -1;
	}

	if (data[0] != SLAVE_ADDRESS) {
		LOG_E("invalid slave address : 0x%02x", data[0]);
		return -1;
	}

	data_len += ese_hal_receive(data + 1, 2);
	if (data_len != 3) {
		LOG_E("mismatch receive size %u, %u", 3, data_len);
		return -1;
	}

	rec_len = data[2] + 1;
	data_len += ese_hal_receive(data + 3, rec_len);
	if (data_len != 3 + rec_len) {
		LOG_E("mismatch receive size %u, %u", 3 + rec_len, data_len);
		return -1;
	}

	return data_len;
}

int boot_firmware(void)
{
	unsigned char txdata[] = {0x12, 0x00, 0x04, 0x00, 0xfc, 0x00, 0x00, 0xea};
	unsigned int txdata_size = 8;
	unsigned char rxdata[MAX_FILE_DATA_SIZE] = {0x0, };
	unsigned int rxdata_size = 6;
	int ret_size = 0;

	HAL_USLEEP(2 * 1000);
	ret_size = ese_hal_send(txdata, txdata_size);
	if (ret_size != txdata_size) {
		LOG_E("mismatch send size : %u, %d", txdata_size, ret_size);
	}

	ret_size = receive_data_with_polling(rxdata, rxdata_size);
	if (ret_size != rxdata_size) {
		LOG_E("failed to star_bootFirmware (receive data fail)");
		return -1;
	}

	if (rxdata[3] != 0x90 && rxdata[4] != 0x00) {
		LOG_E("failed to star_bootFirmware (FW booting fail)");
		LOG_E("return value : %x%x", rxdata[3], rxdata[4]);
		return -1;
	}

	return 0;
}

char __hex2ascii(char hex)
{
	if (hex < 0x0A) hex += 0x30;
	else hex += 0x37;

	return (hex);
}

void __getHWInfo(uint8_t *chip_info, uint8_t *hw_info)
{
	uint32_t i = 0;

	for (i = 0; i < 0x05; i++) {
		hw_info[i + 2] = __hex2ascii(chip_info[0x04 + i]);
	}
	hw_info[11] = __hex2ascii(chip_info[0x2A]);
}

unsigned char __cal_lrc(unsigned char *data, unsigned char data_size)
{
	int i = 0;
	unsigned char lrc = 0x0;
	for (i=0; i < (data_size - 1); i++)
	{
		lrc ^= data[i];
	}
	return lrc;
}

int get_info(unsigned int *seq, unsigned char *info, unsigned int *info_size)
{
	unsigned char txdata[] = {0x12, 0xFF, 0x05, 0x00, 0xb1, 0x3b, 0x00, 0x84, 0xFF};
	unsigned int txdata_size = 9;
	unsigned char rxdata[MAX_FILE_DATA_SIZE] = {0x0, };
	unsigned int rxdata_size = 0x8A;
	int ret_size = 0;

	txdata[1] = ((*seq) << 6) & 0xFF;
	txdata[sizeof(txdata)-1] = __cal_lrc(txdata, sizeof(txdata));

	HAL_USLEEP(2 * 1000);
	ret_size = ese_hal_send(txdata, txdata_size);
	if (ret_size != txdata_size) {
		LOG_E("mismatch send size : %u, %d", txdata_size, ret_size);
	}

	ret_size = receive_data_with_polling(rxdata, rxdata_size);
	if (ret_size != rxdata_size) {
		LOG_E("failed to get_info (receive data fail)");
		return -1;
	}

	if (rxdata[0x87] != 0x90 && rxdata[0x88] != 0x00) {
		LOG_E("failed to get_info (FW get information fail)");
		LOG_E("return value : %x%x", rxdata[0x87], rxdata[0x88]);
		ADD_SEQUENCE(*seq);
		return -1;
	}

	if (memcpy(info, rxdata + 3, rxdata_size - 6) < 0) {
		LOG_E("failed to copy rx data");
		ADD_SEQUENCE(*seq);
		return -1;
	}
	*info_size = rxdata_size - 6;
	ADD_SEQUENCE(*seq);
	return 0;
}

int selfTest(unsigned int *seq, unsigned char test_cmd, unsigned char *selfTestRes, unsigned int *selfTestRes_size)
{
	unsigned char txdata[] = {0x12, 0xFF, 0x04, 0x00, 0xf1, 0xFF, 0x00, 0xFF};	// 0xFF is dynamic data
	unsigned int txdata_size = 8;
	unsigned char rxdata[MAX_FILE_DATA_SIZE] = {0x0, };
	unsigned int rxdata_size = 0x2E;
    unsigned int wtxResponse_size = 8;
    unsigned int wtxResponse_cnt = 0;
	unsigned int wtxTime = 0;
	int ret_size = 0;

	if (test_cmd > 0x7F || test_cmd < 0x01) {
		LOG_E("Wrong selfTest command");
		return -1;
	}

	if ((*seq) != 0 && (*seq) != 1) {
		LOG_E("Wrong sequnce format");
		return -1;
	}
	txdata[5] = (test_cmd) & 0xFF;
	txdata[1] = ((*seq) << 6) & 0xFF;
	txdata[sizeof(txdata)-1] = __cal_lrc(txdata, sizeof(txdata));

	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_NVM_CHECKSUM) != 0 ? 1 : 0;
	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_NVM_ACCESS) != 0 ? 1 : 0;
	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_RAM_ACCESS) != 0 ? 1 : 0;
	wtxResponse_cnt += (test_cmd & SELFTEST_CMD_ENC_CHECK) != 0 ? 1 : 0;

	HAL_USLEEP(2 * 1000);
	ret_size = ese_hal_send(txdata, txdata_size);
	if (ret_size != txdata_size) {
		LOG_E("mismatch send size : %u, %d", txdata_size, ret_size);
	}

	while(wtxResponse_cnt > 0) {
		ret_size = receive_data_with_polling(rxdata, wtxResponse_size);
	    if (ret_size != wtxResponse_size) {
		    LOG_E("failed to get wtxReqeust data");
		    return -1;
	    }
        if (rxdata[1] != 0xc3) {
            LOG_E("failed to wtxReqeust");
            return -1;
        }

		wtxTime = (rxdata[5] << 8) | rxdata[6];
        usleep(wtxTime * 1000);
		wtxResponse_cnt--;
	}
	ret_size = receive_data_with_polling(rxdata, rxdata_size);
	if (ret_size != rxdata_size) {
		LOG_E("failed to selfTest (receive data fail)");
		return -1;
	}
	if (rxdata[0x28] != 0x90 && rxdata[0x29] != 0x00) {
		LOG_E("failed to self_test");
		LOG_E("return value : %x%x", rxdata[0x28], rxdata[0x29]);
		ADD_SEQUENCE(*seq);
		return -1;
	}

	if (memcpy(selfTestRes, rxdata + 3, rxdata_size - 6) < 0) {
		LOG_E("failed to copy rx data");
		return -1;
	}

	*selfTestRes_size = rxdata_size - 6;
	ADD_SEQUENCE(*seq);
	return 0;
}

int main(void)
{
	char device_name[] = {0x61, 0x61, 0x72, 0x64, 0x76, 0x61, 0x72, 0x6B, 0x2D, 0x69, 0x32, 0x63, 0x00};
	unsigned char info[MAX_FILE_DATA_SIZE] = {0x00, };
	unsigned int info_size = MAX_FILE_DATA_SIZE;
    unsigned char selfTestRes[MAX_FILE_DATA_SIZE] = {0x00, };
	unsigned int selfTestRes_size = MAX_FILE_DATA_SIZE;
	unsigned int seq = 0;
	ese_info_t *version = NULL;
	char date[13] = { 0 };
	uint8_t hw_info[12] = {'S', '3', 0x00, 0x00, 0x00, 0x00, 0x00, '_', 'R', 'E', 'V', 0x00};

	if (ese_hal_open(device_name, 400) != 0) {
		LOG_E("failed to open");
		return -1;
	}

	if (boot_firmware() < 0) {
		return -1;
	} else {
		seq = 0;
	}
	LOG_I("Boot Firmware Success");

	if (get_info(&seq, info, &info_size) < 0) {
		return -1;
	}
	LOG_I("get fw information success");

	version = (ese_info_t *)info;
	__getHWInfo(version->chip , hw_info);

	// HW Info :: hw_info
	// FW_ver :: version->core.version
	// CRYPTO_ver :: version->crypto.version

	print_buffer("chip", version->chip, CHIP_INFO_SIZE);
	print_buffer("keyflag", version->key_flag, KEY_FLAG_INFO_SIZE);
	print_buffer("partition", version->partition, PARTITION_INFO_SIZE);
	print_buffer("BL", version->bootloader.version, IMG_VERSION_SIZE);
	date[IMG_DATE_SIZE] = '\0';
	memcpy(date, version->bootloader.build_date, IMG_DATE_SIZE);
	LOG_I("BL build date : %s", date);
	print_buffer("CORE", version->core.version, IMG_VERSION_SIZE);
	memcpy(date, version->core.build_date, IMG_DATE_SIZE);
	LOG_I("CORE build date : %s", date);
	print_buffer("CRYPTO", version->crypto.version, IMG_VERSION_SIZE);
	memcpy(date, version->crypto.build_date, IMG_DATE_SIZE);
	LOG_I("CRYPTO build date : %s", date);
	print_buffer("HW INFO", hw_info, sizeof(hw_info));

	// TEST_CHIP_SN
	if (selfTest(&seq, SELFTEST_CMD_TEST_CHIP_SN, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - TEST_CHIP_SN");
		return -1;
	}
	print_buffer("TEST_CHIP_SN", selfTestRes + 4, 12);
	// end of TEST_CHIP_SN

	// TIMER
	if (selfTest(&seq, SELFTEST_CMD_TIMER, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - TIMER");
		return -1;
	}
	print_buffer("TIMER", selfTestRes + 16, 4);
	// end of TIMER

	// PROVISION_AK
	if (selfTest(&seq, SELFTEST_CMD_PROVISION_AK, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - PROVISION_AK");
		return -1;
	}
	print_buffer("PROVISION_AK", selfTestRes + 20, 4);
	// end of PROVISION_AK

	// NVM_CHECKSUM
	if (selfTest(&seq, SELFTEST_CMD_NVM_CHECKSUM, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - NVM_CHECKSUM");
		return -1;
	}
	// end of NVM_CHECKSUM

	// NVM_ACCESS
	if (selfTest(&seq, SELFTEST_CMD_NVM_ACCESS, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - NVM_ACCESS");
		return -1;
	}
	// end of NVM_ACCESS

	// RAM_ACCESS
	if (selfTest(&seq, SELFTEST_CMD_RAM_ACCESS, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - RAM_ACCESS");
		return -1;
	}
	// end of RAM_ACCESS

	// ENC_CHECK
	if (selfTest(&seq, SELFTEST_CMD_ENC_CHECK, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - ENC_CHECK");
		return -1;
	}
	// end of ENC_CHECK

	// TEST_ALL
	if (selfTest(&seq, SELFTEST_CMD_TEST_ALL, selfTestRes, &selfTestRes_size) < 0) {
		LOG_I("failed to SelfTest - TEST ALL");
		return -1;
	}
	print_buffer("TEST_CHIP_SN", selfTestRes + 4, 12);
	print_buffer("TIMER", selfTestRes + 16, 4);
	print_buffer("PROVISION_AK", selfTestRes + 20, 4);
	// end of TEST_ALL

	if (ese_hal_close() != 0) {
		LOG_E("failed to close");
		return -1;
	}

	return 0;
}
-1;
	}
	return 0;
}
1;
	}
	return 0;
}
