#!/bin/bash

# The following links may help you in case you get lost:
#  https://en.wikipedia.org/wiki/A5/1#Description
#  https://www.youtube.com/watch?v=LgZAI3DdUA4
#  https://en.wikipedia.org/wiki/Karnaugh_map
#  http://archive.today/hTu5n

### Header

echo 'kernel void krak(global ulong *buf, ulong slices) {
  
  /* kernel index */
  private size_t me = get_global_id(0); 

  /* pointer to memory "owned" by this kernel instance */
  private size_t myptr = me * 4 * 64;

  private long i,j,z;
  private ulong diff,res;

  //private ulong flags = buf[myptr + i*4 + 3];

  /* slice algorithm variables */

  private ulong r[64];         // a5/1 registers, serialized
  private ulong ch[64];        // challenge
  private ulong rf[64];        // reduction function
  private ulong keystream[64]; // generated keystream
  private ulong mask = 0;      // running slices

  private ulong cr1r2, cr1r3, cr2r3, clock1, clock2, clock3, fb1, fb2, fb3;
  private ulong iclk1, iclk2, iclk3;

  //private ulong prevlink[64];

  /* fill the machine */

  for(i=0; i<64; i++) {
    r[i] = 0;
    rf[i] = 0;
    ch[i] = 0;
  }

  /* initial register value */
  for(i=0; i<64; i++) {             // for each slice

    /* Is this slice active, i.e., non-zero? */
    if (buf[myptr + i*4] != 0) {
      mask |= 1ULL << i;
    }

    //mask |= (buf[myptr + i*4] != 0) << i;

    //printf("adding %llx %llx\n",buf[me + i*4], buf[myptr + i*4 + 1]);
    for(j=0; j<64; j++) {          // add each bit to the right place
      r[63-j] |= ((buf[myptr + i*4] >> j) & 1) << i;
    }
  }

  /* reduction function */
  for(i=0; i<64; i++) { // for each slice
    //printf("color %llx\n",buf[me + i*4 + 1]);
    for(j=0; j<64; j++) { // for each bit
      rf[63-j] |= ((buf[myptr + i*4 + 1] >> j) & 1) << i;
    }
  }

  /* challenge */
  for(i=0; i<64; i++) { // for each slice
    //printf("challenge %llx\n",buf[me + i*4 + 2]);
    for(j=0; j<64; j++) { // for each bit
      ch[63-j] |= ((buf[myptr + i*4 + 2] >> j) & 1) << i;
    }
  }
'

### Vector declaration

# four-vectors recommended by AMD OpenCL Guide
# so use 16*4-vec to hold 64 bits of A5/1

vec=ulong4
for i in `seq 1 16`; do
  # A5/1 register
  echo "private $vec reg$i;"
done

# debug - is it initialized OK?
echo '
  /*printf("input:  ");
  for(i=0; i<64; i++) {
    printf("%x", (r[i]&1));
  }
  printf("\n");*/'

### Slice machine running

echo 'res = mask;'
echo 'for(z=0; z<1000; z++) {'

## Apply reduction function. And restore the register state.
# And do not modify not-clocked slices (~res).
for i in `seq 0 63`; do
  echo "r[$i] ^= rf[$i] & res;"
done

for i in `seq 1 16`; do
  aidx=$(( ( $i-1 ) * 4 ))
  echo "reg$i.x = r[$aidx + 0];"
  echo "reg$i.y = r[$aidx + 1];"
  echo "reg$i.z = r[$aidx + 2];"
  echo "reg$i.w = r[$aidx + 3];"
done

## See which slices are in distinguished point, i.e., they have zeros in 12 LSBs.
# If we are not "full", we will have some slices inherently zero.
# Hence, we force the result to be 1 in them with |~mask

echo 'res = reg1.x | reg1.y | reg1.z | reg1.w |
      reg2.x | reg2.y | reg2.z | reg2.w |
      reg3.x | reg3.y | reg3.z | reg3.w;'

## If this is the first iteration, all instances that are entering from
# a previous distinguished point will *of* *course* be zero. So ignore them.
# FIXME without conditional (bit smearing)
echo 'if (z == 0) {
        res = mask;
      }';

## We would increment color here, but I don't want to mess with it in OpenCL
# as it would probably require excessive branching, so
# we let our parent handle this - by breaking later. The user-space library
# will notice it and resubmit the fragment with color changed.

## Now, we run 99 dummy cycles to collapse the keyspace.

