// The order names clkb, but a later create_clock without -add replaces clkb on the same port. The clock clkb is not
// effective, so the witness is not decided: WARNING Unresolved and reset-contracts INCOMPLETE (CST-016 B1).
// sverity-args: --top top --sdc @DIR@/unknown_replaced_clock.sdc --config @DIR@/unknown_replaced_clock.json
// expect: CST-016 WARNING
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, s1, s2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge s2) if (!s2) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
