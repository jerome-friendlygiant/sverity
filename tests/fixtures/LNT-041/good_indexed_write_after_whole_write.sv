// sverity-args: --top top
// A write through a run-time index after a write of the whole object leaves no member undriven: the whole write drives every one.
// expect-none: LNT-041
module top(input logic [1:0] i, input logic [1:0] a, input logic [1:0] b, output logic y);
  logic [1:0] m [2];
  always_comb begin
    m = '{a, a};
    m[i] = b;
  end
  assign y = m[0][0] ^ m[1][1];
endmodule
