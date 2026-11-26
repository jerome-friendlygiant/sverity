// sverity-args: --top top --ignore-unknown-modules
// The asynchronous source opaque_rst_n comes from a missing module: the select sel and the register q have the same source
// sets, but the source has no modelled driver, so the domain comparison is Unresolved (RDC-017), not clean.
// expect-unresolved: RDC-017
module top(input logic clka, rst_n, d, output logic q);
  wire opaque_rst_n;
  missing u(.o(opaque_rst_n));
  logic sel;
  wire r = rst_n | sel;
  always_ff @(posedge clka or negedge opaque_rst_n or negedge r)
    if (!opaque_rst_n) sel <= 0; else if (!r) sel <= 0; else sel <= d;
  always_ff @(posedge clka or negedge opaque_rst_n or negedge r)
    if (!opaque_rst_n) q <= 0; else if (!r) q <= 0; else q <= d;
endmodule
