SEARCH_DIR(.);
SEARCH_DIR(src/cpu/rpcm0/ld);

MEMORY
{
   rom (rx)       : org = 0x10000000, len = 2048k
   ram (rwx)      : org = 0x20000000, len = 264k
}

INCLUDE common_boot2.ld
