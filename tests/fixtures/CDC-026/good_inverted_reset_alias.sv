// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-026
// expect-none: CDC-041
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1, rst;
  assign rst = ~rst_n;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  always_ff @(posedge clkb or posedge rst) if (rst) q <= 1'b0; else q <= s1;
endmodule
