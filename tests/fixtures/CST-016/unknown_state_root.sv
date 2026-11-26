// An initial-asserted state is declared on an internal root: only a primary input can carry it, so the clause is Unapplied.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/state_root.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_rst;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clka or negedge a_rst) if (!a_rst) q <= 1'b0; else q <= d;
endmodule
