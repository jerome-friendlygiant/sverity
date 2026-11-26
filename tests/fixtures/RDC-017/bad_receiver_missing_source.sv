// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The select sel is reset by r = rst_n | sel and aux_rst_n. p also uses aux_rst_n, so the receiver union equals the select's sources,
// but q is reset by r alone: sel's reset domain differs from q's, and the finding names q.
// revert: always_ff @(posedge clka or negedge r) if (!r) q <= 0; else q <= d; => always_ff @(posedge clka or negedge r or negedge aux_rst_n) if (!r) q <= 0; else if (!aux_rst_n) q <= 0; else q <= d;
// revert-expect-none: RDC-017 WARNING
// expect: RDC-017 WARNING
module top(input logic clka, rst_n, aux_rst_n, d, output logic q, p);
  logic sel;
  wire r = rst_n | sel;
  always_ff @(posedge clka or negedge r or negedge aux_rst_n)
    if (!r) sel <= 0; else if (!aux_rst_n) sel <= 0; else sel <= d;
  always_ff @(posedge clka or negedge r or negedge aux_rst_n)
    if (!r) p <= 0; else if (!aux_rst_n) p <= 0; else p <= d;
  always_ff @(posedge clka or negedge r) if (!r) q <= 0; else q <= d;
endmodule
