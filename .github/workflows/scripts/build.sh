autoreconf --install
./configure CFLAGS="-Wall -Wextra -Werror -fsanitize=address,undefined"
make -k
