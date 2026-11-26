// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 2-flop CDC data synchroniser from a source register reset by rst_a into a consumer reset only by rst_b still reports RDC-005 WARNING:
// synchronising resolves metastability, not the functional reset-domain crossing into an unreset consumer.
// revert: always_ff @(posedge clkb or negedge rst_b) if (!rst_b) dst_q <= 1'b0; else dst_q <= sync2_q; => always_ff @(posedge clkb or negedge rst_a) if (!rst_a) dst_q <= 1'b0; else dst_q <= sync2_q;
// revert-expect-none: RDC-005 WARNING
// expect: RDC-005 WARNING
module top(input logic clka, clkb, rst_a, rst_b, d, output logic q);
  logic src_q, sync1_q, sync2_q, dst_q;
  always_ff @(posedge clka or negedge rst_a)
    if (!rst_a) src_q <= 1'b0;
    else src_q <= d;
  always_ff @(posedge clkb or negedge rst_b)
    if (!rst_b) begin sync1_q <= 1'b0; sync2_q <= 1'b0; end
    else begin sync1_q <= src_q; sync2_q <= sync1_q; end
  always_ff @(posedge clkb or negedge rst_b) if (!rst_b) dst_q <= 1'b0; else dst_q <= sync2_q;
  assign q = dst_q;
endmodule
