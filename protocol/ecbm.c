#include "ecbm.h"

#include "framer7b.h"
#include "stdser.h"
#include "crc32.h"
#include "raiden.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#if ECBM_DEBUG_EN
#include <stdio.h>
#endif

#define _ECBM_PD_DIR_MASK		0b00010000
#define _ECBM_PD_TYP_MASK		0b00001111
#define _ECBM_PD_DIR_REQ		0b00010000
#define _ECBM_PD_DIR_ANSW		0b00000000
#define _ECBM_PD_TYP_WRITE		0b00000000
#define _ECBM_PD_TYP_READ		0b00000001
#define _ECBM_PD_TYP_ENCS		0b00000100
#define _ECBM_PD_TYP_ERR		0b00001000

void ecbm_init(
	Ecbm* ecbm,
	size_t id,
	int (*write)(size_t id, const uint8_t* data, size_t ndata),
	int (*read)(size_t id, uint8_t* buf, size_t bufsize),
	void (*sleep_ms)(uint32_t ms))
{
	size_t i;
	ecbm->id = id;
	ecbm->write = write;
	ecbm->read = read;
	ecbm->sleep_ms = sleep_ms;
	ecbm->timeout_ms = ECBM_DEF_TIMEOUT_MS;
	framer7b_reset(&ecbm->framer);
	for (i = 0; i < ECBM_MAX_ENC_SESSIONS; i++) {
		ecbm->enc_sessions[i].addr = 0;
	}
}

static int _ecbm_assert_answ(const uint8_t* data, size_t ndata, uint8_t addr, uint8_t pd_typ) {
	uint8_t data2 = data[2];
	if (ndata < 7) {
		return ECBM_ERR_INTEGRITY;
	}
	if ((data[2] & _ECBM_PD_DIR_MASK) != _ECBM_PD_DIR_ANSW) {
		return ECBM_ERR_INTEGRITY;
	}
	if ((data[2] & _ECBM_PD_TYP_MASK) == _ECBM_PD_TYP_ERR) {
		return -data[3];
	}
	if ((data[2] & _ECBM_PD_TYP_MASK) != pd_typ) {
		return ECBM_ERR_INTEGRITY;
	}
	if (data[1] != addr) {
		return ECBM_ERR_INTEGRITY;
	}
	if (crc32(data, ndata - (4 + data[0])) != stdser_g32(&data[ndata - (4 + data[0])])) {
		return ECBM_ERR_INTEGRITY;
	}
	if (pd_typ == _ECBM_PD_TYP_WRITE) {
		return 0;
	}
	else {
		return (int)(ndata - (7 + data[0]));
	}
}

static int _ecbm_transfer(Ecbm* ecbm, size_t ndata, uint8_t addr) {
	uint32_t elapsed_ms;
	int rc;
	uint8_t buf[1];
	
	while (ecbm->read(ecbm->id, buf, 1) > 0) {}
	rc = ecbm->write(ecbm->id, framer7b_get_send_buf(&ecbm->framer), ndata);
	if (rc < 0) {
		return ECBM_ERR_WRITE;
	}
	framer7b_reset(&ecbm->framer);
	if (addr == ECBM_ADDR_BROADCAST) {
		return 0;
	}
	elapsed_ms = 0;
	uint8_t is_rx = 0;
	while (1) {
		rc = ecbm->read(ecbm->id, buf, 1);
		if (rc > 0) {
			is_rx = 1;
			elapsed_ms = 0;
			rc = framer7b_push(&ecbm->framer, buf[0]);
			if (rc > 0) {
				break;
			}
			else if (rc < 0) {
				return ECBM_ERR_INTEGRITY;
			}
		}
		else if (rc == 0) {
			ecbm->sleep_ms(10);
			elapsed_ms += 11;
			if (elapsed_ms > ecbm->timeout_ms) {
				if (is_rx) {
					return ECBM_ERR_INTEGRITY;
				}
				else {
					return ECBM_ERR_TIMEOUT;
				}
			}
		}
		else if (rc < 0) {
			return ECBM_ERR_READ;
		}
	}

	return rc;
}

