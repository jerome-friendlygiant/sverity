// sverity-args: --top top
// The index of one writer is wider than the range proofs model, so whether it meets the other writer's word is not known.
// expect: LNT-049 WARNING 5
module top(input logic clk, we, input logic [127:0] big, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] m [4];
  always_ff @(posedge clk) if (we) m[big] <= d;
  always_ff @(posedge clk) if (!we) m[2] <= ~d;
  assign q = m[0];
endmodule
