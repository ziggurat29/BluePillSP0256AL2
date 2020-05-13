
#include "text_to_speech.h"
#include <stddef.h>
#include <string.h>
#include <ctype.h>


//defs from origianl tts_rules.h; must be in sync

typedef struct PhonSeq {
	const char*	_phone;
	size_t	_len;
} PhonSeq;


typedef struct TTSRule {
	const char*	_left;
	const char*	_bracket;
	const char*	_right;
	PhonSeq	_phone;
} TTSRule;




//i.e., not is punctuation
int _isAlpha ( char ch ) {
	return ( ch >= 'a' && ch <= 'z' );
}

//'#'
int _isVowel ( char ch ) {
	return ( 'a' == ch || 'e' == ch || 'i' == ch || 'o' == ch || 'u' == ch || 'y' == ch );
}

//'*' one or more consonants; also used for '^' (one consonant), and ':' (zero or more)
int _isConsonant ( char ch ) {
	return ( 'b' == ch || 'c' == ch || 'd' == ch || 'f' == ch || 'g' == ch || 
		'h' == ch || 'j' == ch || 'k' == ch || 'l' == ch || 'm' == ch || 
		'n' == ch || 'p' == ch || 'q' == ch || 'r' == ch || 's' == ch || 
		't' == ch || 'v' == ch || 'w' == ch || 'x' == ch || //'y' == ch || 
		'z' == ch );
}

//'.'
int _isVoicedConsonant ( char ch ) {
	return ( 'b' == ch || 'd' == ch || 'g' == ch || 'j' == ch || 'l' == ch || 
		'm' == ch || 'n' == ch || 'r' == ch || 'v' == ch || 'w' == ch || 
		'z' == ch );
}

//'+'
int _isFrontVowel ( char ch ) {
	return ( 'e' == ch || 'i' == ch || 'y' == ch );
}



//I am writing this without using regexes simply because there will not be that
//capability on the final target.

//word := 
//  one or more letter sequences with apostrophe:  [a-zA-Z']+
//  (this is a basic translatable word)
// or
//  one or more punctuation sequence: [/,:;!-\.\?]+
//  (this is an internal word separator that may be adjacent-to/inside a word)
// else skip this character

//so, really there are three character classes to consider

int _classifyChar ( char ch ) {
	if ( ( ch >= 'A' && ch <= 'Z' ) || ( ch >= 'a' && ch <= 'z' ) || ( ch == '\'' ) ) {
		return 1;
	} else if ( ch == '/' || ch == ',' || ch == ':' || ch == ';' || ch == '!' || ch == '-' || ch == '.' || ch == '?' ) {
		return 2;
	} else {
		return 0;
	}
}



int _matchLeft(const char* pszNormWord, size_t nWordLen, size_t nIdxWord, const uint8_t* abyCtx)
{
	if (NULL == abyCtx || 0 == abyCtx[0])	//'anything'? (empty string)
		return 1;

	//OK we match this backwards from the end
	int nIdxText = nIdxWord - 1;		//last char in text
	int nIdxMatch = abyCtx[0] - 1;	//last char in pattern
	//whiz over the context characters, consuming input from the end
	while (nIdxMatch >= 0)
	{
		//try literals
		char chThisCtx = (char)abyCtx[nIdxMatch + 1];	//+1 because length prefix
		if (_isAlpha(chThisCtx) || '\'' == chThisCtx || ' ' == chThisCtx )
		{
			if (chThisCtx != pszNormWord[nIdxText])
				return 0;	//fail; done
			//consume input and carry on
			nIdxText -= 1;
		}
		else
		{
			//must be a pattern metachar
			if ('$' == chThisCtx )	//case of 'Nothing'
			{
				if (0 != nIdxWord)	//nothing to the left
					return 0;
			}
			else if (chThisCtx == '#')	//one or more vowels
			{
				if (!_isVowel(pszNormWord[nIdxText]))
					return 0;
				nIdxText -= 1;
				while (_isVowel(pszNormWord[nIdxText]))
					nIdxText -= 1;
			}
			else if (chThisCtx == ':')	//zero or more consonants
			{
				while (_isConsonant(pszNormWord[nIdxText]))
					nIdxText -= 1;
			}
			else if (chThisCtx == '^')	//one consonant
			{
				if (!_isConsonant(pszNormWord[nIdxText]))
					return 0;
				nIdxText -= 1;
			}
			else if (chThisCtx == '.')	//one voiced consonant
			{
				if (!_isVoicedConsonant(pszNormWord[nIdxText]))
					return 0;
				nIdxText -= 1;
			}
			else if (chThisCtx == '+')	//one front vowel
			{
				if (!_isFrontVowel(pszNormWord[nIdxText]))
					return 0;
				nIdxText -= 1;
			}
			else	//'%' can't be in left context
			{
				return 0;
			}
		}

		nIdxMatch -= 1;
	}

	return 1;
}