int ecbm_write(Ecbm* ecbm, uint8_t addr, uint16_t sig, const uint8_t* data, size_t ndata) {
	uint8_t* buf;
	uint8_t* key;
	int rc;
	uint8_t nfill;

	key = ecbm_get_session_key(ecbm, addr);
	buf = framer7b_get_write_buf(&ecbm->framer);
	nfill = key == NULL ? 0 : 8 - ((ndata + 9) % 8);
	buf[0] = nfill;
	buf[1] = addr;
	buf[2] = _ECBM_PD_DIR_REQ | _ECBM_PD_TYP_WRITE;
	stdser_s16(sig, &buf[3]);
	memcpy(&buf[5], data, ndata);
	stdser_s32(crc32(buf, ndata + 5), &buf[ndata + 5]);
	if (key != NULL) {
		memset(&buf[ndata + 9], ECBM_ENC_FILL_BYTE, nfill);
		raiden_encode_buf(key, buf, ndata + 9 + nfill);
	}

	rc = framer7b_make(&ecbm->framer, ndata + 9 + nfill);
	if (rc <= 0) {
		return ECBM_ERR_ENCODE;
	}
	rc = _ecbm_transfer(ecbm, rc, addr);
	if (rc < 0) {
		return rc;
	}
	if (rc == 0) {
		return ECBM_OK;
	}
	buf = framer7b_get_read_buf(&ecbm->framer);
	if (key != NULL) {
		if (rc % 8 != 0) {
			return ECBM_ERR_INTEGRITY;
		}
		raiden_decode_buf(key, buf, rc);
	}
	return _ecbm_assert_answ(buf, rc, addr, _ECBM_PD_TYP_WRITE);
}

static int _ecbm_read(Ecbm* ecbm, uint8_t addr, uint16_t sig, uint8_t* buffer, size_t bufsize, uint8_t pd_typ) {
	uint8_t* buf;
	uint8_t* key;
	int rc;
	uint8_t nfill;

	key = ecbm_get_session_key(ecbm, addr);
	buf = framer7b_get_write_buf(&ecbm->framer);
	nfill = key == NULL ? 0 : 7;
	buf[0] = nfill;
	buf[1] = addr;
	buf[2] = _ECBM_PD_DIR_REQ | pd_typ;
	stdser_s16(sig, &buf[3]);
	stdser_s32(crc32(buf, 5), &buf[5]);

	key = ecbm_get_session_key(ecbm, addr);
	if (key != NULL) {
		memset(&buf[9], ECBM_ENC_FILL_BYTE, 7);
#if ECBM_DEBUG_EN
		printf("[ECBM:READ] buf before enc: {");
		for (size_t i = 0; i < 16; i++) {
			printf("%02X ", buf[i]);
		}
		printf("}\n");
#endif
		raiden_encode_buf(key, buf, 16);
#if ECBM_DEBUG_EN
		printf("[ECBM:READ] buf after enc: {");
		for (size_t i = 0; i < 16; i++) {
			printf("%02X ", buf[i]);
		}
		printf("}\n");
#endif
	}
#if ECBM_DEBUG_EN
	else {
		printf("[ECBM:READ] buf without enc: {");
		for (size_t i = 0; i < 16; i++) {
			printf("%02X ", buf[i]);
		}
		printf("}\n");
	}
#endif
	rc = framer7b_make(&ecbm->framer, 9 + nfill);
	if (rc <= 0) {
		return ECBM_ERR_ENCODE;
	}
#if ECBM_DEBUG_EN
	printf("[ECBM:READ] bytes to send: %i\n", rc);
#endif
	rc = _ecbm_transfer(ecbm, rc, addr);
	if (rc < 0) {
		return rc;
	}
	buf = framer7b_get_read_buf(&ecbm->framer);
	if (key != NULL) {
		if (rc % 8 != 0) {
			return ECBM_ERR_INTEGRITY;
		}
		raiden_decode_buf(key, buf, rc);
	}
	rc = _ecbm_assert_answ(buf, rc, addr, _ECBM_PD_TYP_READ);
	if (rc < 0) {
		return rc;
	}
	if (rc > bufsize) {
		return ECBM_ERR_OVERFLOW;
	}
	memcpy(buffer, &buf[3], rc);
	return rc;
}

