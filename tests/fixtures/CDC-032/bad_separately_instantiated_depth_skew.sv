// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-032 ERROR
module sync #(parameter int N=2) (input logic clk, d, output logic q);
  logic [N-1:0] r;
  always_ff @(posedge clk) r <= {r[N-2:0], d};
  assign q = r[N-1];
endmodule
module top(input logic clka, clkb, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a_q;
  always_ff @(posedge clka) a_q <= d;
  sync #(2) u0(.clk(clkb), .d(a_q[0]), .q(q[0]));
  sync #(3) u1(.clk(clkb), .d(a_q[1]), .q(q[1]));
endmodule