int _matchRight(const char* pszNormWord, size_t nWordLen, size_t nIdxWord, const uint8_t* abyCtx)
{
	if (NULL == abyCtx || 0 == abyCtx[0])	//'anything'? (empty string)
		return 1;
	//OK we match this forwards from the beginning
	int nIdxText = nIdxWord;	//first char in text
	int nIdxMatch = 0;		//first char in pattern
	//whiz over the context characters, consuming input from the beginning
	while (nIdxMatch < abyCtx[0])
	{
		//try literals
		char chThisCtx = (char)abyCtx[nIdxMatch + 1];	//+1 because length prefix
		if (_isAlpha(chThisCtx) || '\'' == chThisCtx || ' ' == chThisCtx )
		{
			if (chThisCtx != pszNormWord[nIdxText])
				return 0;	//fail; done
			//consume input and carry on
			nIdxText += 1;
		}
		else
		{
			//must be a pattern metachar
			if ('$' == chThisCtx )	//case of 'Nothing'
			{
				if (nWordLen != nIdxWord)	//nothing to the right
					return 0;
			}
			else if (chThisCtx == '#')	//one or more vowels
			{
				if (! _isVowel(pszNormWord[nIdxText]))
					return 0;
				nIdxText += 1;
				while (_isVowel(pszNormWord[nIdxText]))
					nIdxText += 1;
			}
			else if (chThisCtx == ':')	//zero or more consonants
			{
				while (_isConsonant(pszNormWord[nIdxText]))
					nIdxText += 1;
			}
			else if (chThisCtx == '^')	//one consonant
			{
				if (! _isConsonant(pszNormWord[nIdxText]))
					return 0;
				nIdxText += 1;
			}
			else if (chThisCtx == '.')	//one voiced consonant
			{
				if (! _isVoicedConsonant(pszNormWord[nIdxText]))
					return 0;
				nIdxText += 1;
			}
			else if (chThisCtx == '+')	//once front vowel
			{
				if (! _isFrontVowel(pszNormWord[nIdxText]))
					return 0;
				nIdxText += 1;
			}
			else if (chThisCtx == '%')	//'e'-related things at the end of the word '-e', '-ed', '-er', '-es', '-ely', '-ing'
			{
				if ('e' == pszNormWord[nIdxText])
				{
					nIdxText += 1;	//we will definitely take the e; now see if we can also consume an ly, r, s, or d
					if ('l' == pszNormWord[nIdxText])
					{
						nIdxText += 1;
						if ('y' == pszNormWord[nIdxText])
							nIdxText += 1;
						else
							nIdxText -= 1;	//don't consume the 'l'
					}
					else if ('r' == pszNormWord[nIdxText] || 's' == pszNormWord[nIdxText] || 'd' == pszNormWord[nIdxText])
					{
						nIdxText += 1;
					}
				}
				else if ('i' == pszNormWord[nIdxText])
				{
					nIdxText += 1;
					if ('n' == pszNormWord[nIdxText])
					{
						nIdxText += 1;
						if ('g' == pszNormWord[nIdxText])
							nIdxText += 1;
						else
							return 0;
					}
				}
				else
					return 0;
			}
			else	//horror unknown
				return 0;
		}
		nIdxMatch += 1;
	}

return 1;
}



