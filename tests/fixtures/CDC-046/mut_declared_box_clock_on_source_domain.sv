// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// MUTATION of good_declared_box_same_domain.sv: the box clock port is wired to the source domain, its data input is launched in clkb.
// expect: CDC-046 ERROR
// revert: HARD_IP u(.clk(clka), => HARD_IP u(.clk(clkb),
// revert-expect-none: CDC-046 ERROR
module HARD_IP(input logic clk, din, output logic dout);
  assign dout = din;
endmodule
module top(input logic clka, clkb, d, output logic q, r);
  logic a_q, b_q, o;
  always_ff @(posedge clka) a_q <= d;
  assign r = a_q;
  always_ff @(posedge clkb) b_q <= d;
  HARD_IP u(.clk(clka), .din(b_q), .dout(o));
  always_ff @(posedge clkb) q <= o;
endmodule
