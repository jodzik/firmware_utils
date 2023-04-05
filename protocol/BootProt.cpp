#include "BootProt.hpp"
#include "ecbm.h"

#include <vector>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <exception>
#include <optional>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>

#define BOOTPROT_DEBUG_EN	1

using namespace std;

BootProt::BootProt(Ecbm* ecbm, uint8_t addr, const array<uint8_t, 16> auth_key) : _addr(addr), _ecbm(ecbm) {
	int rc;
#if BOOTPROT_DEBUG_EN
	cout << "static auth key: ";
	for (const auto& v : auth_key) {
		printf("%02X ", v);
	}
	cout << endl;
#endif
	rc = ecbm_begin_enc_session(_ecbm, _addr, auth_key.data());
	if (rc < 0) {
		throw runtime_error("fail to begin pre-reset encrypted session: " + to_string(rc));
	}
	ecbm_reset(_ecbm, _addr);
	this_thread::sleep_for(chrono::milliseconds(500));
	rc = ecbm_begin_enc_session(_ecbm, _addr, auth_key.data());
	if (rc < 0) {
		throw runtime_error("fail to begin encrypted session: " + to_string(rc));
	}
	EcbmDeviceInfo boot_info = { 0 };
	rc = ecbm_read_info(_ecbm, _addr, &boot_info);
	if (rc < 0) {
		throw runtime_error("fail to read bootloader info: " + to_string(rc));
	}
	cout << "bootloader info: name: " << boot_info.name << ", version: " << (int)boot_info.version[0] << "." << (int)boot_info.version[1] << "." << (int)boot_info.version[2] << endl;
}

BootProt::~BootProt() {
	
}

void BootProt::upload_firmware(const FirmwareInfo& info, const array<uint8_t, 16>& test_phrase, const vector<uint8_t>& data, size_t blocksize) {
	if (data.size() == 0) {
		throw runtime_error("firmware is empty");
	}
	if (data.size() % 8 != 0) {
		throw runtime_error("firmware length must be multiple at 8, but given: " + to_string(data.size()));
	}
	cout << "send firmware info.." << endl;
	EcbmDeviceInfo fw_info = {0};
	#if defined(__MINGW32__) || defined(_WIN32)
	strcpy_s(fw_info.name, 32, info.name.c_str());
	#else
	strncpy(fw_info.name, info.name.c_str(), 32);
	#endif
	memcpy(fw_info.version, info.version.data(), 3);
	int rc = ecbm_begin_upload_firmware(_ecbm, _addr, &fw_info, test_phrase.data(), 5000);
	if (rc < 0) {
		throw runtime_error("fail to begin upload firmware: " + to_string(rc));
	}

	cout << "upload.." << endl;
	size_t ptr = 0;
	size_t rem = data.size();
	size_t cur;
	int tries = 0;
	while (ptr < rem) {
		cout << (ptr * 100) / rem << "%" << endl;
		if (rem - ptr > blocksize) {
			cur = blocksize;
		}
		else {
			cur = rem - ptr;
		}
		trie:
		rc = ecbm_write_firmware_block(_ecbm, _addr, &data.data()[ptr], cur, ptr, 2500);
		if (rc < 0) {
			tries++;
			if (tries > 5) {
				throw runtime_error("fail to write firmware block: " + to_string(rc));
			}
			else {
#if BOOTPROT_DEBUG_EN
				cout << "fail to write fw block: " << rc << endl;
#endif
				this_thread::sleep_for(chrono::milliseconds(100));
				goto trie;
			}
		}
		tries = 0;
		ptr += cur;
	}

	cout << "verify.." << endl;
	rc = ecbm_end_upload_firmware(_ecbm, _addr, info.checksum, data.size(), 5000);
	if (rc < 0) {
		throw runtime_error("fail to terminate firmware upload: " + to_string(rc));
	}
	cout << "upload and verify complete." << endl;
}

void BootProt::pick() {
	int rc = ecbm_pick(_ecbm, _addr);
	if (rc < 0) {
		throw runtime_error("fail to pick device: " + to_string(rc));
	}
}

FirmwareInfo BootProt::get_firmware_info() {
	EcbmDeviceInfo info = {0};
	int rc = ecbm_firmware_info(_ecbm, _addr, &info);
	if (rc < 0) {
		throw runtime_error("fail to read device info: " + to_string(rc));
	}
	uint32_t checksum = 0;
	rc = ecbm_firmware_checksum(_ecbm, _addr, &checksum);
	if (rc < 0) {
		throw runtime_error("fail to read firmware checksum: " + to_string(rc));
	}
	return FirmwareInfo {
		.name = string(info.name),
		.version = {info.version[0], info.version[1], info.version[2]},
		.checksum = checksum
	};
}

void BootProt::set_new_auth_key(const array<uint8_t, 16> new_auth_key) {
	int rc = ecbm_set_new_auth_key(_ecbm, _addr, new_auth_key.data());
	if (rc < 0) {
		throw runtime_error("fail set new auth key: " + to_string(rc));
	}
}
