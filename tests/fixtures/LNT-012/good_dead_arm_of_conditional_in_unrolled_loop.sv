// sverity-args: --top top
// Gray to binary: bin[i] reads bin[i+1] only where i < N-1. Per iteration of the unrolled loop the other arm of the ?: is dead, so the
// out-of-range select is never read and the dependencies form a chain, not a cycle.
// expect-none: LNT-012
module top(input logic [3:0] gray, output logic [3:0] bin);
  logic [3:0] b;
  always_comb for (int i = 0; i < 4; i++) b[i] = gray[i] ^ ((i < 3) ? bin[i+1] : 1'b0);
  assign bin = b;
endmodule
