// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Three inversions: two cancel (clk_n is ~~n1), the third does not, so the clock is still a logic root of its own (n1).
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
module top(input logic clka, rst_n, d, output logic q);
  logic n1, n2, clk_n;
  assign n1 = ~clka;
  assign n2 = ~n1;
  assign clk_n = ~n2;
  always_ff @(posedge clk_n or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
