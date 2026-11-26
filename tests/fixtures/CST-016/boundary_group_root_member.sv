// An assert_together member that is an internal root rather than a primary input: the group is Unapplied, and the
// primary input that is also named is left unused, so it is not reported as bound evidence.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/group_root_member.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clka or negedge a_rst) if (!a_rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
