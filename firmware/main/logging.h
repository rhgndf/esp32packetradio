#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <vector>
#include <string>

#include <spiram_allocator.h>

esp_err_t logging_init();
std::vector<std::pair<uint64_t, std::string>, PSRAMAllocator<std::pair<uint64_t, std::string>>> logging_get_all_entries();

#endif