int ecbm_read(Ecbm* ecbm, uint8_t addr, uint16_t sig, uint8_t* buffer, size_t bufsize) {
	return _ecbm_read(ecbm, addr, sig, buffer, bufsize, _ECBM_PD_TYP_READ);
}

static int _ecbm_read_info(Ecbm* ecbm, uint8_t addr, EcbmDeviceInfo* info_buf, uint16_t sig) {
	uint8_t buf[sizeof(EcbmDeviceInfo)];
	int rc;
	rc = ecbm_read(ecbm, addr, sig, buf, sizeof(buf));
	if (rc < 0) {
		return rc;
	}
	if (rc < 4) {
		return ECBM_ERR_INTEGRITY;
	}
	memset(info_buf, 0, sizeof(EcbmDeviceInfo));
	rc = (int)stdser_gstr(buf, info_buf->name, 32);
	info_buf->version[0] = buf[rc];
	info_buf->version[1] = buf[rc + 1];
	info_buf->version[2] = buf[rc + 2];

	return 0;
}

int ecbm_read_info(Ecbm* ecbm, uint8_t addr, EcbmDeviceInfo* info_buf) {
	return _ecbm_read_info(ecbm, addr, info_buf, ECBM_SIG_INFO);
}

int ecbm_firmware_info(Ecbm* ecbm, uint8_t addr, EcbmDeviceInfo* info_buf) {
	return _ecbm_read_info(ecbm, addr, info_buf, ECBM_SIG_BOOT_FW_INFO);
}

int ecbm_pick(Ecbm* ecbm, uint8_t addr) {
	return ecbm_write(ecbm, addr, ECBM_SIG_PICK, NULL, 0);
}

void ecbm_reset(Ecbm* ecbm, uint8_t addr) {
	ecbm_write(ecbm, addr, ECBM_SIG_RESET, NULL, 0);
	ecbm_close_enc_session(ecbm, addr);
}

void ecbm_reset_bus(Ecbm* ecbm) {
	ecbm_close_all_enc_session(ecbm);
	ecbm_write(ecbm, ECBM_ADDR_BROADCAST, ECBM_SIG_RESET, NULL, 0);
}

void ecbm_set_timeout(Ecbm* ecbm, uint16_t timeout_ms) {
	ecbm->timeout_ms = timeout_ms;
}

uint16_t ecbm_get_timeout(const Ecbm* ecbm) {
	return ecbm->timeout_ms;
}

int ecbm_begin_upload_firmware(Ecbm* ecbm, uint8_t addr, const EcbmDeviceInfo* fw_info, const uint8_t test_phrase[16], uint16_t timeout_ms) {
	uint8_t buf[32 + 3 + 16];
	size_t ptr;
	int rc;
	uint16_t prev_timeout;
	ptr = stdser_sstr(fw_info->name, buf, 32);
	buf[ptr] = fw_info->version[0];
	buf[ptr + 1] = fw_info->version[1];
	buf[ptr + 2] = fw_info->version[2];
	memcpy(&buf[ptr + 3], test_phrase, 16);
	prev_timeout = ecbm->timeout_ms;
	ecbm->timeout_ms = timeout_ms;
	rc = ecbm_write(ecbm, addr, ECBM_SIG_BOOT_BEGIN, buf, ptr + 19);
	ecbm->timeout_ms = prev_timeout;
	return rc;
}

