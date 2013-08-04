/*
	Copyright (c) 2013 Game Closure.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of GCIF nor the names of its contributors may be used
	  to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

#include "LZReader.hpp"
using namespace cat;


bool LZReader::init(int xsize, int ysize, ImageReader & CAT_RESTRICT reader) {
	// Initialize parameters
	_xsize = xsize;
	_ysize = ysize;

	// Zero recent distances
	CAT_OBJCLR(_recent);
	_recent_ii = 0;

	// Initialize decoders
	if CAT_UNLIKELY(!_len_decoder.init(LEN_SYMS, reader, LEN_HUFF_BITS)) {
		CAT_DEBUG_EXCEPTION();
		return false;
	}
	if CAT_UNLIKELY(!_sdist_decoder.init(SDIST_SYMS, reader, SDIST_HUFF_BITS)) {
		CAT_DEBUG_EXCEPTION();
		return false;
	}
	if CAT_UNLIKELY(!_ldist_decoder.init(LDIST_SYMS, reader, LDIST_HUFF_BITS)) {
		CAT_DEBUG_EXCEPTION();
		return false;
	}

	return true;
}

int LZReader::read(u16 escape_code, ImageReader & CAT_RESTRICT reader, u32 &dist) {
	CAT_DEBUG_ENFORCE(escape_code < ESCAPE_SYMS);

	int len;

	if (escape_code < ESC_DIST_1) {
		// Recent distance
		dist = _recent[(_recent_ii + escape_code) & 3];
		len = readLen(reader);

		// Does not update recent array to avoid adding duplicates
	} else {
		if (escape_code >= ESC_DIST_LONG_2) {
			// Long distance
			len = escape_code - ESC_DIST_LONG_2 + 2;
			if (len > 9) {
				len = readLen(reader);
			}
			dist = readLongDist(reader);
		} else if (escape_code >= ESC_DIST_SHORT_2) {
			// Short distance
			len = escape_code - ESC_DIST_SHORT_2 + 2;
			if (len > 9) {
				len = readLen(reader);
			}
			dist = readShortDist(reader);
		} else if (escape_code >= ESC_DIST_UP_N2) {
			// Local neighbors above
			dist = (_xsize + escape_code - ESC_DIST_UP_N2 - 2) & DIST_MASK;
			len = readLen(reader);
		} else {
			// Local neighbors left
			dist = escape_code - ESC_DIST_1 + 2;
			if (dist == 2) {
				dist = 1;
			}
			len = readLen(reader);
		}

		// Store recent
		int ii = _recent_ii;
		_recent[ii] = dist;
		_recent_ii = (ii + 1) & 3;
	}

	return len;
}

