#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <zlib.h>
#include <sstream>
#include <vector>
#include <openssl/sha.h>
#include <iomanip>
#include <unordered_map>
#include <algorithm>

/**
 * Reads and decompresses data from a blob object, then processes it to extract the actual content.
 * Note: Blobs only store the contents of a file, not its name or permissions.
 * The formate of a blob object looks like this: blob <size>\0<content> (size is in bytes)
 *
 * This function performs the following steps:
 *
 * 1. **Open the File**:
 *    - Opens the file specified by the `file_path` parameter in binary mode for reading.
 *    - If the file cannot be opened, an error message is printed, and an empty string is returned.
 *
 * 2. **Read Compressed Data**:
 *    - Reads the entire content of the file into a `std::string` named `compressed_data`.
 *    - The file is read using an input stream iterator that iterates through each character in binary mode.
 *
 * 3. **Initialize zlib Stream**:
 *    - Initializes a `z_stream` structure for decompression with default values.
 *    - Sets the input size (`avail_in`) and the pointer to the compressed data (`next_in`).
 *    - Configures `zalloc`, `zfree`, and `opaque` to `Z_NULL` (default memory management).
 *    - Calls `inflateInit` to set up the decompression stream.
 *    - If initialization fails, an error message is printed, and an empty string is returned.
 *
 * 4. **Prepare for Decompression**:
 *    - Allocates a buffer (`std::vector<char>`) to hold decompressed data.
 *    - Uses a loop to call `inflate`, decompressing the data in chunks:
 *      - **Set Output Buffer**: Defines the size and location for decompressed data (`avail_out` and `next_out`).
 *      - **Call `inflate`**: Processes data from the input buffer and writes it to the output buffer.
 *      - **Check for Errors**: Handles any errors during decompression (e.g., stream error, data error, memory error).
 *      - **Append Data**: Adds the decompressed data from the buffer to the `decompressed_data` string.
 *      - Continues processing until all compressed data is decompressed (`Z_STREAM_END`).
 *    - Cleans up the zlib stream using `inflateEnd`.
 *
 * 5. **Process Decompressed Data**:
 *    - Initializes a `std::istringstream` with the decompressed data to facilitate further processing.
 *    - Reads the header part of the data using `std::getline` until a null terminator (`'\0'`).
 *    - Reads the remaining part of the data, which is the actual content, into a `std::string`.
 *
 * 6. **Return Actual Data**:
 *    - Returns the string containing the actual content after extracting the header.
 *
 * The function is designed to handle compressed Git object data and separate out the content from the header.
 */
std::string decompress_zlib_data(std::string &file_path, bool remove_header) {
    // Open the file in binary mode for reading.
    // std::ifstream is used for reading from files.
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) { // Check if the file was successfully opened.
        std::cerr << "Failed to open file: " << file_path << "\n"; // Print an error message if the file cannot be opened.
        return ""; // Return an empty string to indicate failure.
    }

    // Read the compressed data from the file into a string.
    // Using std::istreambuf_iterator to iterate through the file's content and store it in a string.
    std::string compressed_data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    file.close(); // Close the file as it is no longer needed.

    // Initialize a zlib stream structure (z_stream) to manage the decompression process.
    z_stream strm{};
    strm.avail_in = compressed_data.size(); // Set the number of bytes available at the input.

    // The zlib library functions `deflate` and `inflate` work with `Bytef*` pointers for input and output data. 
    // In C++, `std::string::data()` returns a `char*` pointer, which needs to be explicitly cast to `Bytef*` 
    // to match the expected type for zlib's functions. 
    // `reinterpret_cast` is used here to perform this type conversion safely and clearly, 
    // ensuring that the `char*` pointer is interpreted correctly as `Bytef*` for the zlib operations. 

    strm.next_in = reinterpret_cast<Bytef*>(compressed_data.data()); // Pointer to the first byte of the input data to be decompressed.
    strm.zalloc = Z_NULL; // Use the default memory allocation function.
    strm.zfree = Z_NULL; // Use the default memory deallocation function.
    strm.opaque = Z_NULL; // No user-defined data is needed.

    // Initialize the zlib decompression process.
    // inflateInit prepares the z_stream structure for decompression.
    if (inflateInit(&strm) != Z_OK) { // Check if initialization was successful.
        std::cerr << "Failed to initialize zlib stream\n"; // Print an error message if initialization fails.
        return ""; // Return an empty string to indicate failure.
    }

    // Prepare a buffer to hold decompressed data chunks.
    // The buffer size is set to 4096 bytes (4 KB).
    std::vector<char> buffer(4096);
    std::string decompressed_data; // String to accumulate the decompressed data.

    // Decompress the data in a loop until all data has been processed.
    int ret;
    do {
        strm.avail_out = buffer.size(); // Set the number of bytes available in the output buffer.
        strm.next_out = reinterpret_cast<Bytef*>(buffer.data()); // Pointer to the start of the output buffer.

        // Call the inflate function to decompress the data.
        // Z_NO_FLUSH tells inflate not to flush the output data unless absolutely necessary.
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) { // Check for decompression errors.
            inflateEnd(&strm); // Clean up the zlib stream in case of an error.
            std::cerr << "Error during decompression\n"; // Print an error message if decompression fails.
            return ""; // Return an empty string to indicate failure.
        }

        // Append the decompressed data to the result string.
        // Only append the bytes that were actually written to the buffer (buffer.size() - strm.avail_out).
        decompressed_data.append(buffer.data(), buffer.size() - strm.avail_out);

    } while (ret != Z_STREAM_END); // Continue decompressing until all data has been processed.

    // Clean up the zlib stream, releasing any allocated resources.
    inflateEnd(&strm);

    // Process the decompressed data to extract the actual content, ignoring the Git object header.
    std::istringstream stream(decompressed_data); // Create a string stream from the decompressed data.
    if(remove_header) {
        std::string header;
        std::getline(stream, header, '\0'); // Read the header from the string stream, stopping at the null terminator.
    }

    // The remaining part of the data is the actual content after the header.
    std::string actual_data;
    std::getline(stream, actual_data); // Read the actual content from the string stream.
    return actual_data; // Return the actual decompressed content.
}

