/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  | Unless required by applicable law or agreed to in writing, software      |
  | distributed under the License is distributed on an "AS IS" BASIS,        |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. |
  | See the License for the specific language governing permissions and      |
  | limitations under the License. See accompanying LICENSE file.            |
  +--------------------------------------------------------------------------+
  | Author: codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#include <clocale>
#include <cwchar>

TEST(cat_string, formatting_encoding_failure_and_recovery)
{
    const char *locale = std::setlocale(LC_CTYPE, nullptr);
    ASSERT_NE(locale, nullptr);
    const std::string original_locale = locale;
    DEFER(std::setlocale(LC_CTYPE, original_locale.c_str()));
    ASSERT_NE(std::setlocale(LC_CTYPE, "C"), nullptr);
    size_t length = 0;
    char *valid = cat_slprintf("%s", &length, "valid");
    ASSERT_NE(valid, nullptr);
    DEFER(cat_free(valid));
    ASSERT_EQ(length, CAT_STRLEN("valid"));
    const wint_t character = static_cast<wint_t>(L'\u0100');
    errno = 0;
    ASSERT_EQ(cat_slprintf("%lc", &length, character), nullptr);
    ASSERT_EQ(errno, EILSEQ);
    ASSERT_EQ(length, 0);
    ASSERT_EQ(cat_sprintf("%lc", character), nullptr);
    cat_buffer_t buffer;
    ASSERT_TRUE(cat_buffer_create(&buffer, 16));
    DEFER(cat_buffer_close(&buffer));
    ASSERT_TRUE(cat_buffer_append(&buffer, CAT_STRL("prefix")));
    errno = 0;
    ASSERT_FALSE(cat_buffer_append_printf(&buffer, "%lc", character));
    ASSERT_EQ(errno, EILSEQ);
    ASSERT_EQ(std::string(buffer.value, buffer.length), "prefix");
    ASSERT_TRUE(cat_buffer_append_printf(&buffer, "-%d", 7));
    ASSERT_EQ(std::string(buffer.value, buffer.length), "prefix-7");
}

#define CAT_TEST_DEFAULT_STRING_LENGTH    16

TEST(cat_string, nullable_empty_strings)
{
    ASSERT_TRUE(cat_str_is_empty(nullptr));
    ASSERT_TRUE(cat_str_is_empty(""));
    ASSERT_EQ(cat_str_dempty(nullptr), nullptr);
    ASSERT_EQ(cat_str_dempty(""), nullptr);
    for (const char *value : {"text", " "}) {
        ASSERT_FALSE(cat_str_is_empty(value));
        ASSERT_EQ(cat_str_dempty(value), value);
    }
}

TEST(cat_string, append_binary_data_preserves_boundaries)
{
    char output[8];
    memset(output, '#', sizeof(output));
    const char data[] = {'a', '\0', 'b'};
    char *end = cat_strnappend(output + 1, data, sizeof(data));
    ASSERT_EQ(end, output + 1 + sizeof(data));
    ASSERT_EQ(memcmp(output + 1, data, sizeof(data)), 0);
    ASSERT_EQ(output[0], '#');
    ASSERT_EQ(*end, '#');
    ASSERT_EQ(cat_strnappend(end, data, 0), end);
    end = cat_strnappend(end, "c", 1);
    ASSERT_EQ(end, output + 5);
    ASSERT_EQ(end[-1], 'c');
    ASSERT_EQ(*end, '#');
}

TEST(cat_string, const_string_borrows_exact_range)
{
    cat_const_string_t view;
    cat_const_string_init(&view);
    ASSERT_EQ(view.data, nullptr);
    ASSERT_EQ(view.length, 0);
    char data[] = {'a', '\0', 'b'};
    cat_const_string_create(&view, data, sizeof(data));
    ASSERT_EQ(view.data, data);
    ASSERT_EQ(view.length, sizeof(data));
    ASSERT_EQ(memcmp(view.data, data, view.length), 0);
    data[2] = 'c';
    ASSERT_EQ(view.data[2], 'c');
    cat_const_string_create(&view, nullptr, 0);
    ASSERT_EQ(view.data, nullptr);
    ASSERT_EQ(view.length, 0);
}

TEST(cat_string, hexprint)
{
    char in[] = "hello";
    char *out;

    out = cat_hex_dump(in, CAT_STRLEN(in));
    ASSERT_NE(nullptr, out);
    ASSERT_STREQ("0x68 0x65 0x6C 0x6C 0x6F", out);
    cat_free(out);
}

