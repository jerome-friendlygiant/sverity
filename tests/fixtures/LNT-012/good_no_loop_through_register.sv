// sverity-args: --top top
// Feedback through a register is not a combinational cycle.
// expect-none: LNT-012
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] nxt;
  assign nxt = q + d;
  always_ff @(posedge clk) q <= nxt;
endmodule
