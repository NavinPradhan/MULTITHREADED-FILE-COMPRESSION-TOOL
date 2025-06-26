#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <zlib.h>

const size_t CHUNK_SIZE = 1024 * 1024;

struct Chunk
{
    size_t index;
    std::vector<char> data;
    std::vector<char> compressed;
};

std::mutex mtx;
std::condition_variable cv;
std::vector<Chunk> compressedChunks;

void compress_chunk(Chunk &chunk)
{
    uLongf compSize = compressBound(chunk.data.size());
    chunk.compressed.resize(compSize);
    if (compress((Bytef *)chunk.compressed.data(), &compSize,
                 (Bytef *)chunk.data.data(), chunk.data.size()) != Z_OK)
    {
        std::cerr << "Compression failed for chunk " << chunk.index << "\n";
        return;
    }
    chunk.compressed.resize(compSize);

    {
        std::lock_guard<std::mutex> lock(mtx);
        compressedChunks.push_back(std::move(chunk));
    }
    cv.notify_one();
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file>\n";
        return 1;
    }

    std::ifstream inFile(argv[1], std::ios::binary);
    if (!inFile)
    {
        std::cerr << "Error opening input file.\n";
        return 1;
    }

    std::ofstream outFile(argv[2], std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Error opening output file.\n";
        return 1;
    }

    std::vector<std::thread> workers;
    size_t index = 0;

    while (!inFile.eof())
    {
        Chunk chunk;
        chunk.index = index++;
        chunk.data.resize(CHUNK_SIZE);
        inFile.read(chunk.data.data(), CHUNK_SIZE);
        chunk.data.resize(inFile.gcount());

        if (chunk.data.empty())
            break;

        workers.emplace_back(compress_chunk, std::ref(chunk));
    }

    for (auto &t : workers)
    {
        if (t.joinable())
            t.join();
        std::sort(compressedChunks.begin(), compressedChunks.end(),
                  [](const Chunk &a, const Chunk &b)
                  {
                      return a.index < b.index;
                  });

        for (const auto &chunk : compressedChunks)
        {
            uint32_t size = chunk.compressed.size();
            outFile.write(reinterpret_cast<const char *>(&size), sizeof(size)); // Store size
            outFile.write(chunk.compressed.data(), size);
        }

        std::cout << "Compression complete. " << index << " chunks processed.\n";
        return 0;
    }
}
