#include <iostream>
#include <vector>
using namespace std;

#include "Log.hpp"
#include "Clock.hpp"

#include "ImageWriter.hpp"
#include "ImageMaskWriter.hpp"
#include "ImageLZWriter.hpp"
#include "ImageCMWriter.hpp"

#include "ImageReader.hpp"
#include "ImageMaskReader.hpp"
#include "ImageLZReader.hpp"
#include "ImageCMReader.hpp"
using namespace cat;

#include "lodepng.h"
#include "optionparser.h"


class Converter {
public:
	static int compress(const char *filename, const char *outfile) {
		vector<unsigned char> image;
		unsigned width, height;

		unsigned error = lodepng::decode(image, width, height, filename);

		if (error) {
			CAT_WARN("main") << "Decoder error " << error << ": " << lodepng_error_text(error);
			return error;
		}

		int err;

		ImageWriter writer;
		if ((err = writer.init(width, height))) {
			CAT_WARN("main") << "Unable to initialize image writer: " << ImageWriter::ErrorString(err);
			return err;
		}

		// Generate ImageMask
		ImageMaskWriter imageMaskWriter;
		if ((err = imageMaskWriter.initFromRGBA(&image[0], width, height))) {
			CAT_WARN("main") << "Unable to generate image mask: " << ImageWriter::ErrorString(err);
			return err;
		}

		imageMaskWriter.write(writer);
		imageMaskWriter.dumpStats();

		ImageLZWriter imageLZWriter;
		if ((err = imageLZWriter.initFromRGBA(&image[0], width, height))) {
			CAT_WARN("main") << "Unable to initialize LZ compressor: " << ImageWriter::ErrorString(err);
			return err;
		}

		imageLZWriter.write(writer);
		imageLZWriter.dumpStats();

		ImageCMWriter imageCMWriter;
		if ((err = imageCMWriter.initFromRGBA(&image[0], width, height, imageMaskWriter, imageLZWriter))) {
			CAT_WARN("main") << "Unable to initialize CM compressor: " << ImageWriter::ErrorString(err);
			return err;
		}

		imageCMWriter.write(writer);
		imageCMWriter.dumpStats();

		if ((err = writer.finalizeAndWrite(outfile))) {
			CAT_WARN("main") << "Unable to finalize and write image mask: " << ImageWriter::ErrorString(err);
			return err;
		}

#if 1
		{
			CAT_WARN("main") << "Writing post-filter image file";

			lodepng_encode_file("postfilter.png", (const unsigned char*)&image[0], width, height, LCT_RGBA, 8);
		}
#endif

		CAT_INFO("main") << "Wrote " << outfile;
		return 0;
	}

	static int decompress(const char *filename, const char *outfile) {
		ImageReader reader;

		int err;

		if ((err = reader.init(filename))) {
			CAT_WARN("main") << "Unable to read file: " << ImageReader::ErrorString(err);
			return err;
		}

		// Fully-Transparent Alpha Mask

		ImageMaskReader imageMaskReader;
		if ((err = imageMaskReader.read(reader))) {
			CAT_WARN("main") << "Unable to read GC-FTAM: " << ImageReader::ErrorString(err);
			return err;
		}

		imageMaskReader.dumpStats();

		// 2D-LZ Exact Match

		ImageLZReader imageLZReader;
		if ((err = imageLZReader.read(reader))) {
			CAT_WARN("main") << "Unable to read GC-2D-LZ: " << ImageReader::ErrorString(err);
			return err;
		}

		imageLZReader.dumpStats();

		// Context Modeling Decompression

		ImageCMReader imageCMReader;
		if ((err = imageCMReader.read(reader, imageMaskReader, imageLZReader))) {
			CAT_WARN("main") << "Unable to read GC-CM: " << ImageReader::ErrorString(err);
			return err;
		}

		imageCMReader.dumpStats();

		imageLZReader.dumpStats();

		// Verify hash

		if (!reader.finalizeCheckHash()) {
			CAT_WARN("main") << "Hash mismatch";
			return 1000;
		}

		CAT_WARN("main") << "Writing output image file: " << outfile;
		// Convert to image:

		CAT_INFO("main") << "Read success!";
		return 0;
	}
};


