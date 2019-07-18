
/* How many bursts to load in parallel. The GPU should be fully saturated.
   Something like 50 is a good start, depending on number of computing units
   on your card.
   If the value is too low, "kernels" in oclvankus log will be lower than
   specified (and the performance would be of course impaired).
   If the value is too high, the cracker will have high latency.
*/
#define QSIZE 140
/* XXX 80 */

/* size of GPGPU buffer, kernels*slices */
#define CLBLOBSIZE 8191*32
/* 4095 */

/* tables we have */
uint64_t mytables[] = {380, 220, 100,108,116,124,132,140,148,156,164,172,180,188,196,204,212,230,238,250,260,268,276,292,324,332,340,348,356,364,372,388,396,404,412,420,428,436,492,500};