echo '#pragma unroll 9'
echo 'for (j = 0; j<99; j++) {'

# First we solve A5/1 irregular clocking. We will need this again in the future,
# so we create a function for it.

function a5_ireg_init {

  # Majority clocking.
  # When these bits are the same, then XOR returns 0 and ~ returns 1 et vice versa
  echo 'cr1r2 = ~(reg14.w ^ reg9.z);
        cr1r3 = ~(reg14.w ^ reg4.x);
        cr2r3 = ~(reg9.z  ^ reg4.x);'

  # So now clockX = instances whose regX is being clocked
  echo 'clock1 = cr1r2 | cr1r3;
        clock2 = cr1r2 | cr2r3;
        clock3 = cr1r3 | cr2r3;'

  # We don't want to clock instances that are already in distinguished point.
  # By the way original slicer just breaks the kernel once one instance reaches
  # DP - but we don't want such frequent resubmits
  # (they would happen on average chainlen/slicewidth, i.e. 2**12/32, or every
  # 128th iteration)

  echo 'clock1 &= res;
        clock2 &= res;
        clock3 &= res;'

  echo '//printf("res %x clk %x %x %x\n", res, clock1, clock2, clock3);'

  # LFSR feedback
  echo 'fb1 = (reg12.y ^ reg12.z ^ reg12.w ^ reg13.z) & clock1;
        fb2 = (reg6.w ^ reg7.x) & clock2;
        fb3 = (reg1.x ^ reg1.y ^ reg1.z ^ reg4.w) & clock3;'

  # Precompute clock inverse.
  echo 'iclk1 = ~clock1;
        iclk2 = ~clock2;
        iclk3 = ~clock3;'

}; a5_ireg_init

# The looping itself. Again a function, we don't want to have it here multiple
#  times!
# Relax, grab the Wikipedia image on it
#  https://en.wikipedia.org/wiki/File:A5-1_GSM_cipher.svg
#  and see how does it happen. It's just the rotation expressed explicitly!

