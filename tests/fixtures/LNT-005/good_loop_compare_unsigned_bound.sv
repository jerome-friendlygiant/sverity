// sverity-args: --top top
// Plain P1: a counting loop from 0 by 1 is compared with an unsigned constant bound. Every value of i, and the value that fails
// the test, is non-negative, so the signed/unsigned comparison is value-preserving. The finding is INFO with the proof.
// expect: LNT-005 INFO 10
// expect-none: LNT-005 WARNING
module top(input logic [7:0] d, output logic [3:0] n);
  localparam int unsigned LIMIT = 8;
  always_comb begin
    n = '0;
    for (int i = 0; i < LIMIT; i++) n += d[i];
  end
endmodule
