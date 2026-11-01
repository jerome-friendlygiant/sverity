// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Independent resets on two asynchronous clocks (the data crossing itself is CDC).
// expect: RDC-005 WARNING
module top(input logic clka, clkb, rst_a, rst_b, d, output logic q);
  logic s, t1, t2;
  always_ff @(posedge clka or negedge rst_a) if (!rst_a) s <= 1'b0; else s <= d;
  always_ff @(posedge clkb or negedge rst_b) if (!rst_b) {t2, t1} <= 2'b0; else {t2, t1} <= {t1, s};
  assign q = t2;
endmodule
