# Builtin Method Dispatch

Builtin method dispatch is static and compile-time only. There is no hidden dynamic dispatch.

The revised language direction is **receiver-first**:

```cat
val numbers = Vec[Int32][1, 2, 3]
val size = numbers.len()
```

This file tracks the builtin subset that is already implemented in the compiler core, while the revised type surface is still being normalized.

## Revised Direction

- dispatch hangs off the receiver value, not helper namespaces
- method availability is resolved by semantic analysis from the receiver type
- dispatch stays static whenever possible to preserve `Fast`
- borrow-sensitive methods must keep ownership and view tracking explicit

## Current Implemented Receiver Groups

### Str

Current implemented subset:
- `len()`
- `is_empty()`
- `byte_at(index)`
- `first_byte()`
- `last_byte()`
- `find_byte(byte)`
- `count_byte(byte)`
- `starts_with(prefix)`
- `ends_with(suffix)`
- `contains(part)`
- `contains_byte(byte)`
- `slice(start, len)`

Notes:
- the compiler core already treats string-like receivers as byte-backed
- the final revised iteration story for `Str` is still pending
- default iteration over `Str` values has not been switched to the final `Char`-first model yet

### Bytes

Current implemented subset:
- `len()`
- `is_empty()`
- `byte_at(index)`
- `first_byte()`
- `last_byte()`
- `find_byte(byte)`
- `count_byte(byte)`
- `starts_with(prefix)`
- `ends_with(suffix)`
- `contains(part)`
- `contains_byte(byte)`
- `slice(start, len)`
- `capacity()`
- `has_capacity(min)`
- `reserve(min)`
- `truncate(len)`
- `shrink_to_fit()`
- `clear()`

### Vec[T]

Current implemented subset:
- `len()`
- `is_empty()`
- `capacity()`
- `has_capacity(min)`
- `reserve(min)`
- `truncate(len)`
- `shrink_to_fit()`
- `clear()`

### Map[K, V]

Current implemented subset:
- `len()`
- `is_empty()`
- `capacity()`
- `has_capacity(min)`
- `reserve(min)`
- `shrink_to_fit()`
- `clear()`

### Set[T]

Current implemented subset:
- `len()`
- `is_empty()`
- `capacity()`
- `has_capacity(min)`
- `reserve(min)`
- `shrink_to_fit()`
- `clear()`

### Ring[T]

Current implemented subset:
- `len()`
- `is_empty()`
- `capacity()`
- `has_capacity(min)`
- `reserve(min)`
- `truncate(len)`
- `shrink_to_fit()`
- `clear()`

## Migration Notes

This file is intentionally conservative.

What is already true:
- dispatch is static
- receiver typing is checked in sema
- mutable-only methods stay gated by mutable receiver rules
- view-returning helpers keep ownership tracking involved

What is not finished yet:
- final revised naming for every receiver family
- the final `Str` / `Char` iteration model
- the broader receiver-first collection API planned in the revised PRD
