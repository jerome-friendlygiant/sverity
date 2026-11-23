// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// One inversion is a different signal: the inverted clock is a logic root of its own, not clka.
// expect: CDC-002 WARNING
// expect-unresolved: CDC-002
module top(input logic clka, rst_n, d, output logic q);
  logic clk_n;
  assign clk_n = ~clka;
  always_ff @(posedge clk_n or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