TEST(cat_string, srand_zero_termination)
{
    char buffer[8];

    ASSERT_EQ(buffer, cat_srand(CAT_STRS(buffer)));
    ASSERT_EQ(CAT_STRLEN(buffer), strlen(buffer));
}

TEST(cat_string, srand_not_null_buffer)
{
    char buffer[8];

    ASSERT_EQ(buffer, cat_srand(CAT_STRS(buffer)));
}

TEST(cat_string, srand_null_buffer)
{
    char *out;

    out = cat_srand(nullptr, 8);
    ASSERT_NE(nullptr, out);
    cat_free(out);
}

TEST(cat_string, snrand_not_null_buffer)
{
    char buffer[8];

    ASSERT_EQ(buffer, cat_snrand(CAT_STRS(buffer)));
}

TEST(cat_string, snrand_null_buffer)
{
    char *out;

    out = cat_snrand(nullptr, 8);
    ASSERT_NE(nullptr, out);
    cat_free(out);
}

TEST(cat_string, strlchr)
{
    const char p[] = "foobarchar";
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'f'), p);
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'o'), p + 1);
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'b'), p + 3);
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'a'), p + 4);
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'r'), p + 5);
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'c'), p + 6);
    ASSERT_EQ(cat_strlchr(p, p + CAT_STRLEN(p), 'h'), p + 7);
}

TEST(cat_string, create)
{
    cat_string_t string;
    ASSERT_TRUE(cat_string_create(&string, CAT_STRL("Hello libcat")));
    ASSERT_NE(string.value, nullptr);
    ASSERT_GT(string.length, 0);
    cat_string_close(&string);
}

TEST(cat_string, bounded_search_and_copy)
{
    const char input[] = "abc\0def";
    ASSERT_EQ(cat_strnlen(input, sizeof(input)), 3);
    ASSERT_EQ(cat_strnlen(input, 2), 2);
    ASSERT_EQ(cat_strnlen(input, 0), 0);
    ASSERT_EQ(cat_strlchr(input, input + 2, 'c'), nullptr);
    ASSERT_EQ(cat_strlchr(input, input, 'a'), nullptr);
    char output[sizeof(input)] = {};
    ASSERT_EQ(cat_stpcpy(output, input), output + 3);
    ASSERT_STREQ(output, "abc");
    ASSERT_EQ(cat_stpcpy(output, ""), output);
    ASSERT_STREQ(output, "");
    ASSERT_TRUE(cat_str_is_print("", 0));
    ASSERT_TRUE(cat_str_is_print(CAT_STRL("hello 123")));
    ASSERT_FALSE(cat_str_is_print(input, sizeof(input)));
}

TEST(cat_string, formatted_string)
{
    size_t length = 0;
    char *output = cat_slprintf("%s:%d", &length, "item", 42);
    ASSERT_NE(output, nullptr);
    DEFER(cat_free(output));
    ASSERT_STREQ(output, "item:42");
    ASSERT_EQ(length, strlen(output));
    char *empty = cat_sprintf("%s", "");
    ASSERT_NE(empty, nullptr);
    DEFER(cat_free(empty));
    ASSERT_STREQ(empty, "");
}

TEST(cat_string, list_contains_ci)
{
    const struct {
        const char *list;
        const char *value;
        cat_bool_t contains;
    } cases[] = {
        {"one,two three", "TWO", cat_true},
        {"one,two three", "three", cat_true},
        {" , one,, two ,", "one", cat_true},
        {"one,two", "on", cat_false},
        {"one,two", "two more", cat_false},
        {"", "one", cat_false},
        {"one", "", cat_false},
    };
    for (const auto &c : cases) {
        SCOPED_TRACE(c.list);
        ASSERT_EQ(cat_str_list_contains_ci(c.list, c.value, strlen(c.value)), c.contains);
    }
}

