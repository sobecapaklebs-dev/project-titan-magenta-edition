#ifndef PROJECT_TITAN_UTF8_H
#define PROJECT_TITAN_UTF8_H

#define TITAN_UTF8_REPLACEMENT_CODEPOINT 0xFFFDU

/*
 * Decodes exactly one scalar value. On malformed input it returns U+FFFD and
 * consumes one byte, guaranteeing forward progress without swallowing a later
 * valid character.
 */
unsigned int TitanUtf8_DecodeOne(
    const char *text,
    unsigned int *byte_index
);

/* Encodes a Unicode scalar value, replacing invalid values with U+FFFD. */
unsigned int TitanUtf8_EncodeOne(
    unsigned int codepoint,
    char output[5]
);

int TitanUtf8_IsScalarValue(unsigned int codepoint);

#endif
