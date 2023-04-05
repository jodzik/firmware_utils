#pragma once

#include "ecbm.h"

#include <cstdlib>
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <optional>

//using namespace std;

struct FirmwareInfo {
	std::string name;
	std:: array<uint8_t, 3> version;
	uint32_t checksum;
};

class BootProt
{
public:
	BootProt(Ecbm* ecbm, uint8_t addr, const std::array<uint8_t, 16> auth_key);
	~BootProt();

	void upload_firmware(const FirmwareInfo& info, const std::array<uint8_t, 16>& test_phrase, const std::vector<uint8_t>& data, size_t blocksize = 256);
	void pick();
	FirmwareInfo get_firmware_info();
	void set_new_auth_key(const std::array<uint8_t, 16> new_auth_key);

private:
	uint8_t _addr;
	Ecbm* _ecbm;
};
