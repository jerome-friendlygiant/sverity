// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// a primary input with no declared relation feeds a declared box: unresolved, never safe
// expect: CDC-046 WARNING
module HARD_IP(input logic clk, din, output logic dout);
  assign dout = din;
endmodule
module top(input logic clkb, d, output logic q);
  logic o;
  HARD_IP u(.clk(clkb), .din(d), .dout(o));
  always_ff @(posedge clkb) q <= o;
endmodule
