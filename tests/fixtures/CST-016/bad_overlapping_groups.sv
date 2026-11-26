// Two assert_together groups share the member `b`: groups must be disjoint, so both are decided conflicts.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/overlapping_groups.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 2
// expect-confidence: CST-016 established
module top(input logic clka, clkb, rst_a_n, rst_b_n, rst_c_n, d, output logic q, r, s);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
  always_ff @(posedge clka or negedge rst_c_n) if (!rst_c_n) s <= 1'b0; else s <= d;
endmodule
