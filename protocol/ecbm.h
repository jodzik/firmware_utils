#ifndef ECBM
#define ECBM

#ifdef __cplusplus
extern "C" {
#endif

#include "framer7b.h"

#include <stdlib.h>
#include <stdint.h>

#define ECBM_DEBUG_EN			0
#define ECBM_DEF_TIMEOUT_MS		250
#define ECBM_ENC_FILL_BYTE		0x5A
#define ECBM_MAX_ENC_SESSIONS	8

#define ECBM_OK				0
#define _ECBM_ERRB_APP		-1
#define _ECBM_ERRE_APP		-255
#define _ECBM_ERRB_INT		-256
#define _ECBM_ERRE_INT		-511
#define _ECBM_ERRB_BUS		-512
#define _ECBM_ERRE_BUS		-1023

#define ECBM_IS_APP_ERR(error_code)		(error_code <= _ECBM_ERRB_APP && error_code >= _ECBM_ERRE_APP)
#define ECBM_IS_INT_ERR(error_code)		(error_code <= _ECBM_ERRB_INT && error_code >= _ECBM_ERRE_INT)
#define ECBM_IS_BUS_ERR(error_code)		(error_code <= _ECBM_ERRB_BUS && error_code >= _ECBM_ERRE_BUS)

#define ECBM_ERR_NO_SIG					-1
#define ECBM_ERR_INTERNAL				-4
#define ECBM_ERR_BOOT_INC_CHECKSUM		-16
#define ECBM_ERR_BOOT_INC_KEY			-17
#define ECBM_ERR_NO_ENC					-24
#define ECBM_ERR_MUST_ENC				-25

#define ECBM_ERR_READ			-256
#define ECBM_ERR_WRITE			-258
#define ECBM_ERR_OVERFLOW		-260
#define ECBM_ERR_NO_MEM			-262
#define ECBM_ERR_INC_ARG		-300
#define ECBM_ERR_ENCODE			-310

#define ECBM_ERR_TIMEOUT		-512
#define ECBM_ERR_INTEGRITY		-520

#define	ECBM_ADDR_BROADCAST		0

#define ECBM_SIG_RESET			0
#define ECBM_SIG_INFO			1
#define ECBM_SIG_PICK			15
#define ECBM_SIG_BOOT_BEGIN		16
#define ECBM_SIG_BOOT_END		17
#define ECBM_SIG_BOOT_CHECKSUM	18
#define ECBM_SIG_BOOT_WRITE		20
#define ECBM_SIG_BOOT_FW_INFO	22
#define ECBM_SIG_AKEY			24

typedef struct EcbmDeviceInfo {
	char name[32];
	uint8_t version[3];
} EcbmDeviceInfo;

typedef struct EcbmEncSession {
	uint8_t addr;
	uint8_t key[16];
} EcbmEncSession;

typedef struct Ecbm {
	size_t id;
	int (*write)(size_t id, const uint8_t* data, size_t ndata);
	int (*read)(size_t id, uint8_t* buf, size_t bufsize);
	void (*sleep_ms)(uint32_t ms);
	Framer7b framer;
	uint16_t timeout_ms;
	EcbmEncSession enc_sessions[ECBM_MAX_ENC_SESSIONS];
} Ecbm;

void ecbm_init(
	Ecbm* ecbm,
	size_t id,
	int (*write)(size_t id, const uint8_t* data, size_t ndata),
	int (*read)(size_t id, uint8_t* buf, size_t bufsize),
	void (*sleep_ms)(uint32_t ms)
);

int ecbm_write(Ecbm* ecbm, uint8_t addr, uint16_t sig, const uint8_t* data, size_t ndata);
int ecbm_read(Ecbm* ecbm, uint8_t addr, uint16_t sig, uint8_t* buf, size_t bufsize);
int ecbm_read_info(Ecbm* ecbm, uint8_t addr, EcbmDeviceInfo* info_buf);
int ecbm_pick(Ecbm* ecbm, uint8_t addr);
void ecbm_reset(Ecbm* ecbm, uint8_t addr);
void ecbm_reset_bus(Ecbm* ecbm);
void ecbm_set_timeout(Ecbm* ecbm, uint16_t timeout_ms);
uint16_t ecbm_get_timeout(const Ecbm* ecbm);

int ecbm_begin_upload_firmware(Ecbm* ecbm, uint8_t addr, const EcbmDeviceInfo* fw_info, const uint8_t test_phrase[16], uint16_t timeout_ms);
int ecbm_write_firmware_block(Ecbm* ecbm, uint8_t addr, const uint8_t* data, size_t ndata, size_t offset, uint16_t timeout_ms);
int ecbm_end_upload_firmware(Ecbm* ecbm, uint8_t addr, uint32_t checksum, size_t fw_len, uint16_t timeout_ms);
int ecbm_firmware_checksum(Ecbm* ecbm, uint8_t addr, uint32_t* checksum_buf);
int ecbm_firmware_info(Ecbm* ecbm, uint8_t addr, EcbmDeviceInfo* info_buf);

void ecbm_set_timeout(Ecbm* ecbm, uint16_t timeout_ms);

int ecbm_begin_enc_session(Ecbm* ecbm, uint8_t addr, const uint8_t base_key[16]);
int ecbm_close_enc_session(Ecbm* ecbm, uint8_t addr);
int ecbm_close_all_enc_session(Ecbm* ecbm);
uint8_t* ecbm_get_session_key(Ecbm* ecbm, uint8_t addr);
int ecbm_set_new_auth_key(Ecbm* ecbm, uint8_t addr, const uint8_t new_key[16]);

#ifdef __cplusplus
}
#endif

#endif // !ECBM
