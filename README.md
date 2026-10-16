# PNG

A small C program that creates a PNG file.

It writes a 100x100 px RGB PNG. The image data uses an uncompressed DEFLATE block, so no compression library is needed.

## Build

```sh
gcc src/png.c -o png
```

## Usage

```sh
./png <output.png>
```

## Example

This is how it looks:

![](examples/output.png)
