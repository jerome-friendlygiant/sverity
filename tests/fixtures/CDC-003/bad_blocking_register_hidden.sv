// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-003 ERROR
// Regression from the independent audit: this hazard must never be classified as safe.
// tmp is written with a blocking assignment in a clka process and read in a clkb process:
// it is a clka REGISTER. The model treats it as a combinational temporary, so the two real
// crossings (x_b: clkb -> tmp: clka -> q: clkb) disappear.
module top(input logic clka, clkb, d, output logic q);
  logic x_b, tmp;
  always_ff @(posedge clkb) x_b <= d;
  always_ff @(posedge clka) tmp = x_b;
  always_ff @(posedge clkb) q <= tmp;
endmodule
