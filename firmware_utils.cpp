// firmware_utils.cpp: определяет точку входа для приложения.
//

#include "firmware_utils.h"
#include "structopt.hpp"
#include "msgpack.hpp"
#include "protocol/raiden.h"
#include "protocol/crc32.h"
#include "protocol/BootProt.hpp"
#include "protocol/stdser.h"
#include "protocol/ecbm.h"
#include "protocol/xserial.hpp"

#include <fstream>
#include <iterator>
#include <algorithm>
#include <cstdint>
#include <random>
#include <cstring>
#include <thread>

#define DEF_ADDR		1
#define DEBUG_EN		1
#define DEBUG_IOECBM_EN	0

using namespace std;

struct Arguments {
	struct Ports : structopt::sub_command {
		optional<bool> verbose = false;
	};

	struct Encrypt : structopt::sub_command {
		string file;
		string firmware_name;
		string firmware_version;
		string key;
		string test_phrase;
		optional<int> filler = 1;
	};

	struct Upload : structopt::sub_command {
		string file;
		int pincode = 0;
		optional<int> port;
	};

	struct GetInfo : structopt::sub_command {
		int pincode = 0;
		optional<int> port;
	};

	struct SetPin : structopt::sub_command {
		int pincode = 0;
		optional<int> port;
		int new_pincode = 0;
	};

	struct PinToKey : structopt::sub_command {
		int pincode = 0;
	};

	struct GenKey : structopt::sub_command {
		enum class Fmt {
			insep,
			c,
			both
		};
		optional<Fmt> fmt = Fmt::both;
	};

	struct EcbmCmd : structopt::sub_command {
		struct Wu16 : structopt::sub_command {
			uint16_t addr = 0;
			uint16_t sig = 0;
			uint16_t data = 0;
			optional<int> pin;
		};

		optional<int> port;

		Wu16 wu16;
	};

	Ports ports;
	Encrypt encrypt;
	Upload upload;
	GetInfo info;
	SetPin set_pincode;
	PinToKey pintokey;
	GenKey genkey;
	EcbmCmd ecbm;
};

STRUCTOPT(Arguments::Ports, verbose);
STRUCTOPT(Arguments::Encrypt, file, firmware_name, firmware_version, key, test_phrase, filler);
STRUCTOPT(Arguments::Upload, file, pincode, port);
STRUCTOPT(Arguments::GetInfo, pincode, port);
STRUCTOPT(Arguments::SetPin, pincode, port, new_pincode);
STRUCTOPT(Arguments::PinToKey, pincode);
STRUCTOPT(Arguments::GenKey, fmt);

STRUCTOPT(Arguments::EcbmCmd::Wu16, pin, addr, sig, data);
STRUCTOPT(Arguments::EcbmCmd, port, wu16);

STRUCTOPT(Arguments, ports, encrypt, upload, info, set_pincode, pintokey, genkey, ecbm);

struct FirmwareFile {
	string name;
	vector<uint8_t> version;
	uint32_t checksum;
	vector<uint8_t> test_phrase;
	vector<uint8_t> data;

	template<class T>
	void pack(T& pack) {
		pack(name, version, checksum, test_phrase, data);
	}
};

array<uint8_t, 16> pin_to_key(int pin) {
	array<uint8_t, 16> key;
	array<uint8_t, 3> digits;
	digits[0] = (uint8_t)(pin % 100);
	digits[1] = (uint8_t)((pin % 10000) / 100);
	digits[2] = (uint8_t)(pin / 10000);
	uint32_t crc = (uint32_t)(pin + 1);
	for (auto i = 0; i < 4; i++) {
		crc = crc32_dync(crc, digits[0]);
		crc = crc32_dync(crc, digits[1]);
		crc = crc32_dync(crc, digits[2]);
		stdser_s32(crc, &key.data()[i * 4]);
	}
	return key;
}

xserial::ComPort* _com;

