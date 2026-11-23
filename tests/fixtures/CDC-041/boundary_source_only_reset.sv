// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Source reset without a reset on the required synchronizer output stage is CDC-041's INFO boundary case.
// expect: CDC-041 INFO
// expect-confidence: CDC-041 heuristic
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic src_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) src_q <= 1'b0; else src_q <= d;
  always_ff @(posedge clkb) begin s1 <= src_q; s2 <= s1; end
  assign q = s2;
endmodule
