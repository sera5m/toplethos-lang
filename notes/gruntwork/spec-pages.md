# Already written

This is a copy of the rule that is now in USE.md, section Pages. Do not implement it as a speed pass. Do not page-align the battery objects.

A page is 4096 bytes. Placement is not a type and not a mark. `@paged` is residency. `@memory_hard` is a file.

Size is the laid-out size: alignment padding, present flag, mutex extra bytes. A child includes the parent prefix. An embedded object is not placed alone. An empty object takes no page.

Alignment is max(16, C++ alignment). A mutex cell is also 64-byte aligned. If that pad would cross the page, start the next page.

| size | placement |
|---|---|
| 0 | no page |
| 1 … 2048 | pack. 2048 fits 2, 1024 fits 4, 512 fits 8 |
| 2049 … 4096 | one per page, base on the boundary. The spare stays empty. It is not a load block |

A load block is separate from that spare. `@blockinit[name, pages];` names `pages` pages (default 1). `@blockpack[name]` places a cell in it, in source order, or it does not fit and the compile fails. `@blockpush` / `@blockpop` only set which block a bare `@blockpack` fills. They do not load a page. A `@paged` block is loaded with `lock` and `hold`. `getblockidbyname` and `getnamebyblockid` only name it.
| 4097+ | base on the boundary, contiguous, ceil(size/4096) pages |

Arrays stay contiguous. Do not page-align elements. A frame uses the same table on the stack. A frame of 4096 or less stays on the stack. No malloc to force a page.

Context per live name: page, offset, length, resident, dirty, other-writer. Load a page only when the next op names one that is not resident. Do not move a live object. Two runs may share a page. Two mutable cells of different runs are 64 bytes apart. A huge page is only for one allocation of at least 2 MiB.

The interpreter uses the same sizes and does not insert 4096-byte gaps.
