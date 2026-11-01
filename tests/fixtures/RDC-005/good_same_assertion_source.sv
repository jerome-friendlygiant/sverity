// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Both domains are reset from the same port through their own synchronisers: same assertion source.
// expect-none: RDC-005
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic ra1, ra2, rb1, rb2, s, t1, t2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {ra2, ra1} <= 2'b0; else {ra2, ra1} <= {ra1, 1'b1};
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {rb2, rb1} <= 2'b0; else {rb2, rb1} <= {rb1, 1'b1};
  always_ff @(posedge clka or negedge ra2) if (!ra2) s <= 1'b0; else s <= d;
  always_ff @(posedge clkb or negedge rb2) if (!rb2) {t2, t1} <= 2'b0; else {t2, t1} <= {t1, s};
  assign q = t2;
endmodule
