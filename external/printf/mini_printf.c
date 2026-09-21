#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

static unsigned emit_unsigned(char *out, unsigned value, unsigned base,
                              unsigned width, char pad)
{
    char reversed[11];
    unsigned count = 0;
    do {
        const unsigned digit = value % base;
        reversed[count++] = (char)('0' + digit);
        value /= base;
    } while (value != 0u);
    unsigned written = 0;
    while (width > count) {
        out[written++] = pad;
        width--;
    }
    while (count != 0u) out[written++] = reversed[--count];
    return written;
}

int sprintf_(char *out, const char *format, ...)
{
    va_list args;
    char *const start = out;
    va_start(args, format);
    while (*format != 0) {
        if (*format != '%') {
            *out++ = *format++;
            continue;
        }
        format++;
        char pad = ' ';
        bool sign_space = false;
        if (*format == '0') {
            pad = '0';
            format++;
        } else if (*format == ' ') {
            sign_space = true;
            format++;
        }
        unsigned width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10u + (unsigned)(*format++ - '0');
        }
        int precision = -1;
        if (*format == '.') {
            format++;
            precision = 0;
            if (*format == '*') {
                precision = va_arg(args, int);
                format++;
            } else {
                while (*format >= '0' && *format <= '9')
                    precision = precision * 10 + (*format++ - '0');
            }
        }
        switch (*format++) {
        case 'd':
        case 'i': {
            const int value = va_arg(args, int);
            const bool negative = value < 0;
            const unsigned magnitude = negative ? 0u - (unsigned)value : (unsigned)value;
            char digits[11];
            const unsigned count = emit_unsigned(digits, magnitude, 10u, 0u, ' ');
            const unsigned sign = (negative || sign_space) ? 1u : 0u;
            if (pad == ' ') while (width > count + sign) { *out++ = ' '; width--; }
            if (sign != 0u) *out++ = negative ? '-' : ' ';
            if (pad == '0') while (width > count + sign) { *out++ = '0'; width--; }
            for (unsigned i = 0; i < count; i++) *out++ = digits[i];
            break;
        }
        case 'u':
            out += emit_unsigned(out, va_arg(args, unsigned), 10u, width, pad);
            break;
        case 'o':
            out += emit_unsigned(out, va_arg(args, unsigned), 8u, width, pad);
            break;
        case 's': {
            const char *s = va_arg(args, const char *);
            unsigned len = 0;
            while (s[len] != 0 && (precision < 0 || len < (unsigned)precision)) len++;
            while (width > len) {
                *out++ = ' ';
                width--;
            }
            while (len-- != 0u) *out++ = *s++;
            break;
        }
        case '%':
            *out++ = '%';
            break;
        default:
            break;
        }
    }
    *out = 0;
    va_end(args);
    return (int)(out - start);
}
