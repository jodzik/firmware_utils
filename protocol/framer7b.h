#ifndef FRAMER7B
#define FRAMER7B

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

#define FRAMER7B_BUFSIZE	4096 + 4096/7 + 2

typedef struct Framer7b {
	uint8_t buf[FRAMER7B_BUFSIZE];
	size_t bufsize;
	size_t bufptr;
	uint8_t state;
	uint8_t id;
} Framer7b;

int framer7b_push(Framer7b* framer, uint8_t byte);
int framer7b_make(Framer7b* framer, size_t ndata);
uint8_t* framer7b_get_write_buf(Framer7b* framer);
uint8_t* framer7b_get_read_buf(Framer7b* framer);
uint8_t* framer7b_get_send_buf(Framer7b* framer);
void framer7b_reset(Framer7b* framer);

#ifdef __cplusplus
}
#endif

#endif // !FRAMER7B
