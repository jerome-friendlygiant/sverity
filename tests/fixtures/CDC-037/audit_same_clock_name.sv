// sverity-args: --top top --sdc @DIR@/same2.sdc
// expect: CST-018 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// Two distinct clock input ports, one SDC clock name covers both; no synchroniser.
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= a_q;
endmodule
