#include "titan_utf8.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned int decode(const char *text, unsigned int *used)
{
    unsigned int index = 0u;
    unsigned int value = TitanUtf8_DecodeOne(text, &index);
    *used = index;
    return value;
}

int main(void)
{
    unsigned int used;
    char encoded[5];

    assert(decode("A", &used) == 'A' && used == 1u);
    assert(decode("\xC3\xA9", &used) == 0xE9u && used == 2u);
    assert(decode("\xE2\x82\xAC", &used) == 0x20ACu && used == 3u);
    assert(decode("\xF0\x9F\x98\x80", &used) == 0x1F600u && used == 4u);

    assert(decode("\xC0\x80", &used) == TITAN_UTF8_REPLACEMENT_CODEPOINT && used == 1u);
    assert(decode("\xE0\x80\x80", &used) == TITAN_UTF8_REPLACEMENT_CODEPOINT && used == 1u);
    assert(decode("\xED\xA0\x80", &used) == TITAN_UTF8_REPLACEMENT_CODEPOINT && used == 1u);
    assert(decode("\xF4\x90\x80\x80", &used) == TITAN_UTF8_REPLACEMENT_CODEPOINT && used == 1u);
    assert(decode("\x80", &used) == TITAN_UTF8_REPLACEMENT_CODEPOINT && used == 1u);

    assert(TitanUtf8_EncodeOne(0xD800u, encoded) == 3u);
    assert(memcmp(encoded, "\xEF\xBF\xBD", 3u) == 0);
    assert(TitanUtf8_EncodeOne(0x110000u, encoded) == 3u);
    assert(memcmp(encoded, "\xEF\xBF\xBD", 3u) == 0);

    puts("utf8: OK");
    return 0;
}
