# About

This project aims to provide a full-featured [exFAT](http://en.wikipedia.org/wiki/ExFAT) file system implementation for Unix-like systems. It consists of a [FUSE](http://en.wikipedia.org/wiki/Filesystem_in_Userspace) module (fuse-exfat) and a set of utilities (exfat-utils).

Supported operating systems:

* GNU/Linux
* Mac OS X 10.5 or later
* FreeBSD
* OpenBSD

Most GNU/Linux distributions already have fuse-exfat and exfat-utils in their repositories, so you can just install and use them. The next chapter describes how to compile them from source.

# Compiling

To build this project under GNU/Linux you need to install the following packages:

* git
* scons
* fuse-devel (or libfuse-dev)
* gcc

Get the source code, change directory and compile:

```
git clone https://github.com/relan/exfat.git
cd exfat
scons
```

Then install driver and utilities:

```
sudo scons install
```

# Mounting

Modern GNU/Linux distributions will mount exFAT volumes automatically—util-linux-ng 2.18 (was renamed to util-linux in 2.19) is required for this. Anyway, you can mount manually (you will need root privileges):

```
sudo mount.exfat-fuse /dev/sdXn /mnt/exfat
```

where /dev/sdXn is the partition special file, /mnt/exfat is a mountpoint.

# Feedback

If you have any questions, issues, suggestions, bug reports, etc. please create an [issue](https://github.com/relan/exfat/issues). Pull requests are also welcome!
