//
// Created by sf on 4/15/16.
//

//#include "framework/archivers/physfs_archiver.cue.h"
#include "physfs_archiver_cue.h"
#include "framework/logger.h"

#include "library/strings.h"
#include <regex>
#include <boost/filesystem.hpp>

#include <physfs.h>
#include <fstream>
#include <mutex>
// for std::memcmp
#include <cstring>
#include <cstddef>
#include <library/sp.h>
#include <inttypes.h>
// endianness check
#include <SDL_endian.h>

using namespace OpenApoc;

namespace
{

// We actually only use BINARY here, but just for the sake of completion
enum class CUE_FileType {
    FT_UNDEFINED, // Should only be set if parsing failed
    FT_BINARY,
    FT_MOTOROLA,
    FT_AIFF,
    FT_WAVE,
    FT_MP3
};

// FIXME: Add more (all?) supported formats?
enum class CUE_TrackMode {
    MODE_UNDEFINED, // Should only be set if parsing failed
    MODE1_2048,
    MODE1_2352,
    MODE2_2048,
    MODE2_2324,
    MODE2_2336,
    MODE2_2352
};

// FIXME: This is a very incomplete CueSheet parser!
class CueParser
{
  private:
    enum {
        PARSER_START,
        PARSER_FILE,
        PARSER_TRACK,
        PARSER_FINISH,
        PARSER_ERROR
    } parserState;

    static std::regex commandRegex ;
    static std::regex fileArgRegex ;
    static std::regex trackArgRegex;
    static std::regex indexArgRegex;

    UString dataFileName;
    CUE_FileType fileType;
    CUE_TrackMode trackMode;

    // Parse command while not being in a specific context
    bool parseStart(std::string command, std::string arg)
    {
        // Waiting for "FILE" command
        UString cmd(command);
        if (cmd.toUpper() != "FILE")
        {
            LogInfo("Encountered unexpected command: \"%s\", ignoring", cmd.c_str());
            return false;
        }
        auto matchIter = std::sregex_iterator(arg.begin(), arg.end(), fileArgRegex);
        if (matchIter == std::sregex_iterator())
        {
            LogError("Malformed arguments for FILE command: \"%s\"", arg.c_str());
            parserState = PARSER_ERROR;
            return false;
        }
        auto match = *matchIter;
        dataFileName = UString(match[2]);
        UString fileTypeStr(match[3]);
        if (fileTypeStr.toUpper() != "BINARY")
        {
            LogError("Unsupported file type: \"%s\"", fileTypeStr.c_str());
            parserState = PARSER_ERROR;
            fileType = CUE_FileType::FT_UNDEFINED;
            return false;
        }
        fileType = CUE_FileType::FT_BINARY;
        return true;
    }
    // Parse command while being in a FILE context
    bool parseFile(std::string command, std::string arg)
    {
        // Waiting for the "TRACK" command
        UString cmd(command);
        if (cmd.toUpper() != "TRACK")
        {
            // According to https://www.gnu.org/software/ccd2cue/manual/html_node/FILE-_0028CUE-Command_0029.html#FILE-_0028CUE-Command_0029
            // only TRACK is allowed after FILE
            LogError("Encountered unexpected command: \"%s\" (only TRACK is allowed)", cmd.c_str());
            parserState = PARSER_ERROR;
            fileType = CUE_FileType::FT_UNDEFINED;
            return false;
        }
        auto matchIter = std::sregex_iterator(arg.begin(), arg.end(), trackArgRegex);
        if (matchIter == std::sregex_iterator())
        {
            LogError("Malformed arguments for TRACK command: \"%s\"", arg.c_str());
            parserState = PARSER_ERROR;
            fileType = CUE_FileType::FT_UNDEFINED;
        }
        auto match = *matchIter;
        int trackNumber = std::stoi(match[1]);
        UString modeStr(match[2]);

        if (trackNumber > 1)
        {
            LogWarning("First track is not numbered 1 (actual number is %d)", trackNumber);
        }
        trackMode = CUE_TrackMode::MODE_UNDEFINED;
        modeStr = modeStr.toUpper();
        if (modeStr == "MODE1/2048")
            trackMode = CUE_TrackMode::MODE1_2048;
        else if (modeStr == "MODE1/2352")
            trackMode = CUE_TrackMode::MODE1_2352;
        else if (modeStr == "MODE2/2048")
            trackMode = CUE_TrackMode::MODE2_2048;
        else if (modeStr == "MODE2/2324")
            trackMode = CUE_TrackMode::MODE2_2324;
        else if (modeStr == "MODE2/2336")
            trackMode = CUE_TrackMode::MODE2_2336;
        else if (modeStr == "MODE2/2352")
            trackMode = CUE_TrackMode::MODE2_2352;
        if (trackMode == CUE_TrackMode::MODE_UNDEFINED)
        {
            LogError("Unknown/unimplemented mode \"%s\"", modeStr.c_str());
            parserState = PARSER_ERROR;
            return false;
        }
        return true;
    }

