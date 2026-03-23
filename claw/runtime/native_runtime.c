#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct claw_slice {
    const unsigned char* ptr;
    int64_t len;
} claw_slice;

typedef struct claw_buffer {
    unsigned char* ptr;
    int64_t len;
    int64_t cap;
} claw_buffer;

static void claw_write_bytes(const unsigned char* ptr, int64_t len) {
    if (ptr == NULL || len <= 0) {
        return;
    }
    fwrite(ptr, 1, (size_t)len, stdout);
}

static void claw_write_i128(__int128 value) {
    if (value == 0) {
        fputc('0', stdout);
        return;
    }

    unsigned __int128 magnitude = 0;
    if (value < 0) {
        fputc('-', stdout);
        magnitude = (unsigned __int128)(-(value + 1)) + 1;
    } else {
        magnitude = (unsigned __int128)value;
    }

    char buffer[48];
    size_t count = 0;
    while (magnitude > 0) {
        buffer[count++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    }
    while (count > 0) {
        fputc(buffer[--count], stdout);
    }
}

static void claw_finish_print(bool newline) {
    if (newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

void claw_runtime_print_i1(_Bool value) __asm__("claw.runtime.print.i1");
void claw_runtime_print_i8(int8_t value) __asm__("claw.runtime.print.i8");
void claw_runtime_print_i16(int16_t value) __asm__("claw.runtime.print.i16");
void claw_runtime_print_i32(int32_t value) __asm__("claw.runtime.print.i32");
void claw_runtime_print_i64(int64_t value) __asm__("claw.runtime.print.i64");
void claw_runtime_print_i128(__int128 value) __asm__("claw.runtime.print.i128");
void claw_runtime_print_float(float value) __asm__("claw.runtime.print.float");
void claw_runtime_print_double(double value) __asm__("claw.runtime.print.double");
void claw_runtime_print_ptr(void* value) __asm__("claw.runtime.print.ptr");
void claw_runtime_print_slice(claw_slice value) __asm__("claw.runtime.print.slice");
void claw_runtime_print_buffer(claw_buffer value) __asm__("claw.runtime.print.buffer");

void claw_runtime_println_i1(_Bool value) __asm__("claw.runtime.println.i1");
void claw_runtime_println_i8(int8_t value) __asm__("claw.runtime.println.i8");
void claw_runtime_println_i16(int16_t value) __asm__("claw.runtime.println.i16");
void claw_runtime_println_i32(int32_t value) __asm__("claw.runtime.println.i32");
void claw_runtime_println_i64(int64_t value) __asm__("claw.runtime.println.i64");
void claw_runtime_println_i128(__int128 value) __asm__("claw.runtime.println.i128");
void claw_runtime_println_float(float value) __asm__("claw.runtime.println.float");
void claw_runtime_println_double(double value) __asm__("claw.runtime.println.double");
void claw_runtime_println_ptr(void* value) __asm__("claw.runtime.println.ptr");
void claw_runtime_println_slice(claw_slice value) __asm__("claw.runtime.println.slice");
void claw_runtime_println_buffer(claw_buffer value) __asm__("claw.runtime.println.buffer");

void claw_runtime_print_i1(_Bool value) {
    fputs(value ? "true" : "false", stdout);
    claw_finish_print(false);
}

void claw_runtime_print_i8(int8_t value) {
    fprintf(stdout, "%d", (int)value);
    claw_finish_print(false);
}

void claw_runtime_print_i16(int16_t value) {
    fprintf(stdout, "%d", (int)value);
    claw_finish_print(false);
}

void claw_runtime_print_i32(int32_t value) {
    fprintf(stdout, "%d", value);
    claw_finish_print(false);
}

void claw_runtime_print_i64(int64_t value) {
    fprintf(stdout, "%lld", (long long)value);
    claw_finish_print(false);
}

void claw_runtime_print_i128(__int128 value) {
    claw_write_i128(value);
    claw_finish_print(false);
}

void claw_runtime_print_float(float value) {
    fprintf(stdout, "%.9g", (double)value);
    claw_finish_print(false);
}

void claw_runtime_print_double(double value) {
    fprintf(stdout, "%.17g", value);
    claw_finish_print(false);
}

void claw_runtime_print_ptr(void* value) {
    fprintf(stdout, "%p", value);
    claw_finish_print(false);
}

void claw_runtime_print_slice(claw_slice value) {
    claw_write_bytes(value.ptr, value.len);
    claw_finish_print(false);
}

void claw_runtime_print_buffer(claw_buffer value) {
    claw_write_bytes(value.ptr, value.len);
    claw_finish_print(false);
}

void claw_runtime_println_i1(_Bool value) {
    fputs(value ? "true" : "false", stdout);
    claw_finish_print(true);
}

void claw_runtime_println_i8(int8_t value) {
    fprintf(stdout, "%d\n", (int)value);
    fflush(stdout);
}

void claw_runtime_println_i16(int16_t value) {
    fprintf(stdout, "%d\n", (int)value);
    fflush(stdout);
}

void claw_runtime_println_i32(int32_t value) {
    fprintf(stdout, "%d\n", value);
    fflush(stdout);
}

void claw_runtime_println_i64(int64_t value) {
    fprintf(stdout, "%lld\n", (long long)value);
    fflush(stdout);
}

void claw_runtime_println_i128(__int128 value) {
    claw_write_i128(value);
    claw_finish_print(true);
}

void claw_runtime_println_float(float value) {
    fprintf(stdout, "%.9g\n", (double)value);
    fflush(stdout);
}

void claw_runtime_println_double(double value) {
    fprintf(stdout, "%.17g\n", value);
    fflush(stdout);
}

void claw_runtime_println_ptr(void* value) {
    fprintf(stdout, "%p\n", value);
    fflush(stdout);
}

void claw_runtime_println_slice(claw_slice value) {
    claw_write_bytes(value.ptr, value.len);
    claw_finish_print(true);
}

void claw_runtime_println_buffer(claw_buffer value) {
    claw_write_bytes(value.ptr, value.len);
    claw_finish_print(true);
}
