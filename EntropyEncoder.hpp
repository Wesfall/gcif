#ifndef ENTROPY_ENCODER_HPP
#define ENTROPY_ENCODER_HPP

#include "Platform.hpp"
#include "ImageWriter.hpp"
#include <vector>

namespace cat {


//#define ADAPTIVE_ZRLE
#define ADAPTIVE_ZRLE_THRESH 10 /* percent */

#define USE_AZ /* After-Zero Context (Pseudo-Order-1) */

static const int FILTER_RLE_SYMS = 128; // Number of symbols set apart for zRLE


//// EntropyEncoder

class EntropyEncoder {
	static const int BZ_SYMS = 256 + FILTER_RLE_SYMS;
#ifdef USE_AZ
	static const int AZ_SYMS = 256;
#endif

	u32 histBZ[BZ_SYMS], maxBZ;
#ifdef USE_AZ
	u32 histAZ[AZ_SYMS], maxAZ;
#endif
	u32 zeroRun;

	std::vector<int> runList;
	int runListReadIndex;

#ifdef ADAPTIVE_ZRLE
	u32 zeros, total;
	bool usingZ;
#endif

	u16 codesBZ[BZ_SYMS];
	u8 codelensBZ[BZ_SYMS];

#ifdef USE_AZ
	u16 codesAZ[AZ_SYMS];
	u8 codelensAZ[AZ_SYMS];
#endif

	void reset();

	void endSymbols();

	int writeZeroRun(int run, ImageWriter &writer);

public:
	CAT_INLINE EntropyEncoder() {
		reset();
	}
	CAT_INLINE virtual ~EntropyEncoder() {
	}

	void push(u8 symbol);
	void finalize();

	u32 writeOverhead(ImageWriter &writer);
	u32 encode(u8 symbol, ImageWriter &writer);
	u32 encodeFinalize(ImageWriter &writer);
};


} // namespace cat

#endif // ENTROPY_ENCODER_HPP
