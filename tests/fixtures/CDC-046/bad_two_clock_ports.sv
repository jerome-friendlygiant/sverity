// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --library @DIR@/cells.json
// each data port names its own clock; din_b is fed from clka but is synchronous to clkb
// expect: CDC-046 ERROR
module TWO_CLK_IP(input logic clk_a, clk_b, din_a, din_b, output logic dout);
  assign dout = din_a & din_b;
endmodule
module top(input logic clka, clkb, d, output logic q);
  logic a_q, b_q, o;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) b_q <= d;
  TWO_CLK_IP u(.clk_a(clka), .clk_b(clkb), .din_a(a_q), .din_b(a_q), .dout(o));
  always_ff @(posedge clkb) q <= o;
endmodule
