#include "micant/km/executive/image_metadata.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>

using namespace micant::ps::detail;

int main() {
    std::array<uint8_t, 32> storage{};
    UserMetadataWriter writer(storage, 0x10000, 4);
    UserUnicodeString value{};
    assert(writer.append(u"A\u03a9", value));
    assert(value.length == 4 && value.maximumLength == 6 && value.buffer == 0x10004);
    const char16_t expected[]{u'A', u'\u03a9', 0};
    assert(std::memcmp(storage.data() + 4, expected, sizeof(expected)) == 0);
    assert(writer.offset() == 10);
    assert(writer.append({}, value));
    assert(value.length == 0 && value.maximumLength == 0 && value.buffer == 0);
    assert(writer.offset() == 10);
    const auto before = storage;
    value = {1, 2, 0, 3};
    assert(!writer.append(u"12345678901234567890", value));
    assert(value.length == 1 && value.maximumLength == 2 && value.buffer == 3);
    assert(storage == before && writer.offset() == 10);
    UserMetadataWriter exact(storage, 0x10000, 26);
    assert(exact.append(u"ab", value) && exact.offset() == storage.size());
    UserMetadataWriter overflow(storage, std::numeric_limits<uintptr_t>::max() - 1, 0);
    assert(!overflow.append(u"a", value));
}
