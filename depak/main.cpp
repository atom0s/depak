/**
 * Kingdoms of Amalur: Re-Reckoning PAK Dumper
 * (c) 2020 atom0s [atom0s@live.com]
 *
 * Proof of concept to dump the on-disk PAK files.
 *
 * Does not support all PAK formats.
 * Does not dump special entries.
 */
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "aplib.lib")
#include "aplib.h"

/**
 * PAK Header Structure
 *
 */
struct pakheader_t
{
    uint32_t Signature;     // The file type signature.
    uint32_t IsValid;       // Flag to determine if the file should be processed.
    uint32_t Unknown00;     // Unknown - 0x00000010 - Used for the header-skip alignment for reading entries.
    uint32_t Unknown01;     // Unknown - 0x00000100 - Used for the decompression alignment block sizes.
    uint64_t EntriesOffset; // Offset to the block of entry information.
    uint32_t Unknown02;     // Unknown - 0x00000000
    uint32_t Unknown03;     // Unknown - 0x00000000
};

/**
 * PAK File Entry Structure
 *
 */
struct pakfileentry_t
{
    uint32_t Crc;      // Used as the file name id which links to the string table id.
    uint32_t Position; // The position where the file data block is stored.
    uint32_t Size;     // The size of the file.
};

/**
 * PAK File Name Structure
 *
 */
struct pakfilename_t
{
    uint32_t FileId;   // Links to the file entry crc.
    uint32_t NameSize; // The size of the file name.
    char Name[];       // The file name.
};

/**
 * PAK File Format Enumeration
 *
 */
enum PakFileType
{
    CompressedBE      = 0x4B504B62,
    CompressedLE      = 0x6C4B504B,
    UncompressedBE    = 0x624B4150,
    UncompressedLE    = 0x6C4B4150,
    KaikoCompressedBE = 0x6252414B,
    KaikoCompressedLE = 0x6C52414B,
};

/**
 * Saves a compressed file from a parent PAK file.
 *
 * @param {FILE*} f - The opened file pointer.
 * @param {std::string&} name - The file name.
 * @param {uint64_t} offset - The offset to the file data.
 * @param {uint32_t} size - The size of the file.
 */
void save_compressed_file(FILE* f, const std::string& name, const uint64_t offset, const uint32_t size)
{
    // Step the file to the entry location..
    _fseeki64(f, offset, SEEK_SET);

    // Read the compressed file information..
    uint32_t fileSize = 0;
    uint32_t chunks   = 0;
    fread(&fileSize, 4, 1, f);
    fread(&chunks, 4, 1, f);

    // Read and process the compressed data chunks..
    std::vector<uint32_t> chunkSizes;
    if (chunks > 0)
    {
        // Read the chunk sizes table..
        for (std::size_t x = 0; x < chunks; x++)
        {
            uint32_t chunk = 0;
            fread(&chunk, 4, 1, f);
            chunkSizes.push_back(chunk);
        }

        // Read and decompress the chunks..
        std::vector<uint8_t> fileData;
        for (std::size_t x = 0; x < chunks; x++)
        {
            std::vector<uint8_t> bufferEnc(chunkSizes[x], u8'\0');
            std::vector<uint8_t> bufferDec(4096, u8'\0');

            // Read the current chunk encrypted data..
            fread(bufferEnc.data(), 1, chunkSizes[x], f);

            // Decompress the chunk data..
            auto decSize = aP_depack_asm(bufferEnc.data(), bufferDec.data());
            fileData.insert(fileData.end(), bufferDec.begin(), bufferDec.begin() + decSize);
        }

        // Save the decompressed file..
        char filePath[MAX_PATH]{};
        sprintf_s(filePath, u8"dump//%s", name.c_str());

        FILE* out = nullptr;
        if (fopen_s(&out, filePath, u8"wb") != ERROR_SUCCESS)
            printf_s(u8"[!] Error: Failed to dump file: %s\r\n", filePath);
        else
        {
            fwrite(fileData.data(), fileData.size(), 1, out);
            fclose(out);
        }
    }
}

/**
 * Unsupported PAK file processor.
 */
void process_pak_unsupported(void)
{
    printf_s(u8"[!] Error: PAK file type unsupported!\r\n");
}

/**
 * PAK file processor for the file type: PakFileType::KaikoCompressedLE
 * 
 * @param {FILE*} f - The opened file pointer.
 * @param {long long} fileSize - The total size of the opened file.
 * @param {pakheader_t*} header - The parsed PAK header.
 */
