// sverity-args: --top top --sdc @DIR@/excl.sdc
// Clocks declared logically exclusive never run together: reported INFO, not as a violation.
// expect-none: CDC-003 ERROR
// expect-none: CDC-003 WARNING
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, b_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q <= 1'b0; else b_q <= a_q;
  assign q = b_q;
endmodule
