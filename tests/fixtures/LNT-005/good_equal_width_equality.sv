// sverity-args: --top top
// An equality of two 4-bit operands, one signed, compares the same bit patterns whatever the signedness. INFO with the proof.
// expect: LNT-005 INFO 6
// expect-none: LNT-005 WARNING
module top(input logic [3:0] u, input logic signed [3:0] s, output logic y);
  assign y = (u == s);
endmodule