/**
 * Compresses the contents of a file and formats it as a Git-style blob object.
 * The format of a blob object looks like this: blob <size>\0<content> (size is in bytes)
 *
 * This function performs the following steps:
 *
 * 1. **Open the File**:
 *    - Opens the file specified by the `file_path` parameter in binary mode for reading.
 *    - If the file cannot be opened, an error message is printed, and an empty string is returned.
 *
 * 2. **Read File Data**:
 *    - Reads the entire content of the file into a `std::vector<char>` named `file_data`.
 *    - The file is read using an input stream iterator that iterates through each character in binary mode.
 *    - The `file_data` vector will hold the raw bytes read from the file.
 *
 * 3. **Create Git-style Header**:
 *    - Constructs a header string in the Git object format: "blob <size>\0".
 *    - `file_data.size()` gives the size of the file content, which is converted to a string and appended after "blob ".
 *    - The header is terminated with a null character (`'\0'`).
 *
 * 4. **Combine Header and Data**:
 *    - Combines the Git-style header and the file data into a single `std::string` called `full_data`.
 *    - This string will be the input data for compression.
 *
 * 5. **Initialize zlib Stream**:
 *    - Initializes a `z_stream` structure for compression with default values.
 *    - Sets the input size (`avail_in`) and the pointer to the data to be compressed (`next_in`).
 *    - The `reinterpret_cast<Bytef*>(full_data.data())` converts the `char*` pointer from `std::string::data()` to `Bytef*` needed by zlib.
 *    - Configures `zalloc`, `zfree`, and `opaque` to `Z_NULL` (default memory management).
 *    - Calls `deflateInit` to set up the compression stream with zlib's best compression setting (`Z_BEST_COMPRESSION`).
 *    - If initialization fails, an error message is printed, and an empty string is returned.
 *
 * 6. **Prepare for Compression**:
 *    - Allocates a buffer (`std::vector<char>`) to hold compressed data chunks.
 *    - Uses a loop to call `deflate`, compressing the data in chunks:
 *      - **Set Output Buffer**: Defines the size and location for compressed data (`avail_out` and `next_out`).
 *      - **Call `deflate`**: Processes data from the input buffer and writes compressed data to the output buffer.
 *      - **Check for Errors**: Handles any errors during compression (e.g., stream error).
 *      - **Append Data**: Adds the compressed data from the buffer to the `compressed_data` string.
 *      - Continues processing until all data has been compressed (`strm.avail_out` is not zero).
 *    - Cleans up the zlib stream using `deflateEnd`.
 *
 * 7. **Return Compressed Data**:
 *    - Returns the string containing the compressed data after the compression process is completed.
 *
 * This function is designed to handle file data and compress it in the Git object format.
 */
