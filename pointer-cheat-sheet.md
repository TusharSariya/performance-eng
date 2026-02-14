# C Pointer Cheat Sheet

## Basics

| Symbol | Meaning | Example |
|--------|---------|---------|
| `&x` | Address of `x` | `p = &x` → p holds x's address |
| `*p` | Dereference: value p points to | `y = *p` or `*p = 10` |
| `*` in declaration | "pointer to" (part of type) | `int *p` → p is pointer to int |

**Mental model:** A pointer is a variable that holds an address. `&` = "address of." `*` (in an expression) = "follow the pointer."

---

## Declaration syntax

```c
int *p;           // p is pointer to int
int *a, *b;       // both a and b are pointers (need * each time)
char *s;          // s is pointer to char (e.g. string)
int **pp;         // pp is pointer to (pointer to int)
const int *p;     // p points to const int — can't change *p
int *const p;     // p is const — can't change p, can change *p
```

Read right-to-left: "p is a pointer to int", "pp is a pointer to pointer to int".

---

## Common operations

```c
int x = 42;
int *p = &x;      // p points to x
*p = 99;          // same as x = 99
int y = *p;       // y gets value at p (99)
p = NULL;         // p points to nothing (don't dereference!)
```

---

## Pointers and arrays

- Array name **decays** to pointer to first element: `arr` ↔ `&arr[0]`
- `arr[i]` is exactly `*(arr + i)`
- Pointer arithmetic is in **elements**, not bytes: `p + 1` adds `sizeof(*p)` bytes

```c
int arr[] = {10, 20, 30};
int *p = arr;
*(p + 1) = 99;    // same as arr[1] = 99
p[2] = 0;         // same as arr[2] = 0
```

---

## Why pointer-to-pointer (`**`)?

C passes by value. To let a function **change the caller's pointer** (e.g. after `malloc`), pass the address of that pointer:

```c
void alloc_int(int **out) {
    *out = malloc(sizeof(int));
    **out = 42;
}
int *ptr = NULL;
alloc_int(&ptr);   // ptr now points to allocated int with 42
```

- `out` = address of caller's pointer (the "middle man")
- `*out` = the pointer we're updating
- `**out` = the int we're writing to

---

## Allocation and freeing

```c
int *p = malloc(10 * sizeof(int));
// use p...
free(p);
p = NULL;          // avoid use-after-free

void *a = aligned_alloc(64, 64);  // 64-byte aligned block
free(a);
```

- `malloc`/`aligned_alloc` return pointer; check for `NULL`.
- Only `free` what you got from malloc/calloc/realloc/aligned_alloc.

---

## Quick reference

| You want… | Use |
|-----------|-----|
| Address of variable | `&x` |
| Value at pointer | `*p` |
| Declare pointer | `type *name` |
| Pass pointer so function can change pointee | `void f(type *p)` |
| Pass pointer so function can change the pointer itself | `void f(type **p)`, call with `&ptr` |
| Null pointer | `NULL` or `(void*)0` |
| Pointer to first element of array | `arr` or `&arr[0]` |