int ecbm_write_firmware_block(Ecbm* ecbm, uint8_t addr, const uint8_t* data, size_t ndata, size_t offset, uint16_t timeout_ms) {
	uint16_t prev_timeout;
	int rc;
	uint8_t* buf = malloc(ndata + 4);
	if (buf == NULL) {
		return ECBM_ERR_NO_MEM;
	}
	stdser_s32((uint32_t)offset, buf);
	memcpy(&buf[4], data, ndata);
	prev_timeout = ecbm->timeout_ms;
	ecbm->timeout_ms = timeout_ms;
	rc = ecbm_write(ecbm, addr, ECBM_SIG_BOOT_WRITE, buf, ndata + 4);
	ecbm->timeout_ms = prev_timeout;
	free(buf);
	return rc;
}

int ecbm_end_upload_firmware(Ecbm* ecbm, uint8_t addr, uint32_t checksum, size_t fw_len, uint16_t timeout_ms) {
	uint8_t buf[8];
	int rc;
	uint16_t prev_timeout;
	stdser_s32(checksum, buf);
	stdser_s32((uint32_t)fw_len, &buf[4]);
	prev_timeout = ecbm->timeout_ms;
	ecbm->timeout_ms = timeout_ms;
	rc = ecbm_write(ecbm, addr, ECBM_SIG_BOOT_END, buf, 8);
	ecbm->timeout_ms = prev_timeout;
	return rc;
}

int ecbm_firmware_checksum(Ecbm* ecbm, uint8_t addr, uint32_t* checksum_buf) {
	uint8_t buf[4];
	int rc;
	rc = ecbm_read(ecbm, addr, ECBM_SIG_BOOT_CHECKSUM, buf, 4);
	if (rc < 0) {
		return rc;
	}
	if (rc != 4) {
		return ECBM_ERR_INTEGRITY;
	}
	*checksum_buf = stdser_g32(buf);
	return 0;
}

int ecbm_begin_enc_session(Ecbm* ecbm, uint8_t addr, const uint8_t base_key[16]) {
	int rc;
	uint8_t buf[16];
	size_t i;

	ecbm_close_enc_session(ecbm, addr);
	rc = _ecbm_read(ecbm, addr, 0, buf, sizeof(buf), _ECBM_PD_TYP_ENCS);
	if (rc < 0) {
		return rc;
	}
	if (rc != 16) {
		return ECBM_ERR_INTEGRITY;
	}
	for (i = 0; i < ECBM_MAX_ENC_SESSIONS; i++) {
		if (ecbm->enc_sessions[i].addr == 0) {
			ecbm->enc_sessions[i].addr = addr;
			raiden_decode(base_key, buf, ecbm->enc_sessions[i].key, 16);
			return ECBM_OK;
		}
	}

	return ECBM_ERR_OVERFLOW;
}

int ecbm_close_enc_session(Ecbm* ecbm, uint8_t addr) {
	size_t i;
	for (i = 0; i < ECBM_MAX_ENC_SESSIONS; i++) {
		if (ecbm->enc_sessions[i].addr == addr) {
			ecbm->enc_sessions[i].addr = 0;
			return ECBM_OK;
		}
	}
	return -1;
}

int ecbm_close_all_enc_session(Ecbm* ecbm) {
	size_t i;
	int cnt = 0;
	for (i = 0; i < ECBM_MAX_ENC_SESSIONS; i++) {
		if (ecbm->enc_sessions[i].addr != 0) {
			ecbm->enc_sessions[i].addr = 0;
			cnt++;
		}
	}
	return cnt;
}

uint8_t* ecbm_get_session_key(Ecbm* ecbm, uint8_t addr) {
	size_t i;
	if (addr == 0) {
		return NULL;
	}
	for (i = 0; i < ECBM_MAX_ENC_SESSIONS; i++) {
		if (ecbm->enc_sessions[i].addr == addr) {
			return ecbm->enc_sessions[i].key;
		}
	}
	return NULL;
}

int ecbm_set_new_auth_key(Ecbm* ecbm, uint8_t addr, const uint8_t new_key[16]) {
	return ecbm_write(ecbm, addr, ECBM_SIG_AKEY, new_key, 16);
}