//// Command-line parameter parsing

enum  optionIndex { UNKNOWN, HELP, VERBOSE, SILENT, COMPRESS, DECOMPRESS, TEST };
const option::Descriptor usage[] =
{
  {UNKNOWN, 0,"" , ""    ,option::Arg::None, "USAGE: ./gcif [options] [output file path]\n\n"
                                             "Options:" },
  {HELP,    0,"h", "help",option::Arg::None, "  --[h]elp  \tPrint usage and exit." },
  {VERBOSE,0,"v" , "verbose",option::Arg::None, "  --[v]erbose \tVerbose console output" },
  {SILENT,0,"s" , "silent",option::Arg::None, "  --[s]ilent \tNo console output (even on errors)" },
  {COMPRESS,0,"c" , "compress",option::Arg::Optional, "  --[c]ompress <input PNG file path> \tCompress the given .PNG image." },
  {DECOMPRESS,0,"d" , "decompress",option::Arg::Optional, "  --[d]ecompress <input GCI file path> \tDecompress the given .GCI image" },
  {TEST,0,"t" , "test",option::Arg::Optional, "  --[t]est <input PNG file path> \tTest compression to verify it is lossless" },
  {UNKNOWN, 0,"" ,  ""   ,option::Arg::None, "\nExamples:\n"
                                             "  ./gcif -tv ./original.png\n"
                                             "  ./gcif -c ./original.png test.gci\n"
                                             "  ./gcif -d ./test.gci decoded.png" },
  {0,0,0,0,0,0}
};

int processParameters(option::Parser &parse, option::Option options[]) {
	if (parse.error()) {
		CAT_FATAL("main") << "Error parsing arguments [retcode:1]";
		return 1;
	}

	if (options[SILENT]) {
		Log::ref()->SetThreshold(LVL_SILENT);
	}

	if (options[VERBOSE]) {
		Log::ref()->SetThreshold(LVL_INANE);
	}

	if (options[COMPRESS]) {
		if (parse.nonOptionsCount() != 2) {
			CAT_WARN("main") << "Input error: Please provide input and output file paths";
		} else {
			const char *inFilePath = parse.nonOption(0);
			const char *outFilePath = parse.nonOption(1);
			int err;

			if ((err = Converter::compress(inFilePath, outFilePath))) {
				CAT_INFO("main") << "Error during conversion [retcode:" << err << "]";
				return err;
			}

			return 0;
		}
	} else if (options[DECOMPRESS]) {
		if (parse.nonOptionsCount() != 2) {
			CAT_WARN("main") << "Input error: Please provide input and output file paths";
		} else {
			const char *inFilePath = parse.nonOption(0);
			const char *outFilePath = parse.nonOption(1);
			int err;

			if ((err = Converter::decompress(inFilePath, outFilePath))) {
				CAT_INFO("main") << "Error during conversion [retcode:" << err << "]";
				return err;
			}

			return 0;
		}
	} else if (options[TEST]) {
		CAT_FATAL("main") << "TODO";
	}

	option::printUsage(std::cout, usage);
	return 0;
}


//// Entrypoint

int main(int argc, const char *argv[]) {

	Clock::ref()->OnInitialize();

	if (argc > 0) {
		--argc;
		++argv;
	}

	option::Stats  stats(usage, argc, argv);
	option::Option *options = new option::Option[stats.options_max];
	option::Option *buffer = new option::Option[stats.buffer_max];
	option::Parser parse(usage, argc, argv, options, buffer);

	int retval = processParameters(parse, options);

	delete []options;
	delete []buffer;

	Clock::ref()->OnFinalize();

	return retval;
}

