// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of good_low_phase_latch_gate.sv: the enable is held by a falling-edge flop instead of a low-transparent latch.
// expect: CDC-045 WARNING
// revert: always_ff @(negedge clka) en_l <= en_q; => always_latch if (!clka) en_l = en_q;
// revert-expect-none: CDC-045 WARNING
module top(input logic clka, en_d, rst_n, d, output logic q);
  logic en_q, en_l, gclk;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) en_q <= 1'b0; else en_q <= en_d;
  always_ff @(negedge clka) en_l <= en_q;
  assign gclk = clka & en_l;
  always_ff @(posedge gclk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