    // Parse command while being in a TRACK context
    bool parseTrack(std::string command, std::string arg)
    {
        UString cmd(command);
        // TODO: check for possible commands, put parser into an "error" state if command is not valid
        if (cmd.toUpper() != "INDEX")
        {
            LogInfo("Encountered unexpected/unknown command: \"%s\", ignoring", cmd.c_str());
            return false;
        }
        // FIXME: I seriously could not make heads or tails of these indices.
        auto matchIter = std::sregex_iterator(arg.begin(), arg.end(), indexArgRegex);
        if (matchIter == std::sregex_iterator())
        {
            LogError("Malformed arguments for INDEX command: \"%s\"", arg.c_str());
            return false;
        }
        return true;
    }

    bool parse(UString cueFilename)
    {
        boost::filesystem::path cueFilePath(cueFilename.c_str());


        std::ifstream cueFile(cueFilename.str(), std::ios::in);
        if (!cueFile)
        {
            // Stream is unusable, bail out
            return false;
        }
        while(cueFile)
        {
            std::string cueLine;
            std::getline(cueFile, cueLine);

            auto matchIter = std::sregex_iterator(cueLine.begin(), cueLine.end(), commandRegex);
            if (matchIter == std::sregex_iterator()) { continue; }

            auto match = *matchIter;
            std::string command = match[1];
            std::string arg = match[2];

            switch(parserState)
            {
                case PARSER_START:
                    if (parseStart(command, arg))
                    {
                        parserState = PARSER_FILE;
                    }
                    break;
                case PARSER_FILE:
                    if (parseFile(command, arg))
                    {
                        parserState = PARSER_TRACK;
                    }
                    break;
                case PARSER_TRACK:
                    if (parseTrack(command, arg))
                    {
                        parserState = PARSER_FINISH;
                    }
                    break;
                default:
                    LogError("Invalid CueParser state!");
            }
            if ((parserState == PARSER_FINISH) || (parserState == PARSER_ERROR))
                return parserState == PARSER_FINISH;
        }
        return parserState == PARSER_FINISH;
    }