function a5_one_step {

  echo 'reg1.x = (reg1.x & iclk3) | (reg1.y & clock3);
        reg1.y = (reg1.y & iclk3) | (reg1.z & clock3);
        reg1.z = (reg1.z & iclk3) | (reg1.w & clock3);
        reg1.w = (reg1.w & iclk3) | (reg2.x & clock3);
        reg2.x = (reg2.x & iclk3) | (reg2.y & clock3);
        reg2.y = (reg2.y & iclk3) | (reg2.z & clock3);
        reg2.z = (reg2.z & iclk3) | (reg2.w & clock3);
        reg2.w = (reg2.w & iclk3) | (reg3.x & clock3);
        reg3.x = (reg3.x & iclk3) | (reg3.y & clock3);
        reg3.y = (reg3.y & iclk3) | (reg3.z & clock3);
        reg3.z = (reg3.z & iclk3) | (reg3.w & clock3);
        reg3.w = (reg3.w & iclk3) | (reg4.x & clock3);
        reg4.x = (reg4.x & iclk3) | (reg4.y & clock3);
        reg4.y = (reg4.y & iclk3) | (reg4.z & clock3);
        reg4.z = (reg4.z & iclk3) | (reg4.w & clock3);
        reg4.w = (reg4.w & iclk3) | (reg5.x & clock3);
        reg5.x = (reg5.x & iclk3) | (reg5.y & clock3);
        reg5.y = (reg5.y & iclk3) | (reg5.z & clock3);
        reg5.z = (reg5.z & iclk3) | (reg5.w & clock3);
        reg5.w = (reg5.w & iclk3) | (reg6.x & clock3);
        reg6.x = (reg6.x & iclk3) | (reg6.y & clock3);
        reg6.y = (reg6.y & iclk3) | (reg6.z & clock3);
        reg6.z = (reg6.z & iclk3) | fb3;
        reg6.w = (reg6.w & iclk2) | (reg7.x & clock2);
        reg7.x = (reg7.x & iclk2) | (reg7.y & clock2);
        reg7.y = (reg7.y & iclk2) | (reg7.z & clock2);
        reg7.z = (reg7.z & iclk2) | (reg7.w & clock2);
        reg7.w = (reg7.w & iclk2) | (reg8.x & clock2);
        reg8.x = (reg8.x & iclk2) | (reg8.y & clock2);
        reg8.y = (reg8.y & iclk2) | (reg8.z & clock2);
        reg8.z = (reg8.z & iclk2) | (reg8.w & clock2);
        reg8.w = (reg8.w & iclk2) | (reg9.x & clock2);
        reg9.x = (reg9.x & iclk2) | (reg9.y & clock2);
        reg9.y = (reg9.y & iclk2) | (reg9.z & clock2);
        reg9.z = (reg9.z & iclk2) | (reg9.w & clock2);
        reg9.w = (reg9.w & iclk2) | (reg10.x & clock2);
        reg10.x = (reg10.x & iclk2) | (reg10.y & clock2);
        reg10.y = (reg10.y & iclk2) | (reg10.z & clock2);
        reg10.z = (reg10.z & iclk2) | (reg10.w & clock2);
        reg10.w = (reg10.w & iclk2) | (reg11.x & clock2);
        reg11.x = (reg11.x & iclk2) | (reg11.y & clock2);
        reg11.y = (reg11.y & iclk2) | (reg11.z & clock2);
        reg11.z = (reg11.z & iclk2) | (reg11.w & clock2);
        reg11.w = (reg11.w & iclk2) | (reg12.x & clock2);
        reg12.x = (reg12.x & iclk2) | fb2;
        reg12.y = (reg12.y & iclk1) | (reg12.z & clock1);
        reg12.z = (reg12.z & iclk1) | (reg12.w & clock1);
        reg12.w = (reg12.w & iclk1) | (reg13.x & clock1);
        reg13.x = (reg13.x & iclk1) | (reg13.y & clock1);
        reg13.y = (reg13.y & iclk1) | (reg13.z & clock1);
        reg13.z = (reg13.z & iclk1) | (reg13.w & clock1);
        reg13.w = (reg13.w & iclk1) | (reg14.x & clock1);
        reg14.x = (reg14.x & iclk1) | (reg14.y & clock1);
        reg14.y = (reg14.y & iclk1) | (reg14.z & clock1);
        reg14.z = (reg14.z & iclk1) | (reg14.w & clock1);
        reg14.w = (reg14.w & iclk1) | (reg15.x & clock1);
        reg15.x = (reg15.x & iclk1) | (reg15.y & clock1);
        reg15.y = (reg15.y & iclk1) | (reg15.z & clock1);
        reg15.z = (reg15.z & iclk1) | (reg15.w & clock1);
        reg15.w = (reg15.w & iclk1) | (reg16.x & clock1);
        reg16.x = (reg16.x & iclk1) | (reg16.y & clock1);
        reg16.y = (reg16.y & iclk1) | (reg16.z & clock1);
        reg16.z = (reg16.z & iclk1) | (reg16.w & clock1);
        reg16.w = (reg16.w & iclk1) | fb1;'

}; a5_one_step

echo '}' # end of for 99

## Now do the actual encryption - generate the keystream.
# We will use the subroutines defined above.

echo '#pragma unroll 8'
echo 'for (j = 0; j<64; j++) {'

  a5_ireg_init

  a5_one_step

  # Output bits
  echo 'keystream[j] = (reg1.x ^ reg6.w ^ reg12.y);'

echo '}'


## Check for challenge

# At first, only slices that are not in distinguished point
# (FIXME: do we lose challenge that has 12 zeros in the beginning?)

echo 'diff = ~res;'

# Now OR the whole difference
echo 'for(i=0; i<64; i++) {
        diff |= ch[i] ^ keystream[i];
      }'

# And ok, break once we found one. This happens only extremely rarely.
# (yes, we could restore, update mask and continue)
echo 'if(diff != 0xFFFFFFFFFFFFFFFFULL) {
        //printf("RETURN FINISH\n");
        break;
      }'

# Use the computed keystream in the next round.
# And don't forget to revert non-clocked ones...
echo 'for(i=0; i<64; i++) {
        r[i] = (keystream[i] & res) | (r[i] & ~res);
      }'

echo '/*  printf("output %05i: ",z);
        for(i=0; i<64; i++) {
                printf("%x", (r[i]&1));
        }
        printf("\n");*/
'

echo '}' # slice machine

## Copy data back

echo 'for(i=0; i<64; i++) {             // for each slice
        buf[myptr + i*4] = 0;
        buf[myptr + i*4 + 3] |= ((~res >> i) & 1) << 0; // set color-end flag
        buf[myptr + i*4 + 3] |= ((~diff >> i) & 1) << 1; // set found flag
        for(j=63; j>=0; j--) {
          buf[myptr + i*4] |= ((r[63-j] >> i) & 1) << j;
        }
      }'

echo '}' # kernel exit


