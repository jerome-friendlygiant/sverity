// sverity-args: --top top
// A word written only at a constant index is a register: no memory, no collision obligation.
// expect-none: LNT-082
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) mem[1] <= d;
  always_ff @(posedge clk) q <= mem[1];
endmodule
