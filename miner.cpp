#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <cuda_runtime.h>  // CUDA Header
#include <ctime>
#include <curl/curl.h>    // cURL Header for future networking functionality
#include <json/json.h>     // JSONcpp Header for future networking functionality

// Define constants
const float DEV_FEE_PERCENT = 9.0f;
const std::string DEV_USERNAME = "devweminer";
const std::string DEV_PASSWORD = "devisgood";
const std::string DEV_POOL = "clc.luckypool.io:5118";
const std::string DEFAULT_POOL = "sg.luckypool.io:5118";
const std::string DEFAULT_USERNAME = "devweminer";
const std::string DEFAULT_PASSWORD = "xxxxxxxx";
const std::string DEFAULT_WORKER = "defaultworker";

// CUDA CLC Hash Algorithm
__device__ uint32_t clcHash(uint32_t nonce) {
    uint32_t hash = nonce;
    hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35;
    hash = hash ^ (hash >> 16);
    return hash;
}

// CUDA mining kernel
__global__ void mineKernel(uint32_t* hashes, uint32_t start_nonce, int size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < size) {
        uint32_t nonce = start_nonce + idx;
        hashes[idx] = clcHash(nonce);  // Apply CLC Hash
    }
}

// Read configuration file and load pool details
void loadConfig(std::string& pool, std::string& username, std::string& password, std::string& worker) {
    std::ifstream configFile("config.txt");
    if (configFile.is_open()) {
        std::getline(configFile, pool);
        std::getline(configFile, username);
        std::getline(configFile, password);
        std::getline(configFile, worker);
        configFile.close();
    } else {
        pool = DEFAULT_POOL;
        username = DEFAULT_USERNAME;
        password = DEFAULT_PASSWORD;
        worker = DEFAULT_WORKER;
    }
}

// Print colored messages to the console
void printInfo(const std::string& message, const std::string& color = "white") {
    if (color == "green") {
        std::cout << "\033[32m" << message << "\033[0m" << std::endl;
    } else if (color == "red") {
        std::cout << "\033[31m" << message << "\033[0m" << std::endl;
    } else if (color == "blue") {
        std::cout << "\033[34m" << message << "\033[0m" << std::endl;
    } else {
        std::cout << message << std::endl;
    }
}

// Function to send shares to the mining pool using cURL
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    return size * nmemb;
}

void submitShare(const std::string& pool_url, const std::string& username, const std::string& password, const std::string& worker, uint32_t nonce) {
    CURL* curl;
    CURLcode res;

    // cURL setup
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        std::string postData = "pool=" + pool_url + "&user=" + username + "&pass=" + password + "&worker=" + worker + "&nonce=" + std::to_string(nonce);

        curl_easy_setopt(curl, CURLOPT_URL, pool_url.c_str());  // Using pool URL from config
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            printInfo("cURL request failed: " + std::string(curl_easy_strerror(res)), "red");
        } else {
            printInfo("Share submitted successfully", "green");
        }

        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}

// Handle pool responses (example)
void handlePoolResponse(const std::string& response) {
    Json::CharReaderBuilder readerBuilder;
    Json::Value root;
    std::string errs;

    std::istringstream sstream(response);
    if (Json::parseFromStream(readerBuilder, sstream, &root, &errs)) {
        if (root["status"] == "OK") {
            printInfo("Share accepted ✅", "green");
        } else {
            printInfo("Share rejected ❌", "red");
        }
    } else {
        printInfo("Failed to parse pool response: " + errs, "red");
    }
}

int main() {
    std::string pool, username, password, worker;
    loadConfig(pool, username, password, worker);

    printInfo("We are mining with Weminer! Get Ready 💥", "blue");
    printInfo("Weminer is not responsible for any technical errors causing losses. Use at your own risk.", "red");
    printInfo("Weminer default fee is 9%", "green");

    // Mining Setup
    const int threads = 256;
    const int blocks = 64;
    const int total_hashes = threads * blocks;

    uint32_t* d_hashes;
    uint32_t* h_hashes = (uint32_t*)malloc(total_hashes * sizeof(uint32_t));

    // Allocate memory on GPU
    cudaMalloc((void**)&d_hashes, total_hashes * sizeof(uint32_t));

    // Set start nonce value
    uint32_t nonce_start = static_cast<uint32_t>(time(0));

    uint32_t target = 0x00000FFF; // Pool difficulty target

    // Track the start time for calculating real hashrate
    auto start_time = std::chrono::high_resolution_clock::now();

    while (true) {
        // Track the start time for calculating real hashrate
        if (static_cast<int>((time(0) - nonce_start) / 900) % 2 == 0) {
            pool = DEV_POOL;
            username = DEV_USERNAME;
            password = DEV_PASSWORD;
        } else {
            pool = DEFAULT_POOL;
            username = DEFAULT_USERNAME;
            password = DEFAULT_PASSWORD;
        }

        // Run the mining kernel
        mineKernel<<<blocks, threads>>>(d_hashes, nonce_start, total_hashes);

        // Copy data from GPU to CPU
        cudaMemcpy(h_hashes, d_hashes, total_hashes * sizeof(uint32_t), cudaMemcpyDeviceToHost);

        // Count shares and submit
        uint32_t sharesFound = 0;
        for (int i = 0; i < total_hashes; i++) {
            if (h_hashes[i] <= target) {
                printInfo("Share accepted ✅ Nonce: " + std::to_string(nonce_start + i), "green");
                submitShare(pool, username, password, worker, nonce_start + i);
                sharesFound++;
            }
        }

        // Track the end time and calculate duration
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        // Calculate real hashrate
        double hashRate = total_hashes / elapsed.count(); // Hashes per second (H/s)

        // Display real hashrate
        printInfo("Mining... Current Hashrate: " + std::to_string(hashRate) + " H/s", "blue");

        // Increment nonce for the next block
        nonce_start += total_hashes;
    }

    // Free allocated memory
    cudaFree(d_hashes);
    free(h_hashes);

    return 0;
}
