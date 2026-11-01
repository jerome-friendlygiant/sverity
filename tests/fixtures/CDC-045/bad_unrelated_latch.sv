// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-045 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// A correct-phase latch exists on one gate input, but the posedge-launched enable en_q reaches
// the AND gate unlatched: the gated clock can glitch. Any latch in the cone makes it "recognised".
module top(input logic clka, en_in, t, d, output logic q);
  logic en_q, t_q, t_l, gclk;
  always_ff @(posedge clka) begin en_q <= en_in; t_q <= t; end
  always_latch if (!clka) t_l = t_q;
  assign gclk = clka & en_q & t_l;
  always_ff @(posedge gclk) q <= d;
endmodule
