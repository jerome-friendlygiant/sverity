// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// HARD_IP is clocked by clkb (port role clock) but its data input is launched in clka (declared asynchronous)
// expect: CDC-046 ERROR
module HARD_IP(input logic clk, din, output logic dout);
  assign dout = din;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic a_q, o;
  always_ff @(posedge clka) a_q <= d;
  HARD_IP u(.clk(clkb), .din(a_q), .dout(o));
  always_ff @(posedge clkb) q <= o;
endmodule
