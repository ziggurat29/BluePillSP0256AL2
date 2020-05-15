//ADPCM decoded as per Interactive Multimedia Association's reference algorithm
//and first implemented by Intel/DVI.

#include "adpcm.h"


//'Index Change' table; indexed by adpcm code
const int IndexChange[] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};


//'Quantizer Step Size' lookup table
const int QuantStepSize[] = {
	    7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
	   19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
	   50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
	  130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
	  337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
	  876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
	 2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
	 5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};



//given a 4-bit ADPCM code and current state, generate the signed 16-bit value
int adpcm_decode_sample ( int code, ADPCMstate* state )
{
	//setup
	int predsample = state->prevsample;
	int index = state->previndex;
	//get quantizer step size
	int step = QuantStepSize[index];
	//compute difference
	int diffq = step >> 3;
	if( code & 4 )
		diffq += step;
	if( code & 2 )
		diffq += step >> 1;
	if( code & 1 )
		diffq += step >> 2;
	//predict as previous plus delta
	if ( code & 8 )	//signed 4 bit (1-cpl)
		predsample -= diffq;
	else
		predsample += diffq;
	//clip to int16 range
	if ( predsample > 32767 )
		predsample = 32767;
	else if ( predsample < -32767 )
		predsample = -32767;
	//update quantizer step size base on code
	index += IndexChange[code];
	//clip index to bounds
	if ( index < 0 )
		index = 0;
	else if ( index > 88 )
		index = 88;
	//update the state for next prediction
	state->prevsample = predsample;
	state->previndex = index;
	//tada!
	return predsample;
}


