// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The first synchroniser stage also feeds functional logic: ERROR (declared async).
// expect: CDC-007 ERROR
module top(input logic clka, clkb, rst_n, d, output logic q, output logic leak);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= 2'b00; else {s2, s1} <= {s1, a_q};
  assign q = s2;
  assign leak = s1;
endmodule
