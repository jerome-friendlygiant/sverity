// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The source register's reset is a logic-built net (rst_a_n & en), not a plain reset, which looks undecidable; the spec has
// no Unknown outcome for RDC-004, so the rule still decides: its reset sources are not a subset of the destination's {rst_b_n}.
// expect: RDC-004 WARNING
module top(input logic clka, rst_a_n, en, rst_b_n, d, output logic q);
  logic r, s;
  assign r = rst_a_n & en;
  always_ff @(posedge clka or negedge r) if (!r) s <= 1'b0; else s <= d;
  always_ff @(posedge clka or negedge rst_b_n) if (!rst_b_n) q <= 1'b0; else q <= s;
endmodule
