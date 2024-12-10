SEARCH_DIR(.);
SEARCH_DIR(src/cpu/rpcm0/ld);

MEMORY
{
   rom (rx)       : org = 0x10000100, len = 2048k - 256
   ram (rwx)      : org = 0x20000000, len = 256k
   scratch_x(rwx) : org = 0x20040000, len = 4k
   scratch_y(rwx) : org = 0x20041000, len = 4k
}

INCLUDE common.ld
