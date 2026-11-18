// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// `por_rst_n` and `rst_lc_n` are resets by name (a reset word followed only by polarity or domain words): the net is their combination, never
// a reset gated by functional signals.
// expect: RDC-008 WARNING
// expect-none: RDC-009 WARNING
module top(input logic clka, por_rst_n, rst_lc_n, d, output logic q);
  logic r;
  assign r = por_rst_n & rst_lc_n;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
