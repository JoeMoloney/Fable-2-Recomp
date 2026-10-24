// Linux diagnostic reads must fail safely on inaccessible guest pages.
#include "fable2_ui_input_probe.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

int main() {
  const size_t page_size = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  constexpr uint32_t guest_address = 0x00100000;
  void* address = const_cast<uint8_t*>(fable2::uip::host_of(guest_address));
  void* mapping = ::mmap(address, page_size * 2, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (mapping == MAP_FAILED) {
    std::perror("mmap");
    return 1;
  }
  const uint32_t expected = 0x12345678;
  std::memcpy(mapping, &expected, sizeof(expected));
  uint32_t actual = 0;
  bool ok = fable2::uip::gread(nullptr, guest_address, &actual, sizeof(actual)) &&
            actual == expected;
  if (::mprotect(static_cast<uint8_t*>(mapping) + page_size, page_size,
                 PROT_NONE) != 0) {
    std::perror("mprotect");
    ::munmap(mapping, page_size * 2);
    return 1;
  }
  ok = ok && !fable2::uip::gread(nullptr, guest_address + page_size,
                                &actual, sizeof(actual));
  ok = ok && !fable2::uip::gread(nullptr, guest_address + page_size - 2,
                                &actual, sizeof(actual));
  ::munmap(mapping, page_size * 2);
  ok = ok && !fable2::uip::gread(nullptr, guest_address, &actual, sizeof(actual));
  std::puts(ok ? "Linux diagnostic read checks passed" : "Diagnostic read FAILED");
  return ok ? 0 : 1;
}
