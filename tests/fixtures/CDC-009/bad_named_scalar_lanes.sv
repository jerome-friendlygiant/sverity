// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-confidence: CDC-009 established
module sync2(input logic clk, d, output logic q); logic s1; always_ff @(posedge clk) begin s1 <= d; q <= s1; end endmodule
module top(input logic clka, clkb, d0, d1, output logic [1:0] q);
  logic [1:0] a_q; always_ff @(posedge clka) a_q <= {d1,d0};
  sync2 u0(.clk(clkb), .d(a_q[0]), .q(q[0]));
  sync2 u1(.clk(clkb), .d(a_q[1]), .q(q[1]));
endmodule