//get the count of rules in a section.
int _getRuleSectionLength(const uint8_t* pbyTTSRulesBlob, int nIdxRuleSect)
{
	//the first section is the list of groups' offsets
	uint16_t* pnGrpOff = (uint16_t*)pbyTTSRulesBlob;
	//nIdxRuleSect cannot be > 27, but we aren't going to check that here
	//it's simply the difference between this section start and the next
	uint16_t nThisOff = pnGrpOff[nIdxRuleSect];
	uint16_t nNextOff = pnGrpOff[nIdxRuleSect + 1];
	return (nNextOff - nThisOff) / (4*sizeof(uint16_t));
}



//our definition of a 'compact' rule, which consist of length-prefixed data.
//the first three are the 'context's and are ASCII text, the last is binary.
typedef struct TTSRule_compact
{
	const uint8_t*	_left;
	const uint8_t*	_bracket;
	const uint8_t*	_right;
	const uint8_t*	_phone;
} TTSRule_compact;



//given the rule blob, section, and index, 'reconstitute' the rule into a
//convenient structure.
void _reconstitute_rule ( const uint8_t* pbyTTSRulesBlob, 
		int nIdxRuleSect, int nIdxRule, TTSRule_compact* rule )
{
	//the first section is the list of groups' offsets
	uint16_t* pnGrpOff = (uint16_t*)pbyTTSRulesBlob;
	uint16_t nGroupOff = pnGrpOff[nIdxRuleSect];	//start of rule list
	//the second section is the rules list, each rule is four offsets to
	//length-prefixed data
	uint16_t nRuleOff = nGroupOff + nIdxRule * 4 * sizeof(uint16_t);
	uint16_t* pnRule = (uint16_t*)(&pbyTTSRulesBlob[nRuleOff]);
	rule->_left = &pbyTTSRulesBlob[pnRule[0]];
	rule->_bracket = &pbyTTSRulesBlob[pnRule[1]];
	rule->_right = &pbyTTSRulesBlob[pnRule[2]];
	rule->_phone = &pbyTTSRulesBlob[pnRule[3]];
}



//Given a normalized (i.e. reduced to lower case and trimmed) word, and a
//present index into that word, and a section of rules to contemplate, find
//a rule that matches as per the contexts.  Place the phonemes in the buffer,
//and return the number of characters in the word that were consumed, and
//update the nPhonLen to indicate what is remaining of the buffer.
//If the phonemes will not fit into the buffer, return a negative number in
//nPhonLen indicating how much /more/ buffer would be needed.
int _transforminput(const char* pszNormWord, size_t nWordLen, size_t nIdxWord, 
		const uint8_t* pbyTTSRulesBlob, int nIdxRuleSect, 
		uint8_t* pbyPhon, int* pnPhonLen )
{
	int nConsumed = 1;	//we'll figure it out, but must always consume something
	TTSRule_compact rule;
	int nRuleSecLen = _getRuleSectionLength(pbyTTSRulesBlob, nIdxRuleSect);
	for (int nIdxRule = 0; nIdxRule < nRuleSecLen; ++nIdxRule)
	{
		_reconstitute_rule(pbyTTSRulesBlob, nIdxRuleSect, nIdxRule, &rule);

		//first, see if the 'bracket' context matches, by scanning forward
		size_t nIdxText = nIdxWord;
		size_t nIdxMatch = 0;
		while (nIdxText < nWordLen && nIdxMatch < (size_t)rule._bracket[0])
		{
			//YYY must we consider metachars in bracket context? appears not
			if (pszNormWord[nIdxText] != rule._bracket[nIdxMatch+1])	//+1 because length-prefix
				break;
			nIdxText += 1;
			nIdxMatch += 1;
		}
		//if we didn't match all of the pattern, then it is not a match
		if (nIdxMatch != (size_t)rule._bracket[0])
			continue;
		//see if the left context matches
		if ( ! _matchLeft(pszNormWord, nWordLen, nIdxWord, rule._left))
			continue;
		//see if the right context matches
		if ( ! _matchRight(pszNormWord, nWordLen, nIdxText, rule._right))
			continue;
		//match! push the associated phoneme sequence, and update what we have consumed

		if (*pnPhonLen >= rule._phone[0] )	//enough space?
		{
			memcpy(pbyPhon, &rule._phone[1], rule._phone[0]);
			pbyPhon += rule._phone[0];	//advance
		}
		*pnPhonLen -= rule._phone[0];	//reduce by what we took (or would have taken)

		nConsumed = nIdxText - nIdxWord;
		break;
	}

	return nConsumed;
}