  public:
    CueParser(UString cueFile) : parserState(PARSER_START),
                                 fileType(CUE_FileType::FT_UNDEFINED),
                                 trackMode(CUE_TrackMode::MODE_UNDEFINED)
    {
        parse(cueFile);
    }
    bool isValid() { return parserState == PARSER_FINISH;}
    UString getDataFileName() { return dataFileName; }
    CUE_FileType getDataFileType() { return fileType; }
    CUE_TrackMode getTrackMode() { return trackMode; }
};

// Command regex: first group is a "command", then a whitespace and a number of arguments (till line end)
std::regex CueParser::commandRegex = std::regex("(\\w+)\\s+(.*)");
// File argument regex: optionally quoted filename (could be anything, really), a whitespace and a type
std::regex CueParser::fileArgRegex = std::regex("(\\\"?)(.+)(?:\\1)\\s+(\\w+)");
// Track argument regex: one/two digits for track identifier, a space, then a mode specifier
std::regex CueParser::trackArgRegex = std::regex("(\\d{1,2})\\s+([\\w/]+)");
// Index argument regex: number, whitespace and track time in mm:ss:ff format
std::regex CueParser::indexArgRegex = std::regex("(\\d{1,2})\\s+(\\d{2}):(\\d{2}):(\\d{2})");

// --- iso9660 reader follows

// lsb-msb type as defined by iso9660

struct int16_lsb_msb
{
#ifdef SDL_LIL_ENDIAN
    uint16_t val;        // lsb
    uint16_t __padding;  // msb
#else
    uint16_t __padding;
    uint16_t val;
#endif
};

struct sint16_lsb_msb
{
#ifdef SDL_LIL_ENDIAN
    int16_t val;
    int16_t __padding;
#else
    int16_t __padding;
    int16_t val;
#endif
};

struct int32_lsb_msb
{
#ifdef BOOST_LITTLE_ENDIAN
    uint32_t val;
    uint32_t __padding;
#else
    uint32_t __padding;
    uint32_t val;
#endif
};

struct sint32_lsb_msb
{
#ifdef BOOST_LITTLE_ENDIAN
    int32_t val;
    int32_t __padding;
#else
    int32_t __padding;
    int32_t val;
#endif
};

struct dec_datetime
{
    char year[4];
    char month[2];
    char day[2];
    char hour[2];
    char minute[2];
    char second[2];
    char hndSecond[2]; // hundredth of a second
    uint8_t gmtOff;
    // FIXME: This will correct to the WRONG unix time, the only thing that the
    // return value will be good for is checking whether two files on the same disk
    // were created at the same moment!
    PHYSFS_sint64 toUnixTime() // Convert to a saner (?) time representation
    {
        // The following is clearly an example of now NOT to do time stuff
        // The spec states that all fields are ASCII... we're gonna abuse that
        int year_int = (year[0] - '0') * 1000 +
                       (year[1] - '0') * 100 +
                       (year[2] - '0') * 10 +
                       (year[3] - '0');
        int month_int = (month[0] - '0') * 10 +
                        (month[1] - '0');
        int day_int = (day[0] - '0') * 10 +
                      (day[1] - '0');
        int hour_int = (hour[0] - '0') * 10 +
                       (hour[1] - '0');
        int minute_int = (minute[0] - '0') * 10 +
                         (minute[1] - '0');
        int second_int = (second[0] - '0') * 10 +
                         (second[1] - '0');
        //int hndsec_int = (hndSecond[0] - '0') * 10 +
        //                 (hndSecond[1] - '0');
        int gmtCorrection = 15 * (gmtOff - 48); // in minutes
        // The resulting number is very obviously erroneous, because I don't
        // account for leap years/seconds correctly
        // FIXME: Account for negative values?
        int yearsSinceEpoch = year_int - 1970;
        if (yearsSinceEpoch < 0) {yearsSinceEpoch = 0;}
        PHYSFS_sint64 unixSeconds = yearsSinceEpoch * 365 * 24 * 60 * 60;
        // account for leap years with our *best effort*
        unixSeconds += (yearsSinceEpoch / 4) * 24 * 60 * 60;
        // This is extremely wrong, but I stopped caring
        unixSeconds += (month_int - 1) * 30 * 24 * 60 * 60;
        unixSeconds += (day_int - 1) * 24 * 60 * 60;
        unixSeconds += hour_int * 60 * 60;
        unixSeconds += (minute_int + gmtCorrection) * 60;
        unixSeconds += second_int * 60;
        // Still don't know how to deal with missing timestamps...
        if (unixSeconds < 0)
            return -1;
        return unixSeconds;
    }
};

// Okay, TWO different datetime formats?
struct dir_datetime
{
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t gmtOffset;
    // FIXME: Same warnings as with dec_datetime apply
    PHYSFS_sint64 toUnixTime()
    {
        PHYSFS_sint64 unixSeconds = (year - 70) * 365 * 24 * 60 * 60;
        int gmtCorrection = 15 * (gmtOffset - 48);
        unixSeconds += ((year - 70) / 4) * 24 * 60 * 60;
        unixSeconds += (month - 1) * 30 * 24 * 60 * 60;
        unixSeconds += (day - 1) * 24 * 60 * 60;
        unixSeconds += hour * 60 * 60;
        unixSeconds += (minute + gmtCorrection) * 60;
        unixSeconds += second;
        if (unixSeconds < 0)
            return -1;
        return unixSeconds;
    }
};

static_assert(sizeof(int16_lsb_msb) == 4, "Unexpedted int16_lsb_msb_size!");
static_assert(sizeof(sint16_lsb_msb) == 4, "Unexpedted int16_lsb_msb_size!");
static_assert(sizeof(int32_lsb_msb) == 8, "Unexpedted int16_lsb_msb_size!");
static_assert(sizeof(sint32_lsb_msb) == 8, "Unexpedted int16_lsb_msb_size!");
static_assert(sizeof(dec_datetime) == 17, "Unexpected dec_datetime size!");
static_assert(sizeof(dir_datetime) == 7, "Unexpected dir_datetime size!");

class CueIO {
private:

    friend class CueArchiver;

    UString imageFile; // For stream duplication
    uint32_t lbaStart; // Starting LBA for this stream
    uint32_t lbaCurrent; // Current block for this stream
    uint32_t posInLba; // Current position in lba
    int64_t length; // Allowed length of the stream
    CUE_FileType fileType;
    CUE_TrackMode trackMode;
    std::ifstream fileStream;

    CueIO(const UString &fileName, uint32_t lbaStart, int64_t length,
          CUE_FileType fileType = CUE_FileType::FT_BINARY,
          CUE_TrackMode trackMode = CUE_TrackMode::MODE1_2048)
            : imageFile(fileName), lbaStart(lbaStart), lbaCurrent(lbaStart),
              posInLba(0), length(length), fileType(fileType), trackMode(trackMode)
    {
        fileStream.open(fileName.str(), std::ios::in | std::ios::binary);
        fileStream.seekg(lbaToByteOffset(lbaStart));
    }

