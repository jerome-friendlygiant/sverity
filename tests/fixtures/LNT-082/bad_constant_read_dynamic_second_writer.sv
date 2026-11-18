// A constant read of an unwritten word still meets the writer with a dynamic address.
// sverity-args: --top top
// expect: LNT-082 WARNING 8
module top(input logic clk, input logic [1:0] wa, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) mem[0] <= d;
  always_ff @(posedge clk) mem[wa] <= d;
  always_ff @(posedge clk) q <= mem[1];
endmodule
