SEARCH_DIR(.);
SEARCH_DIR(src/cpu/rpcm0/ld);

MEMORY
{
   rom (rx)       : org = 0x10000100, len = 2048k - 256
   ram (rwx)      : org = 0x20000000, len = 264k
}

INCLUDE common.ld