static int _write(size_t id, const uint8_t* data, size_t ndata) {
#if DEBUG_IOECBM_EN
	cout << "[ECBM] write " << ndata << " bytes: " << endl;
	cout << "\t";
	for (auto i = 0; i < ndata; i++) {
		printf("%02X ", data[i]);
	}
	cout << endl;
#endif
	if (_com->write((char*)data, (unsigned long)ndata)) {
		return 0;
	}
	else {
		return -1;
	}
}

static int _read(size_t id, uint8_t* buf, size_t bufsize) {
	if (_com->bytesToRead() > 0) {
		return _com->read((char*)buf, (unsigned long)bufsize);
	}
	else {
		return 0;
	}

}

static void _sleep_ms(uint32_t ms) {
	this_thread::sleep_for(chrono::milliseconds(ms));
}

class IoEcbm {
public:


	IoEcbm(optional<int> numport) {
		if (numport.has_value()) {
			_com = new xserial::ComPort(numport.value(), 115200, xserial::ComPort::COM_PORT_NOPARITY, 8, xserial::ComPort::COM_PORT_ONESTOPBIT);
		}
		else {
			_com = new xserial::ComPort(115200, xserial::ComPort::COM_PORT_NOPARITY, 8, xserial::ComPort::COM_PORT_ONESTOPBIT);
		}
		if (!_com->getStateComPort()) {
			throw runtime_error("fail to open com port");
		}
		ecbm_init(&_ecbm, 1, _write, _read, _sleep_ms);
	};

	Ecbm* instance() {
		return &_ecbm;
	}

	~IoEcbm() {

	}
private:
	Ecbm _ecbm;
};

void print_fw_info(FirmwareInfo& info) {
	if (info.checksum == 0) {
		cout << "no app" << endl;
	}
	else {
		cout << "name: " << info.name << endl;
		cout << "version: " << (int)info.version[0] << "." << (int)info.version[1] << "." << (int)info.version[2] << endl;
		cout << "checksum: " << info.checksum << endl;
	}
}

void print_key(const uint8_t* key) {
	cout << "[";
	for (auto i = 0; i < 16; i++) {
		printf("%02X ", key[i]);
	}
	cout << "]" << endl;
}

void print_test_phrase(const array<uint8_t, 16>& phrase) {
	for (const auto& v : phrase) {
		cout << v;
	}
	cout << endl;
}