    // Convert LBA to actual position in the stream
    int64_t lbaToByteOffset(uint32_t lba) {
        switch (trackMode) {
            // Each block is 2048 bytes, translation is trivial
            case CUE_TrackMode::MODE1_2048:
            case CUE_TrackMode::MODE2_2048:
                return lba * 2048;
                // Each block is 2048 bytes, but there's a prefix and
                // a postfix area for each sector
            case CUE_TrackMode::MODE1_2352:
                return lba * 2352 + 12 + 4; // 12 sync bytes, 4 header bytes
                // FIXME: Reality check?
                // For mode2, each block might be slightly larger than for mode1
                // These cases are dealing with "cooked" data
            case CUE_TrackMode::MODE2_2324:
                return lba * 2324; // Each sector is 2324 bytes
                // Strangely enough, mode2/2336 is the same as mode2/2352 without header?
            case CUE_TrackMode::MODE2_2336:
                return lba * 2336 + 8;
            case CUE_TrackMode::MODE2_2352:
                return lba * 2048 + 12 + 4 + 8;
            default:
                LogError("Unknown track mode set!");
                // Return negative offset to indicate error
                return -1;
        }
    }

    // Get the "user data" block size
    int32_t blockSize() {
        // FIXME: Reality check?
        switch (trackMode) {
            case CUE_TrackMode::MODE1_2048:
            case CUE_TrackMode::MODE2_2048:
            case CUE_TrackMode::MODE1_2352:
            case CUE_TrackMode::MODE2_2352:
                // Some docs say mode2 contains 2336 bytes of user data per block,
                // others insist on 2048 bytes...
            case CUE_TrackMode::MODE2_2336:
                return 2048;
            case CUE_TrackMode::MODE2_2324:
                return 2324;
            default:
                LogError("Bad track mode!");
        }
        // Unsupported track mode
        return -1;
    }

    // Get the "binary" block size
    int32_t binBlockSize() {
        switch (trackMode) {
            case CUE_TrackMode::MODE1_2048:
            case CUE_TrackMode::MODE2_2048:
                return 2048;
            case CUE_TrackMode::MODE1_2352:
            case CUE_TrackMode::MODE2_2352:
                return 2352;
            case CUE_TrackMode::MODE2_2336:
                return 2336;
            case CUE_TrackMode::MODE2_2324:
                return 2324;
            default:
                LogError("Bad track mode!");
        }
        // Unsupported track mode
        return -1;
    }

    // Offset of the user data portion of the block
    int32_t binDataOffset() {
        switch (trackMode) {
            // FIXME: Check mode2 correctness??
            case CUE_TrackMode::MODE1_2048:
            case CUE_TrackMode::MODE2_2048:
                return 0; // Only user data is present here
            case CUE_TrackMode::MODE2_2324:
                return 0;
            case CUE_TrackMode::MODE1_2352:
                return 12 + 4; // 12 bytes sync, 4 bytes header
            case CUE_TrackMode::MODE2_2352:
                return 12 + 4 + 8; // 12 bytes sync, 4 bytes header, 8 bytes subheader
            case CUE_TrackMode::MODE2_2336:
                return 8; // 8 bytes subheader (?)
            default:
                LogError("Bad track mode!");
        }
        // Unsupported track mode
        return -1;
    }

    int64_t read(void *buf, uint64_t len) {
        // Ignore size 0 reads
        if (!len) return 0;
        // Since we probably will have to read in parts,
        // we have to make the buffer seekable
        char *bufWrite = (char *) buf;
        // If the data is "cooked", just read it.
        if (trackMode == CUE_TrackMode::MODE1_2048 ||
            trackMode == CUE_TrackMode::MODE2_2048)
        {
            // FIXME: This won't correctly handle multi-extent files
            lbaCurrent += len / 2048;
            int64_t start = fileStream.tellg();
            fileStream.read(bufWrite, len);
            return fileStream.tellg() - start;
        }
        int64_t remainLength = length - (lbaCurrent - lbaStart) * blockSize() - posInLba;
        if (remainLength < 0)
        {
            LogError("Trying to read past end of stream!");
            return -1;
        }
        if (len > remainLength)
        {
            // FIXME: This produces way too much output as well, though we could use it somehow?
            LogWarning("Requested read of size %" PRIu64 " is bigger than remaining %" PRIu64 " bytes", len, remainLength);
            len = remainLength;
        }
        int64_t totalRead = 0;
        do {
            int64_t readSize = std::min(len - totalRead, (uint64_t)(blockSize() - posInLba));
            fileStream.read(bufWrite + totalRead, readSize);
            totalRead += fileStream.gcount();
            if (fileStream.gcount() != readSize)
            {
                LogWarning("Read buffer underrun! Wanted %" PRId64 " bytes, got %" PRId64, readSize, fileStream.gcount());
                return totalRead;
            }
            posInLba += readSize;
            if (posInLba >= blockSize())
            {
                posInLba = 0;
                lbaCurrent += 1;
                //fileStream.seekg(lbaToByteOffset(lbaCurrent), std::ios::beg);
                seek((lbaCurrent - lbaStart) * blockSize());
            }
        } while (len > totalRead);
        return totalRead;
    }