void process_pak_karl(FILE* f, const long long fileSize, const pakheader_t* header)
{
    // Validate the incoming information..
    if (f == nullptr || fileSize == 0 || header->IsValid == 0)
    {
        printf_s(u8"[!] Error: Invalid PAK information; cannot process.\r\n");
        return;
    }

    printf_s(u8"[!] Info: Processing PAK file type: Kaiko Compressed (Little Endian)\r\n\r\n");

    std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> fileEntries;
    std::vector<std::tuple<uint32_t, std::string>> stringEntries;

    // Step the file to the entry table..
    _fseeki64(f, header->EntriesOffset, SEEK_SET);

    // Read the entry table information..
    uint32_t eCount = 0; // The count of entries..
    uint32_t sCount = 0; // The count of special entries..
    fread(&eCount, 4, 1, f);
    fread(&sCount, 4, 1, f);

    printf_s(u8"[!] Info: Entry Count: %d\r\n", eCount);
    printf_s(u8"[!] Info: Entry Count: %d (Special)\r\n\r\n", sCount);

    // Process the entries..
    if (eCount > 0)
    {
        printf_s(u8"[!] Info: Parsing entries table...\r\n");

        auto count = eCount;

        do
        {
            // Read the current entry..
            pakfileentry_t entry{};
            fread(&entry, sizeof(entry), 1, f);

            printf_s(u8"[!] Info: Entry found: (Crc: %08X)(Pos: %08X)(Size: %08X)\r\n", entry.Crc, entry.Position, entry.Size);

            // Store the entry information..
            fileEntries.push_back({entry.Crc, entry.Position, entry.Size});
            count--;
        } while (count > 0);

        // Sort the file list by its file position..
        std::sort(fileEntries.begin(), fileEntries.end(), [](const std::tuple<uint32_t, uint32_t, uint32_t>& a, const std::tuple<uint32_t, uint32_t, uint32_t>& b) -> bool {
            return std::get<1>(a) < std::get<1>(b);
        });
    }

    // Process the special entries..
    if (sCount > 0)
    {
        printf_s(u8"[!] Info: Parsing special entries table...\r\n");
        printf_s(u8"[!] Warning: Special entries are not currently supported.\r\n");
    }

    // Process the string table entries (if available)..
    if (eCount > 0)
    {
        printf_s(u8"[!] Info: Parsing strings table for file names...\r\n");

        // Obtain the string table entry..
        auto fileEntry = fileEntries.back();
        fileEntries.pop_back();

        // Step the file to the string entry table..
        _fseeki64(f, (uint64_t)std::get<1>(fileEntry) * header->Unknown00, SEEK_SET);

        // Read the string table header..
        uint32_t tSize = 0; // The string table size..
        uint32_t unk00 = 0; // Unknown (Padding?)
        fread(&tSize, 4, 1, f);
        fread(&unk00, 4, 1, f);

        // Validate the string table size..
        if (tSize == 0)
        {
            printf_s(u8"[!] Error: Invalid string table size; cannot continue to parse.\r\n");
            return;
        }

        uint32_t sSize = 0;

        // Parse the string table..
        do
        {
            // Read the file name data..
            pakfilename_t name{0, 0};
            fread(&name, sizeof(pakfilename_t), 1, f);

            // Read the file name..
            std::string fname(name.NameSize, u8'\0');
            fread(fname.data(), 1, name.NameSize, f);

            // Store the name entry..
            stringEntries.push_back({name.FileId, fname});
            sSize += sizeof(pakfilename_t) + name.NameSize;
        } while (sSize < tSize);
    }

    // Create the output dump folder..
    ::CreateDirectory(u8"dump", nullptr);

    // Finally, dump the files to disc with their proper names..
    std::size_t unknownFileCount = 0;
    std::for_each(fileEntries.begin(), fileEntries.end(), [&f, &header, &stringEntries, &unknownFileCount](const std::tuple<uint32_t, uint32_t, uint32_t>& e) {
        // Obtain the files name if available..
        const auto sentry = std::find_if(stringEntries.begin(), stringEntries.end(), [&e](const std::tuple<uint32_t, std::string>& se) -> bool { return std::get<0>(se) == std::get<0>(e); });
        auto name         = sentry != stringEntries.end() ? std::get<1>(*sentry) : u8"";

        // Construct an invalid file name if one was not found..
        if (name.length() == 0)
        {
            char fileName[MAX_PATH]{};
            sprintf_s(fileName, u8"%08X.unknown_file", unknownFileCount);
            name = fileName;

            unknownFileCount++;
        }

        printf_s(u8"[!] Info: Saving file: %s\r\n", name.c_str());

        // Dump the file..
        save_compressed_file(f, name, (uint64_t)std::get<1>(e) * header->Unknown00, std::get<2>(e));
    });
}

/**
 * Application entry point.
 * 
 * @param {int32_t} argc - The count of parameters passed to the application.
 * @param {char*[]} argv - The array of parameters passed to the application.
 * @return {int32_t} Non-important return value.
 */
int32_t __cdecl main(int32_t argc, char* argv[])
{
    printf_s(u8"Kingdoms of Amalur: Rereckoning PAK Dumper\r\n");
    printf_s(u8"(c) 2020 atom0s [atom0s@live.com]\r\n\r\n");
    printf_s(u8"Personal site: https://atom0s.com/\r\n");
    printf_s(u8"Donations    : https://paypal.me/atom0s\r\n\r\n");

    // Validate the incoming requested PAK file to dump..
    if (argc < 2 || ::GetFileAttributes(argv[1]) == INVALID_FILE_ATTRIBUTES)
    {
        printf_s(u8"[!] Error: No input file given.\r\n");
        return 0;
    }

    // Open the given file for reading..
    FILE* f = nullptr;
    if (fopen_s(&f, argv[1], u8"rb") != ERROR_SUCCESS)
    {
        printf_s(u8"[!] Error: Failed to open PAK file for reading.\r\n");
        return 0;
    }

    // Obtain the total file size..
    _fseeki64(f, 0, SEEK_END);
    const auto size = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    // Validate the size is big enough for a PAK file header at least..
    if (size < sizeof(pakheader_t))
    {
        fclose(f);

        printf_s(u8"[!] Error: Invalid file size; cannot parse PAK file.\r\n");
        return 0;
    }

    // Read the PAK header..
    pakheader_t header{};
    fread(&header, sizeof(pakheader_t), 1, f);

    // Process the PAK file based on its signature type..
    switch (header.Signature)
    {
        case PakFileType::KaikoCompressedLE:
            process_pak_karl(f, size, &header);
            break;

        // Unsupported formats..
        default:
            process_pak_unsupported();
            break;
    }

    printf_s(u8"\r\n\r\nDone!\r\n\r\n");

    fclose(f);
    return 0;
}