int main(int argc, char** argv) {
	try {
		auto opt = structopt::app("fwu", "0.0.1").parse<Arguments>(argc, argv);
		if (opt.encrypt.has_value()) {
			auto file_repr = opt.encrypt.file.substr(opt.encrypt.file.rfind('.'));
			if (file_repr != ".bin") {
				throw runtime_error("firmware file must have '.bin' format, not '" + file_repr + "'");
			}
			
			int ver_buf[3];
			#if defined(__MINGW32__) || defined(_WIN32)
			if (sscanf_s(opt.encrypt.firmware_version.c_str(), "%i.%i.%i", &ver_buf[0], &ver_buf[1], &ver_buf[2]) != 3) {
				throw runtime_error("version must be as three numbers between 0..255 delemited by dot, example - '0.5.12'");
			}
			#else
			if (sscanf(opt.encrypt.firmware_version.c_str(), "%i.%i.%i", &ver_buf[0], &ver_buf[1], &ver_buf[2]) != 3) {
				throw runtime_error("version must be as three numbers between 0..255 delemited by dot, example - '0.5.12'");
			}
			#endif
			vector<uint8_t> version;
			for (size_t i = 0; i < 3; i++) {
				if (ver_buf[i] > 255 || ver_buf[i] < 0) {
					version.push_back(255);
				}
				else {
					version.push_back((uint8_t)ver_buf[i]);
				}
			}

			if (opt.encrypt.key.length() != 32) {
				throw runtime_error("key must be written as 32 HEX symbols, example - '00112233445566778899AABBCCDDEEFF'");
			}
			array<uint8_t, 16> key;
			char buf[3] = {0};
			for (size_t i = 0; i < 16; i++) {
				memcpy(buf, &opt.encrypt.key.c_str()[i*2], 2);
				key[i] = (uint8_t)std::strtoul(buf, NULL, 16);
			}

			if (opt.encrypt.test_phrase.length() != 16) {
				throw runtime_error("test_phrase must be written as 16 ASCII symbols");
			}
			vector<uint8_t> test_phrase;
			uint8_t test_phrase_buf[16];
			raiden_encode(key.data(), (const uint8_t*)opt.encrypt.test_phrase.c_str(), test_phrase_buf, 16);
			for (size_t i = 0; i < 16; i++) {
				test_phrase.push_back(test_phrase_buf[i]);
			}

			uint8_t filler = 0xFF;
			if (opt.encrypt.filler == 0) {
				filler = 0;
			}

			ifstream fw_file(opt.encrypt.file, ios_base::binary);
			if (!fw_file.is_open()) {
				throw runtime_error("fail to open firmware file");
			}
			fw_file.unsetf(std::ios::skipws);
			istream_iterator<uint8_t> start(fw_file), end;
			vector<uint8_t> data(start, end);
			cout << "initial firmware size: " << data.size() << endl;
			while (data.size() % 8 != 0) {
				data.push_back(filler);
			}
			auto crc = crc32(data.data(), data.size());
			raiden_encode_buf(key.data(), data.data(), data.size());
			FirmwareFile fw = {
				.name = opt.encrypt.firmware_name,
				.version = version,
				.checksum = crc,
				.test_phrase = test_phrase,
				.data = data
			};
			auto raw_data = msgpack::pack(fw);
			ofstream out_file(opt.encrypt.file + ".enc", ios_base::binary | ios_base::trunc);
			for (const auto& v : raw_data) {
				out_file << v;
			}
			cout << "complete, new fw size: " << fw.data.size() << endl;
			printf("checksum: %04X\n", crc);
			cout << "encypted file was be written as '" << opt.encrypt.file + ".enc" << "', " << raw_data.size() << " bytes" << endl;
		}
		else if (opt.upload.has_value()) {
			auto key = pin_to_key(opt.upload.pincode);
			ifstream fw_file(opt.upload.file, ios_base::binary);
			if (!fw_file.is_open()) {
				throw runtime_error("fail to open firmware file");
			}
			fw_file.unsetf(std::ios::skipws);
			istream_iterator<uint8_t> start(fw_file), end;
			vector<uint8_t> raw_data(start, end);
			cout << raw_data.size() << " bytes read from file, unpack.." << endl;
			auto fw = msgpack::unpack<FirmwareFile>(raw_data);
			FirmwareInfo fw_info = {
				.name = fw.name,
				.version = {fw.version[0], fw.version[1], fw.version[2]},
				.checksum = fw.checksum
			};
			cout << "firmware info:" << endl;
			print_fw_info(fw_info);
			cout << "size: " << fw.data.size() << endl << endl;
			cout << "continue? y/n ?" << endl;
			char decision;
			cin >> decision;
			if (decision == 'y') {
				try {
					array<uint8_t, 16> test_phrase;
					memcpy(test_phrase.data(), fw.test_phrase.data(), 16);
					IoEcbm io_ecbm(opt.upload.port);
					BootProt dev(io_ecbm.instance(), DEF_ADDR, key);
					dev.upload_firmware(fw_info, test_phrase, fw.data);
					cout << "complete." << endl;
				}
				catch (const std::exception& e) {
					cout << e.what() << endl;
				}
			}
			else {
				cout << "exit" << endl;
			}
		}
		else if (opt.ports.has_value()) {
			xserial::ComPort com;
			vector<string> ports;
			com.getListSerialPorts(ports);
			for (auto p : ports) {
				cout << p << endl;
			}
		}
		else if (opt.info.has_value()) {
			auto key = pin_to_key(opt.info.pincode);
			IoEcbm io_ecbm(opt.info.port);
			BootProt dev(io_ecbm.instance(), DEF_ADDR, key);
			auto info = dev.get_firmware_info();
			cout << "app info:" << endl;
			print_fw_info(info);
		}
		else if (opt.set_pincode.has_value()) {
			IoEcbm io_ecbm(opt.set_pincode.port);
			BootProt dev(io_ecbm.instance(), DEF_ADDR, pin_to_key(opt.set_pincode.pincode));
			dev.set_new_auth_key(pin_to_key(opt.set_pincode.new_pincode));
		}
		else if (opt.pintokey.has_value()) {
			auto key = pin_to_key(opt.pintokey.pincode);
			cout << "{";
			for (auto i = 0; i < key.size(); i++) {
				#if defined(__MINGW32__) || defined(_WIN32)
				printf_s("0x%02X", key[i]);
				#else
				printf("0x%02X", key[i]);
				#endif
				if (i < key.size() - 1) {
					// not last byte
					cout << ", ";
				}
			}
			cout << "}" << endl;
		}
		else if (opt.genkey.has_value()) {
			char charset[] = "0123456789ABCDEF";
			string key;
			random_device my_random_device;
			default_random_engine my_random_engine(my_random_device());
			uniform_int_distribution<int> random_number(0, sizeof(charset) - 2);
			for (int i = 0; i < 32; i++) {
				auto ri = random_number(my_random_engine);
				key.push_back(charset[ri]);
			}
			if (opt.genkey.fmt.value() == Arguments::GenKey::Fmt::insep) {
				cout << key << endl;
			}
			else if (opt.genkey.fmt.value() == Arguments::GenKey::Fmt::c) {
				cout << "{";
				for (size_t i = 0; i < 32; i += 2) {
					cout << "0x" << key[i] << key[i + 1];
					if (i != 30) {
						cout << ", ";
					}
				}
				cout << "}" << endl;
			}
			else {
				cout << key << endl;
				cout << "{";
				for (size_t i = 0; i < 32; i += 2) {
					cout << "0x" << key[i] << key[i + 1];
					if (i != 30) {
						cout << ", ";
					}
				}
				cout << "}" << endl;
			}
		}
		else if (opt.ecbm.has_value()) {
			int rc;
			IoEcbm ecbm(opt.ecbm.port);
			try {
				if (opt.ecbm.wu16.has_value()) {
					auto addr = (uint8_t)opt.ecbm.wu16.addr;
					auto sig = opt.ecbm.wu16.sig;
					auto data = opt.ecbm.wu16.data;
#if DEBUG_EN
					cout << "addr: " << (int)addr << ", sig: " << (int)sig << ", data: " << (int)data << endl;
#endif
					if (opt.ecbm.wu16.pin.has_value()) {
						auto key = pin_to_key(opt.ecbm.wu16.pin.value());
#if DEBUG_EN
						cout << "static auth key: ";
						print_key(key.data());
#endif
						rc = ecbm_begin_enc_session(ecbm.instance(), addr, key.data());
						if (rc != ECBM_OK) {
							throw runtime_error("fail to begin enc session: " + to_string(rc));
						}
					}
					uint8_t buf[2];
					stdser_s16(data, buf);
					rc = ecbm_write(ecbm.instance(), addr, sig, buf, 2);
					if (rc != ECBM_OK) {
						throw runtime_error("fail to write: " + to_string(rc));
					}
				}
				else {
					cout << "type '-h' arg for help" << endl;
				}
			}
			catch (const exception& e) {
				cout << e.what() << endl;
			}
		}
		else {
			cout << "type '-h' arg for help" << endl;
		}
	}
	catch (const structopt::exception& e) {
		cout << e.what() << endl;
		cout << "HELP: " << e.help() << endl;
		return -1;
	}
	catch (const std::exception& e) {
		cout << e.what() << endl;
	}
	
	return 0;
}