    int seek(uint64_t offset)
    {
        if (offset > length)
        {
            return 0;
        }
        // FIXME: This assumes the offset is more or less *sane*
        uint32_t blockOffset = offset / blockSize();
        uint32_t posInBlock = offset % blockSize();

        lbaCurrent = lbaStart + blockOffset;
        posInLba = posInBlock;
        uint64_t binOffset = lbaToByteOffset(lbaCurrent) + posInLba;
        fileStream.seekg(binOffset, std::ios::beg);
        return fileStream.good();
    }

    PHYSFS_sint64 tell()
    {
        //return lbaToByteOffset(lbaCurrent - lbaStart) + posInLba;
        return blockSize() * (lbaCurrent - lbaStart) + posInLba;
    }

    CueIO(const CueIO& other) : imageFile(other.imageFile),
                                lbaStart(other.lbaStart), lbaCurrent(other.lbaCurrent), posInLba(other.posInLba),
                                length(other.length), fileType(other.fileType), trackMode(other.trackMode)
    {
        fileStream.open(imageFile.str(), std::ios::in | std::ios::binary);
        fileStream.seekg(lbaToByteOffset(lbaStart) + posInLba);
    }

    ~CueIO()
    {
        fileStream.close();
    }

    static PHYSFS_Io *createIo()
    {
        return new PHYSFS_Io{
                0,
                nullptr,
                CueIO_read,
                CueIO_write,
                CueIO_seek,
                CueIO_tell,
                CueIO_length,
                CueIO_duplicate,
                CueIO_flush,
                CueIO_destroy
        };
    }

  public:
    static PHYSFS_sint64 CueIO_read(PHYSFS_Io *io, void *buffer, PHYSFS_uint64 len)
    {
        CueIO *cio = (CueIO *)io->opaque;
        return cio->read(buffer, len);
    }

    // We always ignore write requests
    static PHYSFS_sint64 CueIO_write(PHYSFS_Io *io, const void *buffer, PHYSFS_uint64 len)
    {
        std::ignore = io;
        std::ignore = buffer;
        std::ignore = len;
        return -1;
    }

    static int CueIO_seek(PHYSFS_Io *io, PHYSFS_uint64 offset)
    {
        CueIO *cio = (CueIO *)io->opaque;
        return cio->seek(offset);
    }

    static PHYSFS_sint64 CueIO_tell(PHYSFS_Io *io)
    {
        CueIO *cio = (CueIO *)io->opaque;
        return cio->tell();
    }

    static PHYSFS_sint64 CueIO_length(PHYSFS_Io *io)
    {
        CueIO *cio = (CueIO *)io->opaque;
        return cio->length;
    }

    // A note on io->duplicate:
    // The physfs.h doc-comment states that duplicate should return a
    // "new value for a stream's (opaque) field", but that's actually
    // not true (according to implementations in the code).
    // In fact you have to construct a new PHYSFS_Io object, with no
    // dependencies on the old one.
    static PHYSFS_Io *CueIO_duplicate(PHYSFS_Io *io)
    {
        CueIO *cio = (CueIO *)io->opaque;
        // Just go ahead and construct a new file stream
        PHYSFS_Io *retval = createIo();
        // Set the appropriate fields
        io->opaque = new CueIO(*cio);
        return retval;
    }

    static int CueIO_flush(PHYSFS_Io *io)
    {
        std::ignore = io;
        return 1;
    }

    static void CueIO_destroy(PHYSFS_Io *io)
    {
        CueIO *cio = (CueIO *)io->opaque;
        delete cio;
        delete io;
    }

    static PHYSFS_Io *getIo(UString fileName, uint32_t lba, int64_t length, CUE_FileType ftype, CUE_TrackMode tmode)
    {
        CueIO *cio = new CueIO(fileName, lba, length, ftype, tmode);
        if (!cio->fileStream)
        {
            delete cio;
            return nullptr;
        }
        PHYSFS_Io *io = createIo();
        io->opaque = cio;
        return io;
    }

};

const int PHYSFS_API_VERSION = 0;
class CueArchiver
{
private:
    UString imageFile;
    CUE_FileType fileType;
    CUE_TrackMode trackMode;

    CueIO *cio;
    const PHYSFS_Archiver *isoArchiver;

