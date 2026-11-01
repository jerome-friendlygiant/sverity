// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(negedge clkb or negedge rst_n) if (!rst_n) q => always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q
// revert-expect-none: CDC-026 ERROR
// expect: CDC-026 ERROR
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  always_ff @(negedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= s1;
endmodule
