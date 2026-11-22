dd if=/dev/zero of=foo.img bs=1048576 count=1
mkfs/mkexfatfs -i 12345678 foo.img
fsck/exfatfsck foo.img
echo 'f1b3a11f781533f5b69086596be38367d0ebfb77  foo.img' | shasum -c
label/exfatlabel foo.img ￮￮￮￮￮￮￮￮￮￮￮￮￮￮￮
echo '4720462db953d5b2094c884f4b3098a138790533  foo.img' | shasum -c
