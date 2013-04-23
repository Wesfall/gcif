GCIF
====

Game Closure Image Format

This is a Work-In-Progress towards a new RGBA image format that works well for
our spritehseets.

Early test results indicate that GCIF files are ~60% the size of PNG sprites,
and the decompression speed is comparable or better than libpng.


Credit Where It's Due
=====================

The image format is built upon the work of several amazing software developers:

Stefano Brocchi
+ Image codec design inspiration
+ BCIF: http://www.dsi.unifi.it/DRIIA/RaccoltaTesi/Brocchi.pdf

Austin Appleby
+ Fast validation hash
+ MurmurHash3: https://code.google.com/p/smhasher/wiki/MurmurHash3

Yann Collet
+ Fast LZ codec
+ LZ4HC: https://code.google.com/p/lz4/

Charles Bloom
+ JPEG-LS
+ Blog: http://cbloomrants.blogspot.com/2010/08/08-12-10-lost-huffman-paper.html

Rich Geldreich
+ Fast static Huffman codec
+ LZHAM: https://code.google.com/p/lzham/

Thomas Wang
+ Integer hash function
+ Closest URL: http://burtleburtle.net/bob/hash/integer.html


Specification
=============

Starting from the BCIF spec we decided to make some improvements.

To optimize for low decompression time, we restricted ourselves to considering
only the fastest compression primitives: Filtering, LZ, and static Huffman.

BCIF does filtering and static Huffman coding, but left out LZ, which we feel
is a mistake since LZ is great for representing repeated data patterns, which
can be encoded byte-wise, reducing the number of Huffman symbols to decode.
In our tests, adding an LZ step improves compression ratio and decomp speed.

BCIF is also not designed for an alpha channel, and our games require alpha
for sprite-sheets.  Furthermore, our images tend to have a lot of
fully-transparent pixels that indicate 1-bit alpha is a good approach.
So we developed a monochrome compressor that outperforms PNG just for the alpha
channel.  Pixels that are represented by this monochrome bitmask can be skipped
during the rest of the RGBA decoding, so the overall performance is improved.

We also decided to rewrite BCIF from scratch after understanding how it worked,
since we found that the code could be significantly improved in decompression
speed, and we also disagreed with some of the finer points of the algorithm.


### Step 0. Fully-Transparent Pixel Encoding

Fully-transparent pixels are combined into a monochrome raster and a filter is
applied to each pixel:

For the first row:

+ If the pixel to the left is "on", then we predict the pixel is "on."

For other rows:

+ If the pixel above it is "on", then we predict the pixel is "on."
+ If the two pixels to the left are "on", then we predict the pixel is "on."

Whenever the filter fails to predict properly, a 1 bit is written.

The distance between these 1 bits is encoded for each row.
We tried delta-encoding the distances in x and y but did not see improvement.

For each scanline: {number of distances recorded} {list of distances...}
This is encoded as a byte stream, which is then LZ compressed with LZ4HC.

Static Huffman entropy encoding is then performed for further compression.

Pixels that are fully-transparent are skipped over during encoding/decoding.


### Step 1. 2D LZ

A custom 2D LZ77 algorithm is run to find repeated rectangular regions in the
image.  A rolling hash is used to do initial lookups on 3x3 regions, which is
the minimum size allowed for a match.  Hash collisions are checked, and then
matches are expanded.

If the resulting matches are accepted, they exclude further matches from
overlapping with them.  This approach gets RLE for free.  Each rectangular
region takes 10 bytes to represent, which is then compressed with Huffman
encoding and written to the file.  The resulting overhead is close to 5.5 bytes
per zone.


### Step 2. Filtering

Spatial and color filters are applied to the input data in 4x4 pixel blocks as
in BCIF.  The pair of filters that best estimate each block are chosen, as
measured by the L1 norm of the resulting pixel color component values, with 0
and 255 being the best result, and numbers around 128 being the worst.

The two filters are spatial and color.  The spatial filters are:

~~~
 Filter inputs:
         E
 F C B D
   A ?

	// In the order they are applied in the case of a tie:
	SF_Z,			// 0
	SF_D,			// D
	SF_C,			// C
	SF_B,			// B
	SF_A,			// A
	SF_AB,			// (A + B)/2
	SF_BD,			// (B + D)/2
	SF_CLAMP_GRAD,	// CBloom: 12: ClampedGradPredictor
	SF_SKEW_GRAD,	// CBloom: 5: Gradient skewed towards average
	SF_PICK_LEFT,	// New: Pick A or C based on which is closer to F
	SF_PRED_UR,		// New: Predict gradient continues from E to D to current
	SF_ABC_CLAMP,	// A + B - C clamped to [0, 255]
	SF_PAETH,		// Paeth filter
	SF_ABC_PAETH,	// If A <= C <= B, A + B - C, else Paeth filter
	SF_PLO,			// Offset PL
	SF_ABCD,		// (A + B + C + D + 1)/4
	SF_AD,			// (A + D)/2
~~~

In addition to these default filters, 80 linear filters involving A,B,C, and D
are selected to be used based on the input image, and will replace filters in
the above table where they are preferred.