    struct IsoVolumeDescriptor
    {
        uint8_t type;
        uint8_t identifier[5];
        uint8_t version;
        uint8_t ___padding; // This field is here to avoid alignment issues.
                            // It's only used in the boot volume descriptor, and
                            // therefore not interesting to us.
        union
        {
            // Better not even try this one
            /*struct
            {
                uint8_t bootSystemIdentifier__unused[31];
                uint8_t bootIdentifier__unused[32];
                uint8_t bootSystemUse__unused[1977];
            } boot;*/
            struct
            {
                //uint8_t __unused; // Disabled due to ___padding being there
                char sysIdentifier[32];
                char volIdentifier[32];
                uint8_t __unused_8[8];
                int32_lsb_msb volSpaceSize;
                uint8_t __unused_32[32];
                int16_lsb_msb volSetSz;
                int16_lsb_msb volSeqNr;
                int16_lsb_msb lbs;
                int32_lsb_msb pathTblSz;
                uint32_t pathTblLLoc;
                uint32_t optPathTblLLoc;
                uint32_t pathTblMLoc;
                uint32_t optPathTblMLoc;
                uint8_t rootDirEnt[34];
                char volSetIdentifier[128];
                char publisherIdentifier[128];
                char dataPrepIdentifier[128];
                char appIdentifier[128];
                char copyrightIdentifier[38];
                char abstractFileId[36];
                char biblioFileId[37];
                dec_datetime volCreationTime;
                dec_datetime volModificationTime;
                dec_datetime volExpirationTime;
                dec_datetime volEffectiveTime;
                uint8_t fileStructureVersion;
                uint8_t __unused1;
                uint8_t __app_defined__unused[512];
                uint8_t __reserved[653];
            } primary;
            // Supplementary volume descriptor is ignored completely
            struct
            {
                uint8_t __padding[2040];
            } terminator;
        };
    };

    // NOTE: this would have all sorts of alignment issues if used with proper types!
    struct IsoDirRecord_hdr
    {
        uint8_t length;
        uint8_t xarLength;
        uint8_t extentLoc[8]; // cast to int32_lsb_msb
        uint8_t extentLength[8]; // cast to int32_lsb_msb
        dir_datetime recTime;
        uint8_t flags;
        uint8_t fuSize;
        uint8_t gapSize;
        uint8_t volSeqNumber[4]; // cast to int16_lsb_msb
        uint8_t fnLength;
        // That's a bit of a hack, since actual filename might be sized differently
        char fileName[222];
    };

    enum FSEntryFlags
    {
        FSFLAG_HIDDEN = 0x01,
        FSFLAG_DIRENT = 0x02,
        FSFLAG_ASFILE = 0x04,
        FSFLAG_XATTRINFO = 0x08,
        FSFLAG_XATTRPERM = 0x10,
        FSFLAG_RESERVED1 = 0x20,
        FSFLAG_RESERVED2 = 0x40,
        FSFLAG_NOTFINAL = 0x80
    };

    struct FSEntry
    {
        UString name;
        enum {
            FS_FILE,
            FS_DIRECTORY
        } type;
        uint32_t offset;
        uint64_t length;
        int64_t timestamp;
        std::map<UString, FSEntry> children;
    } root;

    void readDir(const IsoDirRecord_hdr &dirRecord, FSEntry &parent)
    {
        int32_lsb_msb lm_location;
        int32_lsb_msb lm_length;
        dir_datetime d_datetime;
        std::memcpy(&lm_location, dirRecord.extentLoc, sizeof(int32_lsb_msb));
        std::memcpy(&lm_length, dirRecord.extentLength, sizeof(int32_lsb_msb));
        std::memcpy(&d_datetime, &dirRecord.recTime, sizeof(dir_datetime));
        uint32_t location = lm_location.val;
        int32_t length = lm_length.val;
        int32_t readpos = 0;
        char* semicolonPos = std::strrchr((char*)dirRecord.fileName, ';');
        if (semicolonPos) { *semicolonPos = '\0'; } // Ignore the semicolon and everything after it
        parent.name = dirRecord.fileName;
        parent.length = length;
        parent.offset = location;
        parent.timestamp = d_datetime.toUnixTime();
#if 0 // Stop archiver from being extremely chatty
        LogInfo("Adding entry: %s", parent.name.c_str());
        LogInfo("  Location %" PRIu64, parent.offset);
        LogInfo("  Length: %" PRIu64, parent.length);
#endif
        if (!(dirRecord.flags & FSFLAG_DIRENT)) {
            parent.type = FSEntry::FS_FILE;
            return;
        } // Next portion is directory-specific
        parent.type = FSEntry::FS_DIRECTORY;
        IsoDirRecord_hdr childDirRecord;
#if 0
        LogInfo("Recursing into: %s (location: %d)", parent.name.c_str(), location);
#endif
        cio->seek(cio->blockSize() * location + readpos);
        do
        {
            // Find a non-empty record
            do
            {
                // Each record starts at an even offset
                if (readpos % 2)
                {
                    readpos += 1;
                    cio->seek(cio->tell() + 1);
                }
                // Read first 33 bytes containing everything but the name
                readpos += cio->read(&childDirRecord, 33);
                // We can safely bail out if we get over the record length
                if (readpos >= length)
                    return;
                // We've read an empty record, that's fine
                if (childDirRecord.length == 0)
                {
                    // Check if we crossed the boundary
                    if (readpos % cio->blockSize() < 33)
                    {
                        // Yup, sure did.
                        readpos -= readpos % cio->blockSize();
                    }
                    cio->seek(cio->blockSize() * location + readpos);
                }
            } while(childDirRecord.length == 0);

            if (childDirRecord.fnLength > 0)
            {
                readpos += cio->read(childDirRecord.fileName, childDirRecord.fnLength);
                childDirRecord.fileName[childDirRecord.fnLength] = '\0';
            }
            else
            {
                continue;
            }
            // Each (?) directory on a CD has a "this directory" and "parent directory" entries, we just ignore them
            if (!std::isalnum(childDirRecord.fileName[0]))
            {
                continue;
            }

            // Now decode what we've read
            FSEntry childEntry;
            int64_t pos = cio->tell();
            readDir(childDirRecord, childEntry);
            // Reset reading position
            cio->seek(pos);
            //cio->seek(cio->blockSize() * location + readpos);
            parent.children[childEntry.name] = childEntry;
        } while ((childDirRecord.length > 0));
    }

