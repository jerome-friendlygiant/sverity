// sverity-args: --top top
// expect-none: LNT-082
module top(input logic clk, input logic [1:0] addr, input logic [7:0] d);
  logic [7:0] mem [4];
  always_ff @(posedge clk) mem[addr] <= d;
endmodule