std::string compress_blob_data(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: File '" << file_path << "' not found." << std::endl;
        return "";
    }

    // Read file contents into a vector
    std::vector<char> file_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Create the Git-style header
    std::string header = "blob " + std::to_string(file_data.size()) + '\0';

    // Combine the header and file data
    std::string full_data = header + std::string(file_data.begin(), file_data.end());

    // Initialize zlib stream
    z_stream strm{};
    strm.avail_in = full_data.size(); // Set the number of bytes available at the input.
    strm.next_in = reinterpret_cast<Bytef*>(full_data.data()); // Pointer to the first byte of the input data to be compressed.

    // Initialize the compression with zlib's best compression
    if (deflateInit(&strm, Z_BEST_COMPRESSION) != Z_OK) {
        std::cerr << "Failed to initialize zlib stream\n";
        return "";
    }

    // Buffer for compressed data
    std::vector<char> buffer(4096);
    std::string compressed_data;

    // Perform the compression
    int ret;
    do {
        strm.avail_out = buffer.size(); // Set the number of bytes available in the output buffer.
        strm.next_out = reinterpret_cast<Bytef*>(buffer.data()); // Pointer to the start of the output buffer.

        ret = deflate(&strm, Z_FINISH);  // Z_FINISH ensures we complete the compression
        if (ret == Z_STREAM_ERROR) { // Check for errors during compression.
            std::cerr << "Stream error during compression\n";
            deflateEnd(&strm); // Clean up the zlib stream in case of an error.
            return "";
        }

        // Append the compressed data to the result string.
        compressed_data.append(buffer.data(), buffer.size() - strm.avail_out);

    } while (strm.avail_out == 0); // Continue until all data is processed.

    // Clean up
    deflateEnd(&strm);

    return compressed_data; // Return the compressed data.
}

/**
 * Compresses a tree format string using zlib with the best compression settings.
 * 
 * This function performs the following steps:
 *
 * 1. **Initialize zlib Stream**:
 *    - Sets up the zlib stream (`z_stream`) with default values for compression.
 *    - Specifies the input data's size and starting point.
 *    - Initializes the compression process using the best compression level.
 *    - If initialization fails, an error is reported, and the function returns an empty string.
 *
 * 2. **Set Up Compression Buffer**:
 *    - Prepares a buffer to store chunks of compressed data.
 *    - Uses a loop to perform compression in steps:
 *      - **Configure Output Buffer**: Specifies the size and location for storing compressed data.
 *      - **Call `deflate`**: Compresses the input data in chunks.
 *      - **Check for Errors**: Handles any compression errors (e.g., stream error).
 *      - **Append Compressed Data**: Adds the compressed data from the buffer to the final string.
 *      - Continues compression until all input data is processed.
 *    - Ends the compression process and cleans up resources.
 *
 * 3. **Return Compressed Data**:
 *    - Returns the final compressed string containing the tree format.
 * 
 * The function is designed to compress a tree format string in preparation for storage or transmission.
 */
std::string compress_tree_format(const std::string& input_data) {
    // Step 1: Initialize the zlib stream structure.
    z_stream strm{};  // Create a zlib stream structure initialized with zeros.

    strm.avail_in = input_data.size(); // Set the size of the input data to be compressed.
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input_data.data())); // Pointer to the input data.

    // Initialize the zlib stream for compression using the best compression level.
    if (deflateInit(&strm, Z_BEST_COMPRESSION) != Z_OK) {
        // If initialization fails, print an error message and return an empty string.
        std::cerr << "Failed to initialize zlib stream\n";
        return "";
    }

    // Step 2: Prepare a buffer to hold compressed data.
    std::vector<char> buffer(4096); // Buffer of 4096 bytes for storing compressed output.

    std::string compressed_data; // String to accumulate all compressed data.

    // Compress the data in chunks until all input data is processed.
    int ret; // Variable to hold the return status of the compression function.
    do {
        strm.avail_out = buffer.size(); // Set the size of the output buffer.
        strm.next_out = reinterpret_cast<Bytef*>(buffer.data()); // Pointer to the output buffer.

        ret = deflate(&strm, Z_FINISH); // Perform compression; Z_FINISH indicates this is the final block.

        if (ret == Z_STREAM_ERROR) { // Check for stream errors during compression.
            std::cerr << "Stream error during compression\n";
            deflateEnd(&strm); // Clean up resources if an error occurs.
            return "";
        }

        // Append the compressed data from the buffer to the final output string.
        compressed_data.append(buffer.data(), buffer.size() - strm.avail_out);

    } while (strm.avail_out == 0); // Repeat until the entire input data is compressed.

    // Clean up the zlib stream to release resources.
    deflateEnd(&strm);

    // Step 3: Return the fully compressed string.
    return compressed_data;
}

