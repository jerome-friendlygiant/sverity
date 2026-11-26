// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// p resets on the posedge of aux_rst = ~aux_rst_n: the inverted copy of the source q and sel reset on (negedge aux_rst_n). It is
// the same assertion source, so the select sel is not in another reset domain (RDC-017).
// expect-none: RDC-017
module top(input logic clka, clkb, rst_n, aux_rst_n, d, output logic q, p);
  logic sel;
  wire r = rst_n | sel; wire aux_rst = ~aux_rst_n;
  always_ff @(posedge clka or negedge r or negedge aux_rst_n)
    if (!r) sel <= 0; else if (!aux_rst_n) sel <= 0; else sel <= d;
  always_ff @(posedge clka or negedge r or posedge aux_rst)
    if (!r) p <= 0; else if (aux_rst) p <= 0; else p <= d;
  always_ff @(posedge clka or negedge r or negedge aux_rst_n)
    if (!r) q <= 0; else if (!aux_rst_n) q <= 0; else q <= d;
endmodule
