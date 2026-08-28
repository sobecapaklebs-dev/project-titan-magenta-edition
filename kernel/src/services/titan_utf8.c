#include "titan_utf8.h"

static int TitanUtf8_IsContinuation(unsigned int value)
{
    return (value & 0xC0u) == 0x80u;
}

int TitanUtf8_IsScalarValue(unsigned int codepoint)
{
    return
        codepoint <= 0x10FFFFu &&
        !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

unsigned int TitanUtf8_DecodeOne(
    const char *text,
    unsigned int *byte_index
)
{
    unsigned int index;
    unsigned int first;
    unsigned int second;
    unsigned int third;
    unsigned int fourth;
    unsigned int codepoint;

    if (text == 0 || byte_index == 0)
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    index = *byte_index;
    first = (unsigned char)text[index];

    if (first == 0u)
    {
        return 0u;
    }

    if (first < 0x80u)
    {
        *byte_index = index + 1u;
        return first;
    }

    *byte_index = index + 1u;

    if (first < 0xC2u || first > 0xF4u || text[index + 1u] == '\0')
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    second = (unsigned char)text[index + 1u];
    if (!TitanUtf8_IsContinuation(second))
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    if (first <= 0xDFu)
    {
        *byte_index = index + 2u;
        return ((first & 0x1Fu) << 6u) | (second & 0x3Fu);
    }

    if (text[index + 2u] == '\0')
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    third = (unsigned char)text[index + 2u];
    if (!TitanUtf8_IsContinuation(third))
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    /* E0 80..9F is overlong; ED A0..BF encodes UTF-16 surrogates. */
    if (
        (first == 0xE0u && second < 0xA0u) ||
        (first == 0xEDu && second > 0x9Fu)
    )
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    if (first <= 0xEFu)
    {
        codepoint =
            ((first & 0x0Fu) << 12u) |
            ((second & 0x3Fu) << 6u) |
            (third & 0x3Fu);
        *byte_index = index + 3u;
        return TitanUtf8_IsScalarValue(codepoint)
            ? codepoint
            : TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    if (text[index + 3u] == '\0')
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    fourth = (unsigned char)text[index + 3u];
    if (!TitanUtf8_IsContinuation(fourth))
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    /* F0 80..8F is overlong; F4 90..BF exceeds U+10FFFF. */
    if (
        (first == 0xF0u && second < 0x90u) ||
        (first == 0xF4u && second > 0x8Fu)
    )
    {
        return TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    codepoint =
        ((first & 0x07u) << 18u) |
        ((second & 0x3Fu) << 12u) |
        ((third & 0x3Fu) << 6u) |
        (fourth & 0x3Fu);
    *byte_index = index + 4u;
    return TitanUtf8_IsScalarValue(codepoint)
        ? codepoint
        : TITAN_UTF8_REPLACEMENT_CODEPOINT;
}

unsigned int TitanUtf8_EncodeOne(
    unsigned int codepoint,
    char output[5]
)
{
    if (output == 0)
    {
        return 0u;
    }

    if (!TitanUtf8_IsScalarValue(codepoint))
    {
        codepoint = TITAN_UTF8_REPLACEMENT_CODEPOINT;
    }

    if (codepoint <= 0x7Fu)
    {
        output[0] = (char)codepoint;
        output[1] = '\0';
        return 1u;
    }

    if (codepoint <= 0x7FFu)
    {
        output[0] = (char)(0xC0u | (codepoint >> 6u));
        output[1] = (char)(0x80u | (codepoint & 0x3Fu));
        output[2] = '\0';
        return 2u;
    }

    if (codepoint <= 0xFFFFu)
    {
        output[0] = (char)(0xE0u | (codepoint >> 12u));
        output[1] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
        output[2] = (char)(0x80u | (codepoint & 0x3Fu));
        output[3] = '\0';
        return 3u;
    }

    output[0] = (char)(0xF0u | (codepoint >> 18u));
    output[1] = (char)(0x80u | ((codepoint >> 12u) & 0x3Fu));
    output[2] = (char)(0x80u | ((codepoint >> 6u) & 0x3Fu));
    output[3] = (char)(0x80u | (codepoint & 0x3Fu));
    output[4] = '\0';
    return 4u;
}