/**
 * Creates a SHA-1 hash of the uncompressed contents of a file, using a Git-style header.
 * The header format is "blob <size>\0" where <size> is the size of the file in bytes.
 *
 * This function performs the following steps:
 *
 * 1. **Open the File**:
 *    - Opens the file specified by the `file_name` parameter in binary mode for reading.
 *    - If the file cannot be opened, an error message is printed, and an empty string is returned.
 *
 * 2. **Read File Contents**:
 *    - Reads the entire content of the file into a `std::vector<char>` named `file_data`.
 *    - The file is read using an input stream iterator that iterates through each character in binary mode.
 *
 * 3. **Create Git-Style Header**:
 *    - Constructs a header string that includes the word "blob", the size of the file in bytes, and a null terminator (`'\0'`).
 *    - This header, combined with the actual file contents, forms the data to be hashed.
 *
 * 4. **Concatenate Header and File Contents**:
 *    - Concatenates the header and the file data into a single string named `store_data`.
 *
 * 5. **Calculate SHA-1 Hash**:
 *    - Uses the `SHA1` function from a cryptographic library to calculate the SHA-1 hash of the `store_data` string.
 *    - The `hash` array, of size `SHA_DIGEST_LENGTH`, holds the resulting 20-byte (160-bit) hash.
 *
 * 6. **Convert Hash to Hexadecimal String**:
 *    - **Initialize Output Stream**:
 *      - An `std::ostringstream` object named `oss` is created to format the hash as a hexadecimal string.
 *    - **Iterate Over Each Byte**:
 *      - The `hash` array contains 20 bytes of data. Each byte is 8 bits, so the total hash size is 160 bits (20 bytes * 8 bits/byte).
 *      - Each element in the `hash` array is an `unsigned char`, which holds one byte of data.
 *    - **Hexadecimal Conversion**:
 *      - **`std::hex`**: This manipulator sets the output stream to hexadecimal mode. It tells the `ostringstream` to interpret and format the integers in hexadecimal (base 16).
 *      - **`std::setw(2)`**: Ensures that each hexadecimal value is two characters wide. This is crucial because each byte (8 bits) is represented by two hexadecimal digits. For instance, a byte with a value of 15 would be displayed as "0f" in hexadecimal.
 *      - **`std::setfill('0')`**: Pads the output with leading zeros if the hexadecimal value is less than two characters. For example, a value of 5 would be padded to "05".
 *      - **`static_cast<int>(hash[i])`**: Converts the `unsigned char` to an `int` for formatting purposes. This conversion is necessary because the stream manipulators (`std::hex`, `std::setw`, and `std::setfill`) work with `int` values. It ensures that the byte is properly interpreted and displayed as a hexadecimal number.
 *      - Each byte in the `hash` array is processed individually, converted to its hexadecimal representation, and appended to the `oss` stream.
 *    - **Resulting Hexadecimal String**:
 *      - After the loop completes, the `oss` object contains the complete hexadecimal string representation of the SHA-1 hash.
 *      - The `oss.str()` method is used to retrieve this string, which represents the entire hash in a human-readable hexadecimal format.
 *
 * The function assumes that the SHA-1 implementation and relevant libraries are properly included and linked in your project.
 * Notes: the SHA hash needs to be computed over the "uncompressed" contents of the file, not the compressed version.
 * The input for the SHA hash is the header (blob <size>\0) + the actual contents of the file, not just the contents of the file.
 */
std::string create_sha_hash(const std::string &file_name, bool return_in_binary, bool is_symlink = false) {
    std::string store_data;
    if (is_symlink) {
        // Handle symlink by reading its target
        std::string target_path = std::filesystem::read_symlink(file_name).string();
        // Create the Git-style header for symlinks: "blob <size>\0".
        std::string header = "blob " + std::to_string(target_path.size()) + '\0';
        store_data = header + target_path;
    } else {
        // Open the file in binary mode for reading.
        std::ifstream file(file_name, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: File '" << file_name << "' not found." << std::endl;
            return ""; // Return an empty string if the file cannot be opened.
        }

        // Read file contents into a vector of characters.
        std::vector<char> file_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close(); // Close the file as it is no longer needed.

        // Create the Git-style header: "blob <size>\0".
        std::string header = "blob " + std::to_string(file_data.size()) + '\0';
        store_data = header + std::string(file_data.begin(), file_data.end());
    }

    // Array to hold the SHA-1 hash (20 bytes, 160 bits).
    unsigned char hash[SHA_DIGEST_LENGTH];
    // Calculate the SHA-1 hash of the concatenated header and file contents.
    SHA1(reinterpret_cast<const unsigned char *>(store_data.c_str()), store_data.size(), hash);

    if (return_in_binary) {
        // Return the binary representation of the SHA-1 hash.
        return std::string(reinterpret_cast<char*>(hash), SHA_DIGEST_LENGTH);
    } 
    else {
        // Create a string stream to convert the hash to a hexadecimal string.
        std::ostringstream oss;
        for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        // Return the final hexadecimal string representation of the SHA-1 hash.
        return oss.str();
    }

}

