// sverity-args: --top top --library @FIXTURES@/LNT-012/registered_macro.json
// The library declares both data ports synchronous to the cell's clock: the path through it is registered.
// expect-none: LNT-012
module MAC_REG(input logic clk, input logic [3:0] a, output logic [3:0] y);
endmodule
module top(input logic clk, input logic [3:0] seed, output logic [3:0] result);
  logic [3:0] mac_in;
  assign mac_in = seed + result;
  MAC_REG u_mac(.clk(clk), .a(mac_in), .y(result));
endmodule
