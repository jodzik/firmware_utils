#include "framer7b.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define _FRAMER7B_MARK_MASK					0b10000000
#define _FRAMER7B_BEGIN						0b11010100
#define _FRAMER7B_END						0b10000001

#define _FRAMER7B_STATE_WAIT				0
#define _FRAMER7B_STATE_RECEIVE				1

/* * * Put only data buffer, return new data size with add
 * ndata must be include only data without mark bytes, push mark bytes after encode
 * data buffer must be have additional size for encode
 * * */
static int _framer7b_encode(uint8_t* data, size_t ndata) {
	size_t nDataAdd = ndata % 7 == 0 ? ndata / 7 : ndata / 7 + 1;
	size_t iDataAdd = ndata;
	memset(&data[iDataAdd], 0, nDataAdd);
	for (size_t i = 0; i < ndata; i++) {
		if (data[i] & _FRAMER7B_MARK_MASK) {
			data[iDataAdd + i / 7] |= 1 << (i % 7);
			data[i] &= ~_FRAMER7B_MARK_MASK;
		}
	}
	return (int)(nDataAdd + ndata);
}

/* * * Put data without mark bytes, return new data size
 * ndata must be include only data bytes, without mark bytes
 * * */
static int _framer7b_decode(uint8_t* data, size_t ndata) {
	size_t nDataAdd = ndata % 8 == 0 ? ndata / 8 : ndata / 8 + 1;
	size_t nrData = ndata - nDataAdd; // Without add bytes

	for (size_t i = 0; i < nrData; i++) {
		if (data[nrData + i / 7] & (1 << (i % 7))) {
			data[i] |= _FRAMER7B_MARK_MASK;
		}
	}
	return (int)nrData;
}

void framer7b_reset(Framer7b* framer) {
	framer->bufptr = 0;
	framer->state = _FRAMER7B_STATE_WAIT;
}

int framer7b_push(Framer7b* framer, uint8_t byte) {
	int rc;
	if (framer->state == _FRAMER7B_STATE_WAIT) {
		if (byte == _FRAMER7B_BEGIN) {
			// Begin receive, save user data in mark byte
			framer7b_reset(framer);
			framer->state = _FRAMER7B_STATE_RECEIVE;
		}
	}
	else {
		if (byte & _FRAMER7B_MARK_MASK) {
			if (byte == _FRAMER7B_END) {
				// Handle packet and return packet data len
				rc = _framer7b_decode(framer->buf, framer->bufptr);
				framer7b_reset(framer);
				return rc;
			}
			else {
				framer7b_reset(framer);
				if (byte == _FRAMER7B_BEGIN) {
					// Begin receive, save user data in mark byte
					framer->state = _FRAMER7B_STATE_RECEIVE;
				}
				else {
					return -1;
				}
			}
		}
		else {
			// Push back data
			framer->buf[framer->bufptr] = byte;
			framer->bufptr++;
			if (framer->bufptr >= framer->bufsize) {
				// Incorrect situation - too many bytes received, reset state
				framer7b_reset(framer);
				return -1;
			}
		}
	}

	return 0;
}

int framer7b_make(Framer7b* framer, size_t ndata) {
	int rc;
	if (ndata + ndata / 7 + 2 > framer->bufsize) {
		return -1;
	}
	framer->buf[0] = _FRAMER7B_BEGIN;
	rc = _framer7b_encode(&framer->buf[1], ndata);
	if (rc < 0) {
		return rc;
	}
	framer->buf[rc + 1] = _FRAMER7B_END;
	return rc + 2;
}

uint8_t* framer7b_get_write_buf(Framer7b* framer) {
	return &framer->buf[1];
}

uint8_t* framer7b_get_read_buf(Framer7b* framer) {
	return framer->buf;
}

uint8_t* framer7b_get_send_buf(Framer7b* framer) {
	return framer->buf;
}
