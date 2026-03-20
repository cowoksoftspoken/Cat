# Builtin Method Dispatch

Current builtin method dispatch is static and compile-time only. There is no hidden dynamic dispatch.

## Text

- `len() -> USize`
- `is_empty() -> Bool`
- `byte_at(index: USize) -> Byte`
- `first_byte() -> Byte`
- `last_byte() -> Byte`
- `find_byte(byte: Byte) -> ISize`
- `count_byte(byte: Byte) -> USize`
- `starts_with(prefix: look Text) -> Bool`
- `ends_with(suffix: look Text) -> Bool`
- `contains(part: look Text) -> Bool`
- `contains_byte(byte: Byte) -> Bool`
- `slice(start: USize, len: USize) -> look Text`

## Bytes

- `len() -> USize`
- `is_empty() -> Bool`
- `byte_at(index: USize) -> Byte`
- `first_byte() -> Byte`
- `last_byte() -> Byte`
- `find_byte(byte: Byte) -> ISize`
- `count_byte(byte: Byte) -> USize`
- `starts_with(prefix: look Bytes) -> Bool`
- `ends_with(suffix: look Bytes) -> Bool`
- `contains(part: look Bytes) -> Bool`
- `contains_byte(byte: Byte) -> Bool`
- `slice(start: USize, len: USize) -> look Bytes`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize) -> Unit`
- `truncate(len: USize) -> Unit`
- `shrink_to_fit() -> Unit`
- `clear() -> Unit`

## Vec

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize) -> Unit`
- `truncate(len: USize) -> Unit`
- `shrink_to_fit() -> Unit`
- `clear() -> Unit`

## Table

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize) -> Unit`
- `shrink_to_fit() -> Unit`
- `clear() -> Unit`

## Set

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize) -> Unit`
- `shrink_to_fit() -> Unit`
- `clear() -> Unit`

## Ring

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize) -> Unit`
- `truncate(len: USize) -> Unit`
- `shrink_to_fit() -> Unit`
- `clear() -> Unit`