//given a buffer of text, pluck the first 'word' that is in it, returning the
//[start,end) boundaries of that word, and also a disposition about if a word
//was found (0), and incomplete word was found (2), or nothing was found (2).
int pluckWord(const char* pszText, int nTextLen,
		const char** pchWordStart, const char** pchWordEnd)
{
	if (NULL == pszText || NULL == pchWordStart || NULL == pchWordEnd)
	{
		if (NULL != pchWordStart)
			*pchWordStart = NULL;
		if (NULL != pchWordEnd)
			*pchWordEnd = NULL;
		return 2;
	}
	if (nTextLen < 0)
		nTextLen = strlen(pszText);

	//crack str into words and text-to-speech them
	size_t nIdxStart = 0;
	size_t nIdxEnd = 0;

	//skip leading non-chars
	while ( nIdxStart < (size_t)nTextLen &&
			0 == _classifyChar(pszText[nIdxStart]) )
	{
		nIdxStart += 1;
	}

	//print ( "gathering..." )
	//gather until word break (or end)
	nIdxEnd = nIdxStart;
	while (nIdxEnd < (size_t)nTextLen &&
			0 != _classifyChar(pszText[nIdxEnd]))
	{
		nIdxEnd += 1;
	}

	*pchWordStart = &pszText[nIdxStart];
	*pchWordEnd = &pszText[nIdxEnd];

	if (nIdxStart == nTextLen)
	{
		// 2:  no word was found -- discard the whole buffer of text
		return 2;
	}
	else
	{
		if (nIdxEnd == nTextLen)
		{
			// 1:  a partial word was found starting at *pchWordStart; discard prior text
			return 1;
		}
		//returns 0:  a word was found
		return 0;
	}
}



//convert a word to speech.  the word must have already been normalized to
//lower case!  returns the number of phonemes produced.
int ttsWord(const char* pszNormWord, int nWordLen,
		const uint8_t* pbyTTSRulesBlob,
		uint8_t* pbyPhon, size_t nPhonLen)
{
	if (nWordLen < 0)
	{
		nWordLen = strlen(pszNormWord);
	}
	//scan the juicy bits
	int nProduced = 0;
	int nIdxWord = 0;
	while (nIdxWord < nWordLen)
	{
		//use the first character to skip to a section of rules
		char chNow = (char)tolower(pszNormWord[nIdxWord]);
		int nIdxRuleSect = 0;
		if (_isAlpha(chNow))
		{
			nIdxRuleSect = chNow - 'a' + 1;
		}
		else
		{
			nIdxRuleSect = 0;
		}

		//whiz through rules to find a match, consume input.  must consume some!
		int nRemBefore = (nProduced < 0) ? 0 : (nPhonLen - nProduced);
		int nRemAfter = nRemBefore;
		int nConsumed = _transforminput(pszNormWord, nWordLen, nIdxWord, pbyTTSRulesBlob, nIdxRuleSect, pbyPhon, &nRemAfter);
		nIdxWord += nConsumed;
		if (nRemAfter < 0)	//if nRem goes negative, we start tracking additional space needed
		{
			if ( nProduced >= 0 )	//first time going negative
			{
				nProduced = 0;	//setup to accumulated additional needed space
				pbyPhon = NULL;	//(for kicks)
			}
			nProduced += nRemAfter;	//(remember nRem is negative, so we add)
		}
		else
		{
			int nTaken = nRemBefore - nRemAfter;
			pbyPhon += nTaken;
			nProduced += nTaken;
		}
	}

	return nProduced;
}

