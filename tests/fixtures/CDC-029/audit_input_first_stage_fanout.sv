// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 WARNING
// Regression from the independent audit: this hazard must never be classified as safe.
// Asynchronous primary input synchronised by s1/s2, but the metastable first stage s1 also drives logic.
module top(input logic clkb, async_in, output logic q, y);
  logic s1, s2, r;
  always_ff @(posedge clkb) begin s1 <= async_in; s2 <= s1; end
  always_ff @(posedge clkb) r <= ~s1;       // first stage used functionally
  assign q = s2;
  assign y = r;
endmodule
