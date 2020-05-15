//==============================================================
//This declares the ADPCM decoder.

#ifndef __ADPCM_H
#define __ADPCM_H

#ifdef __cplusplus
extern "C" {
#endif


typedef struct ADPCMstate
{
	int prevsample;
	int previndex;
} ADPCMstate;


//given a 4-bit ADPCM code and current state, generate the signed 16-bit value
int adpcm_decode_sample ( int code, ADPCMstate* state );



#ifdef __cplusplus
}
#endif

#endif
