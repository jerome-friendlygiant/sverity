// A foreign call has read effects the tool cannot examine while a memory has a writer.
// sverity-args: --top top
// expect: LNT-082 WARNING 8
module top(input logic clk, input logic [1:0] wa, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  import "DPI-C" function logic [7:0] peek(input int a);
  always_ff @(posedge clk) mem[wa] <= d;
  always_ff @(posedge clk) q <= peek(1);
endmodule
