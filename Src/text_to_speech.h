

#ifndef __TEXT_TO_SPEECH_H
#define __TEXT_TO_SPEECH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>


//since the text-to-speech requires 'normalized' (i.e. lower-cased) text, it is
//suggested that in any buffering you might do that you do the lower-casing
//there, prior to calling these things.  That way you can avoid having to
//process it further after using 'pluckWord' and can probably do the text-
//to-speech directly from your buffer, rather than having to make another
//temporary buffer.


//pluck word
//given a text block, find the next word's start and end.  It is required that
//a word be bracketed by word-bounding characters -- i.e. a word cannot end
//a block of text itself.  Instead, some word-bounding character (perhaps just
//newline) needs to come after the final character of the word.
//Ultimately, this is useful when streaming because you generally don't know
//if you've received all the characters yet.
//determines:
//	first character index of a word
//	character index + 1 of end of word (i.e. exclusive)
//returns:
// 0:  a word was found
// 1:  a partial word was found starting at *pchWordStart; discard prior text
// 2:  no word was found -- discard the whole buffer of text
int pluckWord(const char* pszText, int nTextLen,
		const char** pchWordStart, const char** pchWordEnd);


//convert a word to speech.  the word must have already been normalized to
//lower case!  returns the number of phonemes produced.
int ttsWord(const char* pszNormWord, int nWordLen,	//the text
		const uint8_t* pbyTTSRulesBlob,				//the rules blob
		uint8_t* pbyPhon, size_t nPhonLen );		//the speech


//XXX internal; temporarily exposed for unit testing
typedef struct TTSRule_compact
{
	const uint8_t*	_left;
	const uint8_t*	_bracket;
	const uint8_t*	_right;
	const uint8_t*	_phone;
} TTSRule_compact;
int _getRuleSectionLength(const uint8_t* pbyTTSRulesBlob, int nIdxRuleSect);
void _reconstitute_rule(const uint8_t* pbyTTSRulesBlob,
		int nIdxRuleSect, int nIdxRule, TTSRule_compact* rule);
int _transforminput(const char* pszNormWord, size_t nWordLen, size_t nIdxWord,
		const uint8_t* pbyTTSRulesBlob, int nIdxRuleSect,
		uint8_t* pbyPhon, int* pnPhonLen);


#ifdef __cplusplus
}
#endif

#endif