/**
 * Creates a SHA-1 hash of a given tree format string and optionally returns it in hexadecimal format.
 *
 * This function performs the following steps:
 *
 * 1. **Compute SHA-1 Hash**:
 *    - Computes the SHA-1 hash of the input `tree_format` string.
 *    - The hash is stored in a 20-byte array (`hash`).
 *
 * 2. **Return in Desired Format**:
 *    - If `return_in_hex` is `true`, converts the hash to a hexadecimal string.
 *    - If `return_in_hex` is `false`, returns the raw binary hash as a string.
 * 
 * The function is designed to create a hash for a tree object in Git and return it in the desired format.
 */
std::string create_tree_hash(const std::string& tree_format, bool return_in_hex) {
    // Step 1: Compute the SHA-1 hash of the tree format string.
    unsigned char hash[SHA_DIGEST_LENGTH]; // Array to hold the 20-byte SHA-1 hash.
    SHA1(reinterpret_cast<const unsigned char*>(tree_format.c_str()), tree_format.size(), hash); // Compute the SHA-1 hash.

    // Step 2: Return the hash in the desired format.
    if (return_in_hex) {
        // If the hexadecimal format is requested, convert the hash to a hex string.
        std::ostringstream hex_stream; // Stream to hold the hex string.
        for (unsigned char c : hash) {
            // Convert each byte of the hash to a 2-digit hex value and append to the stream.
            hex_stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
        return hex_stream.str(); // Return the hexadecimal string.
    } else {
        // Return the raw binary hash as a string.
        return std::string(reinterpret_cast<char*>(hash), SHA_DIGEST_LENGTH);
    }
}

/**
 * Creates a tree format string representing the contents of a directory for use in a Git tree object.
 *
 * This function performs the following steps:
 *
 * 1. **Initialize Structures**:
 *    - Creates necessary variables and data structures to hold file modes, names, and hashes.
 *
 * 2. **Iterate Through Directory Entries**:
 *    - Loops through all files and subdirectories in the specified `directory_path`.
 *    - For each file, directory, or symlink, calculates the SHA-1 hash, determines the mode, and stores the information.
 *    - Skips the `.git` directory.
 *
 * 3. **Sort Entries**:
 *    - Sorts the collected directory entries alphabetically by their filenames.
 *
 * 4. **Build Tree Format String**:
 *    - Constructs the tree format string by concatenating the mode, name, and hash of each entry.
 *    - Prepends the string with the tree header indicating the type and size of the content.
 *
 * 5. **Return Tree Format String**:
 *    - Returns the final tree format string that can be used to create a Git tree object.
 */
std::string create_tree_format(const std::string& directory_path) {
    // Step 1: Initialize structures.
    std::filesystem::path path(directory_path); // Filesystem path object representing the directory.
    std::string entries_string; // String to accumulate the entries in the tree format.
    std::string mode; // String to hold the file mode (permissions).
    std::string file_hash; // String to hold the SHA-1 hash of the file's content or subtree.
    std::unordered_map<std::string, std::pair<std::string, std::string>> entries; // Map to store filename, mode, and hash.

    // Step 2: Iterate through directory entries.
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().filename() == ".git") {
            continue; // Skip the .git directory.
        }

        // Check if the entry is a regular file.
        if (entry.is_regular_file()) {
            std::string full_file_path = entry.path().string(); // Get the full file path.
            file_hash = create_sha_hash(full_file_path, true); // Compute the SHA-1 hash of the file.

            // Determine the file mode based on its permissions.
            std::filesystem::perms permissions = entry.status().permissions();
            if ((permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                mode = "100755"; // Executable file mode.
            } else {
                mode = "100644"; // Regular file mode.
            }
            entries[entry.path().filename().string()] = {mode, file_hash}; // Store the mode and hash.
        } 
        // Check if the entry is a symbolic link.
        else if (entry.is_symlink()) {
            std::string full_file_path = entry.path().string(); // Get the full path of the symlink.
            file_hash = create_sha_hash(full_file_path, true, true); // Compute the SHA-1 hash of the symlink.
            mode = "120000"; // Symlink mode.
            entries[entry.path().filename().string()] = {mode, file_hash}; // Store the mode and hash.
        } 
        // Check if the entry is a directory.
        else if (entry.is_directory()) {
            mode = "40000"; // Directory mode.
            std::string full_directory_path = entry.path().string(); // Get the full path of the directory.
            std::string tree_format = create_tree_format(full_directory_path); // Recursively create the tree format for the subdirectory.
            file_hash = create_tree_hash(tree_format, false); // Compute the SHA-1 hash of the subdirectory tree format.
            entries[entry.path().filename().string()] = {mode, file_hash}; // Store the mode and hash.
        }
    }

    // Step 3: Sort the entries by filename.
    std::vector<std::string> sorted_keys; // Vector to hold sorted filenames.
    for (const auto& [key, value] : entries) {
        sorted_keys.push_back(key); // Add the filename to the vector.
    }
    std::sort(sorted_keys.begin(), sorted_keys.end()); // Sort the filenames alphabetically.

    // Step 4: Build the tree format string.
    for (int i = 0; i < sorted_keys.size(); i++) {
        // Concatenate the mode, filename, and hash for each entry in the sorted order.
        entries_string += entries[sorted_keys[i]].first + " " + sorted_keys[i] + '\0' + entries[sorted_keys[i]].second;
    }

    // Prepend the tree header to the accumulated entries string.
    std::string tree_format_string = "tree " + std::to_string(entries_string.size()) + '\0' + entries_string;

    // Step 5: Return the final tree format string.
    return tree_format_string;
}

