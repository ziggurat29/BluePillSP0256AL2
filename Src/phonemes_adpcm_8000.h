#ifndef __PHONEMES_ADPCM_8000_H
#define __PHONEMES_ADPCM_8000_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct PhonemeEntry {
	const uint8_t*	_pbyADPCM;	//the ADPCM data for this phoneme
	uint16_t	_nLenComp;		//the compressed length
	uint16_t	_nLenUnc;		//the uncompressed length
} PhonemeEntry;

extern const struct PhonemeEntry g_apePhonemes_8000[];

#ifdef __cplusplus
}
#endif

#endif
