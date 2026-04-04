# Builtin Method Dispatch

Builtin method dispatch is static and compile-time only. There is no hidden dynamic dispatch.

The revised language direction is **receiver-first**:

```cat
val numbers = Vec[Int32][1, 2, 3]
val size = numbers.len()
```

## Design Principles

- dispatch hangs off the receiver value, not helper namespaces
- method availability is resolved by semantic analysis from the receiver type
- dispatch stays static whenever possible to preserve `Fast`
- borrow-sensitive methods must keep ownership and view tracking explicit

## Receiver Groups

### Str

- `len() -> USize`
- `is_empty() -> Bool`
- `byte_at(index: USize) -> UInt8`
- `first_byte() -> UInt8`
- `last_byte() -> UInt8`
- `find_byte(byte: UInt8) -> Int64`
- `count_byte(byte: UInt8) -> USize`
- `starts_with(prefix: ref Str) -> Bool`
- `ends_with(suffix: ref Str) -> Bool`
- `contains(part: ref Str) -> Bool`
- `contains_byte(byte: UInt8) -> Bool`
- `slice(start: USize, len: USize) -> ref Str`

Notes:
- `Str` is byte-backed; byte methods operate on raw UTF-8 bytes
- `scan` over `Str` yields `UInt8` (byte-level iteration)
- `Char`-first iteration is planned but not yet implemented

### Anchor[T]

- `get() -> ref T`

Notes:
- `Anchor.new(value)` is a built-in constructor, not a free function
- `Anchor.get()` returns `ref T` directly
- `Anchor[T]` only accepts owned payloads; borrowed payloads such as `ref T` or `Span[T]` are rejected
### Span[T]

- `len() -> USize`
- `is_empty() -> Bool`

### Vec[T]

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize)`
- `truncate(len: USize)`
- `shrink_to_fit()`
- `clear()`

### Map[K, V]

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize)`
- `shrink_to_fit()`
- `clear()`

### Set[T]

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize)`
- `shrink_to_fit()`
- `clear()`

### Queue[T]

- `len() -> USize`
- `is_empty() -> Bool`
- `capacity() -> USize`
- `has_capacity(min: USize) -> Bool`
- `reserve(min: USize)`
- `truncate(len: USize)`
- `shrink_to_fit()`
- `clear()`

## Migration Status

- All receiver groups now use finalized PRD names
- Legacy types (`Bytes`, `Ring[T]`, `Table[K,V]`) have been removed
- Return types use `UInt8` and `Int64` instead of legacy `Byte` / `ISize`

