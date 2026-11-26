// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An ordinary functional register in another clock domain sampling an independently reset source must still report RDC-005 WARNING.
// revert: always_ff @(posedge clkb or negedge rst_b) if (!rst_b) dst_q <= 1'b0; else dst_q <= src_q; => always_ff @(posedge clkb or negedge rst_a) if (!rst_a) dst_q <= 1'b0; else dst_q <= src_q;
// revert-expect-none: RDC-005 WARNING
// expect: RDC-005 WARNING
module top(input logic clka, clkb, rst_a, rst_b, d, output logic q);
  logic src_q, dst_q;
  always_ff @(posedge clka or negedge rst_a)
    if (!rst_a) src_q <= 1'b0;
    else src_q <= d;
  always_ff @(posedge clkb or negedge rst_b) if (!rst_b) dst_q <= 1'b0; else dst_q <= src_q;
  assign q = dst_q;
endmodule
