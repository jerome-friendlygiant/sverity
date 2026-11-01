// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Regression: an unsynchronised synchronous-reset input, and another domain's clock sampled as data, are CDC hazards.
// expect: CDC-029 WARNING
module top(input logic clka, clkb, rst, d, output logic q, output logic s);
  always @(posedge clkb) if (rst) q <= 1'b0; else q <= d;
  always @(posedge clkb) s <= clka;
endmodule
