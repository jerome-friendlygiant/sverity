// A constant read address that no writer overlaps cannot collide with a write.
// sverity-args: --top top
// expect-none: LNT-082
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) mem[0] <= d;
  always_ff @(posedge clk) q <= mem[1];
endmodule
