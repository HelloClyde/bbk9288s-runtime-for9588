# Local data boundary

This project does not contain BBK firmware, applications, system resources, or
device dumps.

Keep local copies of 9288S `D300` applications under `local/`. Generated 9588
`BDA` files and compiler output belong under `build/`. Both directories are
ignored by Git.

`assets/sanguo/` contains only two small Base64-encoded save-container seeds
used to make 9588 NAND metadata persistent. They contain no executable or game
resource, and the installer never overwrites an existing user save.

Only analyze and run application files that you are authorized to use.