/*
 * The function `get_current_timestamp()` generates and returns the current timestamp 
 * as a formatted string. This timestamp includes the date, time, year, and timezone 
 * in a format that is used in Git commit objects. The timestamp will be in 
 * the format: "Day Month Date HH:MM:SS Year Timezone", e.g., "Mon Aug 26 15:45:30 2024 +0000".
 */
std::string get_current_timestamp() {
    // Get the current time as a time_t object, representing the number of seconds since the Unix epoch (January 1, 1970).
    std::time_t now = std::time(nullptr);

    // Convert the time_t object to a local time structure (std::tm), which breaks 
    // down the time into components like year, month, day, hour, etc.
    std::tm* local_time = std::localtime(&now);

    // Create a buffer to hold the formatted timestamp string.
    char buffer[64];

    // Format the local time into a human-readable string using std::strftime, 
    // and store it in the buffer. The format used is:
    // "%a %b %d %H:%M:%S %Y %z" which corresponds to:
    // - %a: Abbreviated weekday name (e.g., Mon)
    // - %b: Abbreviated month name (e.g., Aug)
    // - %d: Day of the month (01-31)
    // - %H:%M:%S: Hour:Minute:Second in 24-hour format
    // - %Y: Full year (e.g., 2024)
    // - %z: Timezone offset from UTC (e.g., +0000)
    std::strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y %z", local_time);

    // Return the formatted timestamp as a std::string.
    return std::string(buffer);
}

/*
 * The function `create_commit_info()` generates and returns a formatted string 
 * containing the author and committer information for a Git commit object. 
 * The information includes the name, email, and timestamp for both the author 
 * and committer, following Git's commit object format. The result is a string 
 * with two lines, one for the author and one for the committer, each followed 
 * by a newline character.
 */
std::string create_commit_info() {
    // Hardcoded values for the author and committer information.
    // In a real-world scenario, these values would typically be 
    // dynamically generated or retrieved from configuration files.
    std::string author_name = "Author Name";
    std::string author_email = "authorname@example.com";
    std::string committer_name = "Author Name";
    std::string committer_email = "authorname@example.com";

    // Retrieve the current timestamp using the get_current_timestamp() function.
    // This timestamp will be used for both the author and committer fields.
    std::string timestamp = get_current_timestamp();

    // Initialize a string to hold the complete commit information.
    std::string commit_info;

    // Format the author information by combining the name, email, and timestamp.
    // The format used is "author {author_name} <{author_email}> {timestamp}".
    commit_info += "author " + author_name + " <" + author_email + "> " + timestamp + '\n';

    // Format the committer information similarly, following the pattern:
    // "committer {committer_name} <{committer_email}> {timestamp}".
    commit_info += "committer " + committer_name + " <" + committer_email + "> " + timestamp + '\n';

    // Add an extra newline at the end to separate the commit information from the commit message.
    commit_info += '\n';

    // Return the fully formatted commit information string.
    return commit_info;
}

