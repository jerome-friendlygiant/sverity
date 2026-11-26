// Two internal asynchronous reset roots with a declared release order. b_rst is a one-stage synchroniser of rst_n, not a chain
// reset by a_rst, so the RTL witness does not match: the clause is Unresolved and its scope is INCOMPLETE.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/root_order.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-confidence: CST-016 unresolved
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, b_rst;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) b_rst <= 1'b0; else b_rst <= 1'b1;
  always_ff @(posedge clka or negedge a_rst) if (!a_rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge b_rst) if (!b_rst) r <= 1'b0; else r <= d;
endmodule
