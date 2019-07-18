/* Reverse bits in u64 (ok, it should be a optimized) */
uint64_t rev(uint64_t r) {
        uint64_t r1 = r;
        uint64_t r2 = 0;
        for (int j = 0; j < 64 ; j++ ) {
                r2 = (r2<<1) | (r1 & 0x01);
                r1 = r1 >> 1;
        }
        return r2;
}

