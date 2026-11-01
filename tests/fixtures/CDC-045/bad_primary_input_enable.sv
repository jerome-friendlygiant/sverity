// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// a clock gated by a primary-input enable with no latch is also a gating-without-latch hazard (not only an unresolved enable)
// expect: CDC-045 WARNING
module top(input logic clka, en, d, output logic q);
  logic gclk;
  assign gclk = clka & en;
  always_ff @(posedge gclk) q <= d;
endmodule