And the color filters are:

~~~
	// In order of preference (based on entropy scores from test images):
	CF_GB_RG,	// from BCIF
	CF_GR_BG,	// from BCIF
	CF_YUVr,	// YUVr from JPEG2000
	CF_D9,		// from the Strutz paper
	CF_D12,		// from the Strutz paper
	CF_D8,		// from the Strutz paper
	CF_E2_R,	// Derived from E2 and YCgCo-R
	CF_BG_RG,	// from BCIF (recommendation from LOCO-I paper)
	CF_GR_BR,	// from BCIF
	CF_D18,		// from the Strutz paper
	CF_B_GR_R,	// A decent default filter
	CF_D11,		// from the Strutz paper
	CF_D14,		// from the Strutz paper
	CF_D10,		// from the Strutz paper
	CF_YCgCo_R,	// Malvar's YCgCo-R
	CF_GB_RB,	// from BCIF
~~~

The encoder exhaustively tries all of these SF+CF combinations, and the best 20
are then subjected to further entropy analysis.  This additional step greatly
improves compression by increasing the rate at which symbols are reused in the
post-filtered data, which makes the data easier to compress.

This generates encoded pixels and a filter description.  The filter description
is compressed with Huffman encoding and written to the file.


### Step 3. Order-1 Chaos Modeling and Encoding

For each RGB plane, the BCIF "chaos" metric is used to sort each remaining
filtered pixel into one of 8 bins.  The chaos metric is a rough approximation
to order-1 statistics.  The metric is defined as the sum of the highest set bit
index in the left and up post-filter values for each channel.  Recall that
after spatial and color filtering, the image data is mostly eliminated and
replaced with a few values near zero.  Smaller values (and zeroes especially)
lead to better compression, so the "chaos" of a location in the image after
filtering is exactly how large the nearby values are.

Comparing this approach to order-1 statistics, that would be calculating the
statistics for seeing a value of "0" after seeing a value of "1", "2", and so
on.  The limitation of this approach is that it requires significantly more
overhead and working memory since we only admit static Huffman codes for speed.
To get some of the order-1 results, we can group statistics together.  The
probability of seeing "1", "2", etc after "0" is exactly what the chaos level 0
statistics are recording!  Exactly also for chaos level 1.  But for chaos level
2 and above it gets a lot more fuzzy.  Since most of the symbols are close to
zero, this approach is maximizing the effect of order-1 statistics.

Furthermore, the chaos metric cares about two dimensions, both the vertical and
horizontal chaos.  As a result it is well-suited for 2D images.

Since most of the image data is near zero, areas where high values occur tend
to be stored together in the statistical model, which means it can be more
tightly tuned for that data, further improving compression.

When the number of pixels to be encoded falls below a certain threshold, all 8
chaos bins require too much overhead to make them worthwhile, so the compressor
switches off the chaos metric to cut the overhead down to 1/8th its size.

Since there are 8 chaos bins, that means 8 Huffman tables are transmitted for
each of the RGBA color planes.  Therefore, it was essential to develop a good
compression algorithm for the Huffman tables themselves.  So, the tables are
filtered and compressed using the same entropy encoding used on the image data.
This table compression is exceptionally good.  It compresses about 8KB of table
data down into about 3KB using several tricks including truncation.


What works right now
====================

The codec supports full RGBA.  The compressor and decompressor are close to
being finished.  Only a few images cause crashes or other issues.

The code is well-written in our opinion, easy to read and adapt, and easy to
incorporate into mobile development.

We plan to release a Java version for the encoder after the RGBA compression is
functional in C++ code, so that the encoder can be run on any platform without
having to compile it.  The decoder will be split off so that only a minimal
amount of code needs to be added to support this file format.  We're shooting
for one large C++ file, though it may end up being a small number of files.


Future plans
============

We have tried a lot of crazy things to improve compression, and only a few of
them stuck to the project.  Here are some more wild ideas:

+ Support a full palette mode for small or GIF-quantized images.
-- First determine the palette.
-- Sort the palette so that the image data attains maximum smoothness.
-- Apply all of the normal spatial filtering without color filters.
-- Encode it as one channel instead of RGBA.

+ Support scanline spatial and color filters.
-- Define one filter pair for an entire row.
-- Compare the result of doing this with tightly tuned filters; send the best.

+ Support LZ at the scanline level.
-- Make post-filter LZ a part of the scanline filter mode.
-- This is exciting because right now LZ is useless with the post-filter data.
-- WILL be better for some computer-generated images I am looking at.
-- WILL make us better than PNG for ALL images rather than just the majority.

+ Expose more of the thresholds and other constants at the API level.
-- Allows users to further optimize the result.

+ A new spritesheet generator that uses GCIF as an output file format.
-- Even better image compression by eliminating a lot of image data.
-- There is a lot of room for improvement in our current spriter.
-- Incorporate it into the GCIF codebase to make it a one-stop shop for games.

+ Image verification mode.
-- Add a stronger verification hash to the file format that can be checked.

Stay tuned! =) -cat