    static_assert(sizeof(IsoVolumeDescriptor) == 2048, "Unexpected volume size!");
    static_assert(sizeof(IsoDirRecord_hdr) == 255, "Unexpected direntry size!");
    static_assert(offsetof(IsoDirRecord_hdr, fnLength) == 32, "Unexpected filename offset!");
    CueArchiver(UString fileName, CUE_FileType ftype, CUE_TrackMode tmode) : imageFile(fileName), fileType(ftype), trackMode(tmode)
    {
        // "Hey, a .cue-.bin file pair should be really easy to read!" - sfalexrog, 15.04.2016
        boost::filesystem::path filePath(fileName.c_str());
        // FIXME: This fsize is completely and utterly wrong - unless you're reading an actual iso (mode1_2048)
        uint64_t fsize = boost::filesystem::file_size(filePath);
        LogInfo("Opening file %s of size %" PRIu64, fileName.c_str(), fsize);
        cio = new CueIO(fileName, 0, fsize, ftype, tmode);
        if (!cio->fileStream)
        {
            LogError("Could not open file: bad stream!");
        }
        cio->seek(cio->blockSize() * 16);
        LogInfo("Reading ISO volume descriptor");
        IsoVolumeDescriptor descriptor;
        cio->read(&descriptor, sizeof(descriptor));
        LogInfo("CD magic: %c, %c, %c, %c, %c", descriptor.identifier[0],
                descriptor.identifier[1],
                        descriptor.identifier[2],
                        descriptor.identifier[3],
                        descriptor.identifier[4]);
        const char magic[] = {'C', 'D', '0', '0', '1'};
        if (std::memcmp((void*)magic, (void*)descriptor.identifier, 5))
        {
            LogError("Bad CD magic!");
        }
        LogInfo("Descriptor type: %d", (int)descriptor.type);
        IsoDirRecord_hdr rootRecord;
        std::memcpy(&rootRecord, descriptor.primary.rootDirEnt, 34);
        LogInfo("Volume ID: %s", descriptor.primary.volIdentifier);
        LogInfo("Root dirent length: %d", (int)rootRecord.length);
        readDir(rootRecord, root);
    }
    ~CueArchiver()
    {
        delete cio;
    }

    const FSEntry *getFsEntry(const char* name) const
    {
        const FSEntry *current = &root;
        UString dname = name;
        if (dname.length() > 0)
        {
            auto pathParts = dname.split("/");
            for (auto ppart = pathParts.begin(); ppart != pathParts.end(); ppart++)
            {
                auto subdir = current->children.find(*ppart);
                if (subdir == current->children.end())
                {
                    // Not a valid directory, fail fast.
                    PHYSFS_setErrorCode(PHYSFS_ERR_NOT_FOUND);
                    return nullptr;
                }
                // Go into specified subdirectory
                current = &(subdir->second);
            }
        }
        return current;
    }

    void enumerateFiles(const char *dirname, PHYSFS_EnumFilesCallback cb,
                        const char *origdir, void *callbackdata)
    {
        const FSEntry *current = getFsEntry(dirname);
        if (!current) return;
        if (current->type == FSEntry::FS_DIRECTORY) {
            for (auto entry = current->children.begin(); entry != current->children.end(); entry++)
            {
                cb(callbackdata, origdir, entry->first.c_str());
            }
        }
    }

    PHYSFS_Io *openRead(const char *fnm)
    {
        const FSEntry *entry = getFsEntry(fnm);
        if (!entry || (entry->type == FSEntry::FS_DIRECTORY))
        {
            return nullptr;
        }
        return CueIO::getIo(imageFile, entry->offset, entry->length, fileType, trackMode);
    }

