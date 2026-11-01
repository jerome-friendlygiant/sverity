// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// revert: logic a_q, s1, s2; => (* ASYNC_REG = "TRUE" *) logic a_q; (* ASYNC_REG = "TRUE" *) logic s1; (* ASYNC_REG = "TRUE" *) logic s2;
// revert-expect-none: CDC-012 WARNING
// expect: CDC-012 WARNING
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, a_q};
  assign q = s2;
endmodule
