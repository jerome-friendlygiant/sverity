// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The synchroniser lives in a child module reached through ports: still recognised.
// expect: CDC-003 INFO
// expect-none: CDC-003 ERROR
// expect-none: CDC-003 WARNING
module sync2(input logic clk, rst_n, d, output logic q);
  logic s1;
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) {q, s1} <= 2'b00; else {q, s1} <= {s1, d};
endmodule
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  sync2 u_sync(.clk(clkb), .rst_n(rst_n), .d(a_q), .q(q));
endmodule
