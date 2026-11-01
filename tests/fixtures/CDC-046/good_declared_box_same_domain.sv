// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// the data input is launched in the box's own clock domain: no crossing at the box
// expect-none: CDC-046
module HARD_IP(input logic clk, din, output logic dout);
  assign dout = din;
endmodule
module top(input logic clkb, d, output logic q);
  logic b_q, o;
  always_ff @(posedge clkb) b_q <= d;
  HARD_IP u(.clk(clkb), .din(b_q), .dout(o));
  always_ff @(posedge clkb) q <= o;
endmodule
