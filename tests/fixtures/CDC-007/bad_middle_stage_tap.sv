// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign tap = s2; => assign tap = 1'b0;
// revert-expect-none: CDC-007 ERROR
// expect: CDC-007 ERROR
module top(input logic clka, clkb, rst_n, d, output logic q, tap);
  logic a_q, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, a_q};
  assign q = s3;
  assign tap = s2;
endmodule