int initialize_repository() {
    try {
        std::filesystem::create_directory(".git");
        std::filesystem::create_directory(".git/objects");
        std::filesystem::create_directory(".git/refs");

        std::ofstream headFile(".git/HEAD");
        if (headFile.is_open()) {
            headFile << "ref: refs/heads/main\n";
            headFile.close();
        } else {
            std::cerr << "Failed to create .git/HEAD file.\n";
            return EXIT_FAILURE;
        }

        std::cout << "Initialized git directory\n";
        return EXIT_SUCCESS; 
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}

int main(int argc, char *argv[])
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
    if (command == "init") { 
        return initialize_repository();
    }
    else if (command == "cat-file") {

        if (argc <= 3) {
            std::cerr << "Invalid arguments, required `-p <blob_sha>`\n";
            return EXIT_FAILURE;
        }

        std::string flag = argv[2];
        if (flag != "-p") {
            std::cerr << "Invalid flag for cat-file, expected `-p`\n";
            return EXIT_FAILURE;
        }

        const std::string value = argv[3];
        const std::string dir_name = value.substr(0, 2);
        const std::string blob_sha = value.substr(2);
        std::string path = ".git/objects/" + dir_name + "/" + blob_sha;

        std::string file_content = decompress_zlib_data(path, true);
        std::cout << file_content;
    } 
    else if(command == "hash-object") {
        if (argc <= 3) {
            std::cerr << "Invalid arguments, required `-w test.txt`\n";
            return EXIT_FAILURE;
        }

        std::string flag = argv[2];
        if (flag != "-w") {
            std::cerr << "Invalid flag for hash-object, expected `-w`\n";
            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];
        std::string sha_hash = create_sha_hash(file_name, false);

        std::cout << sha_hash;

        std::string file_compressed_data = compress_blob_data(file_name);

        //Create directory and blob file, then write to the file
        std::string dir_name = sha_hash.substr(0, 2);
        std::string file_hash_name = sha_hash.substr(2);
        std::filesystem::path full_dir_path = ".git/objects/" + dir_name;
        std::filesystem::create_directories(full_dir_path);
        std::filesystem::path full_file_path = full_dir_path / file_hash_name;


        // Write the compressed data to the file
        std::ofstream outfile(full_file_path, std::ios::binary);
        if (!outfile) {
            std::cerr << "Failed to open file for writing: " << full_file_path << "\n";
            return EXIT_FAILURE;
        }

        outfile.write(file_compressed_data.data(), file_compressed_data.size());
        outfile.close();
    }
    else if (command == "ls-tree") {
        if (argc <= 3) {
            std::cerr << "Invalid arguments, required `--name-only <tree_sha>`\n";
            return EXIT_FAILURE;
        }

        std::string flag = argv[2];
        if (flag != "--name-only") {
            std::cerr << "Invalid flag for ls-tree, expected `--name-only`\n";
            return EXIT_FAILURE;
        }

        std::string tree_hash = argv[3];
        std::string dir = ".git/objects/" + tree_hash.substr(0, 2);
        std::string file = tree_hash.substr(2);
        std::string path = dir + "/" + file;

        //change function name to decompress zlib data
        //after this function, the tree header will be discarded
        std::string decompressed_tree_data = decompress_zlib_data(path, true);

        // Loop until the decompressed_tree_data string is empty
        while (!decompressed_tree_data.empty()) {
            // The decompressed tree_data string is in this format: 
            // "tree <size>\0<mode> <name>\0<20_byte_sha><mode> <name>\0<20_byte_sha>"

            // Initialize variables to hold parts of the current tree object entry
            std::string mode = "";               // Will hold the file mode (e.g., "100644")
            std::string twenty_byte_hash = "";   // Will hold the SHA-1 hash of the object (20 bytes)
            std::string tree_file_name = "";     // Will hold the file name

            // Find the position of the first space in the string, which separates the mode from the file name
            int empty_space_position = decompressed_tree_data.find(' ');
            // Extract the mode from the beginning of the string up to the first space
            mode = decompressed_tree_data.substr(0, empty_space_position);
            // Remove the extracted mode and the space from the original string
            decompressed_tree_data.erase(0, empty_space_position + 1);
            // Find the position of the null terminator ('\0') that separates the file name from the SHA-1 hash
            int null_terminator_position = decompressed_tree_data.find('\0');            
            // Extract the file name from the string up to the null terminator
            tree_file_name = decompressed_tree_data.substr(0, null_terminator_position);           
            // Remove the extracted file name and the null terminator from the original string
            decompressed_tree_data.erase(0, null_terminator_position + 1);
            // Extract the next 20 characters, which represent the SHA-1 hash (20 bytes)
            twenty_byte_hash = decompressed_tree_data.substr(0, 20);         
            // Remove the extracted SHA-1 hash from the original string
            decompressed_tree_data.erase(0, 20);

            // Output the extracted file name to the console
            std::cout << tree_file_name << std::endl;
        }
    }
    else if(command == "write-tree") {
        // Generate the tree format string and hash from the working directory
        std::string directory_path = "."; // Assuming current working directory
        std::string tree_format = create_tree_format(directory_path);
        std::string tree_hash_hex = create_tree_hash(tree_format, true); // Get hex hash

        // Split the hash into directory and file parts
        std::string hash_dir = tree_hash_hex.substr(0, 2); // First 2 characters
        std::string hash_file = tree_hash_hex.substr(2);   // Remaining 38 characters

        // Create the directory in .git/objects
        std::filesystem::path objects_dir = ".git/objects";
        std::filesystem::path hash_dir_path = objects_dir / hash_dir;
        std::filesystem::create_directories(hash_dir_path);

        // Write the tree format into the file
        std::filesystem::path tree_file_path = hash_dir_path / hash_file;
        std::ofstream tree_file(tree_file_path, std::ios::binary);

        // Ensure the file stream is open before writing
        if (!tree_file) {
            std::cerr << "Error: Could not open file for writing: " << tree_file_path << '\n';
            return 1;
        }

        // Compress and write tree format
        std::string compressed_tree_format = compress_tree_format(tree_format);
        tree_file.write(compressed_tree_format.c_str(), compressed_tree_format.size());
        tree_file.close();

        // Output the 40-character hexadecimal hash
        std::cout << tree_hash_hex << '\n';
    }
    else if(command == "commit-tree") {
        /*
        * This code block is responsible for creating a commit object in a Git-like system. 
        * A commit object is a record that includes the current state of the repository, 
        * linking it to previous states (if any), and documenting the changes made with 
        * an associated message. The commit object is more complex than other Git objects 
        * like blobs and trees because it includes references to a parent commit (if any), 
        * author and committer information, and the commit message itself. 
        * Unlike other Git objects which are stored as a single line, the commit format 
        * contains multiple lines, with each line separated by a newline character ('\n').
        * 
        * The commit object format is as follows:
        * 
        * commit {size}\0tree {tree_sha}
        * parent {parent_sha}  // One or more lines, each starting with "parent {parent_sha}"
        * author {author_name} <{author_email}> {author_date_seconds} {author_date_timezone}
        * committer {committer_name} <{committer_email}> {committer_date_seconds} {committer_date_timezone}
        *
        * {commit message}  // The message describing the changes made in this commit
        * 
        * The commit format is not a single line of text; newlines are used to separate the 
        * different parts of the commit. This structure is necessary for Git to properly 
        * interpret and process the commit object.
        * 
        * IMPORTANT TO NOTE: Strings that are concatenated with a null byte ('\0'), then concatenated
        * with more text, does not mean that text is discarded. This is because the function is using
        * std::string rather than c-style strings, which are terminated with the null byte. The ability 
        * to include null bytes within a std::string without terminating the string or losing content is 
        * due to the fact that std::string internally tracks the size of the string independently of the 
        * null byte. The null byte is just another character within the string, with no special 
        * terminating role as it has in C-style strings. 
        */
        // Extract the tree hash, commit message, and parent SHA from the command-line arguments.
        std::string tree_hash = argv[2];
        std::string commit_message = argv[argc - 1];
        std::string parent_sha = argv[argc - 3];

        // Create the commit information, which includes author, committer, and timestamps.
        std::string commit_info = create_commit_info();

        // Format the commit content with the required structure:
        // - The "tree" line followed by the tree hash.
        // - The "parent" line followed by the parent commit SHA (if applicable).
        // - The commit info (author and committer information).
        // - The commit message.
        // Each of these parts is separated by a newline character ('\n').
        std::string commit_content_format = "tree " + tree_hash + '\n' + "parent " + parent_sha + '\n' + commit_info + commit_message + '\n';

        // Create the final commit object format:
        // - The "commit" line followed by the size of the commit content.
        // - A null byte ('\0') to separate the header from the content.
        // - The actual commit content created above.
        std::string commit_format = "commit " + std::to_string(commit_content_format.size()) + '\0' + commit_content_format;

        // Generate the commit hash by creating a SHA-1 hash of the commit object format.
        // The second argument 'true' indicates that this is the final hash (including compression, etc.).
        std::string commit_hash = create_tree_hash(commit_format, true); // Even though the function is called create_tree_hash, functionally the commit hash is made the same way

        // Write the commit hash to the .git/HEAD file, which points to the latest commit.
        std::ofstream main_directory_path(".git/HEAD");
        if (main_directory_path.is_open()) {
            main_directory_path << commit_hash;
            main_directory_path.close();
        }

        // Prepare to store the commit object in the .git/objects directory.
        // The commit hash is split into two parts:
        // - The first two characters determine the directory name.
        // - The remaining characters are used for the file name.
        std::string dir_name = commit_hash.substr(0, 2);
        std::string file_hash_name = commit_hash.substr(2);
        std::filesystem::path full_dir_path = ".git/objects/" + dir_name;
        
        // Create the necessary directories in the .git/objects path if they don't already exist.
        std::filesystem::create_directories(full_dir_path);
        // Define the full path to the commit object file.
        std::filesystem::path full_file_path = full_dir_path / file_hash_name;
        // Compress the commit object format to save space and match Git's internal storage format.
        std::string compressed_commit_format = compress_tree_format(commit_format); // Even though the function is called compress_tree_format, functionally it compresses the same
        // Create and open the file in binary mode to write the compressed commit object.
        std::ofstream file(full_file_path, std::ios::binary);
        // Write the compressed data to the file, ensuring the full content (including null bytes) is preserved.
        file.write(compressed_commit_format.data(), compressed_commit_format.size());
        // Close the file after writing.
        file.close();

        // Output the commit hash to the console, which can be used as a reference to this commit.
        std::cout << commit_hash;
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