TEST(cat_string, quote_completion)
{
    char output[64];
    size_t length = 0;
    ASSERT_TRUE(cat_str_quote_ex2("abc", 3, output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED, nullptr));
    ASSERT_STREQ(output, "\"abc\"");
    ASSERT_EQ(length, strlen(output));
    ASSERT_FALSE(cat_str_quote_ex2("abcd", 3, output, nullptr,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED | CAT_STR_QUOTE_STYLE_FLAG_ELLIPSIS, nullptr));
    ASSERT_STREQ(output, "\"abc...\"");
    ASSERT_TRUE(cat_str_quote_ex2("a\0b", 3, output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED | CAT_STR_QUOTE_STYLE_FLAG_EMIT_COMMENT, nullptr));
    ASSERT_STREQ(output, " /* \"a\" */");
    ASSERT_TRUE(cat_str_quote_ex2("a", 2, output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_OMIT_TRAILING_0, nullptr));
    ASSERT_STREQ(output, "\"a\"");
    ASSERT_TRUE(cat_str_quote_ex2("a", 2, output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED | CAT_STR_QUOTE_STYLE_FLAG_PRINT_ALL_STRINGS_IN_HEX, nullptr));
    ASSERT_STREQ(output, "\"\\x61\"");
    ASSERT_TRUE(cat_str_quote_ex2("a", 2, output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED | CAT_STR_QUOTE_STYLE_FLAG_PRINT_NON_ASCII_STRINGS_IN_HEX, nullptr));
    ASSERT_STREQ(output, "\"a\"");

    const char control[] = {'\x01', '\0'};
    ASSERT_TRUE(cat_str_quote_ex2(control, sizeof(control), output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED | CAT_STR_QUOTE_STYLE_FLAG_PRINT_NON_ASCII_STRINGS_IN_HEX, nullptr));
    ASSERT_STREQ(output, "\"\\x01\"");
    ASSERT_TRUE(cat_str_quote_ex2("a\0b", 3, output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_ZERO_TERMINATED | CAT_STR_QUOTE_STYLE_FLAG_ELLIPSIS |
        CAT_STR_QUOTE_STYLE_FLAG_EMIT_COMMENT, nullptr));
    ASSERT_STREQ(output, " /* \"a...\" */");
    const char octal[] = {'\x01', '2', '\x1f', '\x7f'};
    ASSERT_FALSE(cat_str_quote_ex2(octal, sizeof(octal), output, &length,
        CAT_STR_QUOTE_STYLE_FLAG_NONE, nullptr));
    ASSERT_STREQ(output, "\"\\0012\\37\\177\"");
}

TEST(cat_string, byte_to_hexstr)
{
    char out[3];
    cat_byte_to_hexstr('x', out);
    out[2] = '\0';
    ASSERT_STREQ(out, "78");
}

TEST(cat_string, byte_is_printable)
{
    ASSERT_TRUE(cat_byte_is_printable('x'));
    ASSERT_FALSE(cat_byte_is_printable('\0'));
}

TEST(cat_string, str_quote)
{
    char *new_str;
    size_t new_length;
    {
        ASSERT_TRUE(cat_str_quote(CAT_STRL("foo"), &new_str, &new_length));
        DEFER(cat_free(new_str));
        ASSERT_STREQ(new_str, "\"foo\"");
    }
    {
        ASSERT_TRUE(cat_str_quote(CAT_STRL("bar\0"), &new_str, &new_length));
        DEFER(cat_free(new_str));
        ASSERT_STREQ(new_str, "\"bar\\0\"");
    }
    {
        cat_bool_t is_complete;
        ASSERT_TRUE(cat_str_quote_ex(
            CAT_STRL("baz\"\\\f\n\r\t\v"),
            &new_str, &new_length,
            CAT_STR_QUOTE_STYLE_FLAG_OMIT_LEADING_TRAILING_QUOTES | CAT_STR_QUOTE_STYLE_FLAG_EMIT_COMMENT,
            "z", &is_complete
        ));
        DEFER(cat_free(new_str));
        ASSERT_STREQ(new_str, " /* ba\\172\\\"\\\\\\f\\n\\r\\t\\v */");
    }
    {
        cat_bool_t is_complete;
        ASSERT_TRUE(cat_str_quote_ex(
            CAT_STRL("char"),
            &new_str, &new_length,
            CAT_STR_QUOTE_STYLE_FLAG_OMIT_LEADING_TRAILING_QUOTES | CAT_STR_QUOTE_STYLE_FLAG_PRINT_ALL_STRINGS_IN_HEX,
            NULL, &is_complete
        ));
        DEFER(cat_free(new_str));
        ASSERT_STREQ(new_str, "\\x63\\x68\\x61\\x72");
    }
    {
        char buffer[4] = "dua";
        cat_bool_t is_complete;
        buffer[3] = 0x7f;
        ASSERT_TRUE(cat_str_quote_ex(
            CAT_STRS(buffer),
            &new_str, &new_length,
            CAT_STR_QUOTE_STYLE_FLAG_OMIT_LEADING_TRAILING_QUOTES | CAT_STR_QUOTE_STYLE_FLAG_PRINT_NON_ASCII_STRINGS_IN_HEX,
            NULL, &is_complete
        ));
        DEFER(cat_free(new_str));
        ASSERT_STREQ(new_str, "\\x64\\x75\\x61\\x7f");
    }
}