    int stat(const char *name, PHYSFS_Stat *stat)
    {
        const FSEntry *current = getFsEntry(name);
        if (!current)
        {
            return 0;
        }

        stat->readonly = 1;
        stat->accesstime = current->timestamp;
        stat->createtime = current->timestamp;
        stat->modtime = current->timestamp;
        switch(current->type)
        {
            case FSEntry::FS_FILE:
                stat->filetype = PHYSFS_FILETYPE_REGULAR;
                stat->filesize = current->length;
                break;
            case FSEntry::FS_DIRECTORY:
                stat->filetype = PHYSFS_FILETYPE_DIRECTORY;
                stat->filesize = 0;
                break;
            default:
                // Well, this should never happen?
                LogError("Unexpected FSEntry::type value!");
        }
        return 1;
    }


public:

    static void *Cue_OpenArchive(PHYSFS_Io *io, const char *filename, int forWriting)
    {
        // FIXME: Here we assume the filename actually points to the actual .cue file,
        // ignoring the PHYSFS_Io (though how would we even read the accompanying file?)
        // TODO: Actually read from PHYSFS_Io to allow mounting non-CUE images?
        if (!filename)
        {
            LogError("FIXME: Cannot operate on purely-PhysFS_Io archives (need a filename)");
            return nullptr;
        }

        if (forWriting)
        {
            LogError("Cue files cannot be written to");
            return nullptr;
        }

        CueParser parser(filename);
        if (!parser.isValid())
        {
            LogError("Could not parse file \"%s\"", filename);
            return nullptr;
        }
        boost::filesystem::path cueFilePath(filename);

        boost::filesystem::path dataFilePath(cueFilePath.parent_path());//parser.getDataFileName().c_str());
        dataFilePath /= parser.getDataFileName().c_str();

        if (!boost::filesystem::exists(dataFilePath))
        {
            LogError("Binary file does not exist: \"%s\"", dataFilePath.c_str());
            return nullptr;
        }

        return new CueArchiver(dataFilePath.string(), parser.getDataFileType(), parser.getTrackMode());
    }

    static void Cue_EnumerateFiles(void *opaque, const char *dirname, PHYSFS_EnumFilesCallback cb,
                                   const char *origdir, void *callbackdata)
    {
        CueArchiver *archiver = (CueArchiver*) opaque;
        archiver->enumerateFiles(dirname, cb, origdir, callbackdata);
    }

    static PHYSFS_Io *Cue_OpenRead(void *opaque, const char *fnm)
    {
        CueArchiver *archiver = (CueArchiver*) opaque;
        return archiver->openRead(fnm);
    }

    static PHYSFS_Io *Cue_OpenWrite(void *opaque, const char *filename)
    {
        std::ignore = opaque;
        std::ignore = filename;
        PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
        return nullptr;
    }

    static PHYSFS_Io *Cue_OpenAppend(void *opaque, const char *filename)
    {
        std::ignore = opaque;
        std::ignore = filename;
        PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
        return nullptr;
    }

    static int Cue_Remove(void *opaque, const char *filename)
    {
        std::ignore = opaque;
        std::ignore = filename;
        PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
        return 0;
    }

    static int Cue_Mkdir(void *opaque, const char *filename)
    {
        std::ignore = opaque;
        std::ignore = filename;
        PHYSFS_setErrorCode(PHYSFS_ERR_READ_ONLY);
        return 0;
    }

    static int Cue_Stat(void *opaque, const char *fn, PHYSFS_Stat *stat)
    {
        CueArchiver *archiver = (CueArchiver*) opaque;
        return archiver->stat(fn, stat);
    }

    static void Cue_CloseArchive(void *opaque)
    {
        CueArchiver *archiver = (CueArchiver*) opaque;
        delete archiver;
    }

    static PHYSFS_Archiver *createArchiver()
    {
        static PHYSFS_Archiver cueArchiver = {
                PHYSFS_API_VERSION,
                {
                        "CUE",
                        "Cuesheet-Backed Image File",
                        "Alexey Rogachevsky <sfalexrog@gmail.com>",
                        "https://github.com/sfalexeog",
                        0 // supportsSymlinks
                },
                Cue_OpenArchive,
                Cue_EnumerateFiles,
                Cue_OpenRead,
                Cue_OpenWrite,
                Cue_OpenAppend,
                Cue_Remove,
                Cue_Mkdir,
                Cue_Stat,
                Cue_CloseArchive
        };
        return &cueArchiver;
    }

};

} /* anonymous namespace */

namespace OpenApoc
{
    void parseCueFile(UString fileName)
    {
        CueParser parser(fileName);
        LogInfo("Parser status: %d", parser.isValid());
        LogInfo("Data file: %s", parser.getDataFileName().c_str());
        LogInfo("Track mode: %d", parser.getTrackMode());
        LogInfo("File mode: %d", parser.getDataFileType());
    }

    PHYSFS_Archiver *getCueArchiver()
    {
        return CueArchiver::createArchiver();
    }

}
