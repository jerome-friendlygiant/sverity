// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A consumer after the final stage must not hide a tap on the first stage.
// expect: CDC-007 ERROR
module top(input logic clka, clkb, rst_n, d, output logic q, tap);
  logic a_q, s1, s2, consumer;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) consumer <= 1'b0; else consumer <= ~s2;
  assign q = consumer;
  assign tap = ~s1;
endmodule
