// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Different clocks, but the source reset source is included in the destination reset sources.
// expect-none: RDC-005
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic s, t1, t2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) s <= 1'b0; else s <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {t2, t1} <= 2'b0; else {t2, t1} <= {t1, s};
  assign q = t2;
endmodule
