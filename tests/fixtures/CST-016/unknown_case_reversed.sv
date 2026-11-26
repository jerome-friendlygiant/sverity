// The RTL releases a under b (reversed), a Conflict. Under set_case_analysis every order is Unresolved, the contradiction
// included (CST-016 B7).
// sverity-args: --top top --sdc @DIR@/unknown_case_reversed.sdc --config @DIR@/unknown_case_reversed.json
// expect: CST-016 WARNING
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic b_rst, a1, a_rst;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_rst <= 1'b0; else b_rst <= 1'b1;
  always_ff @(posedge clkb or negedge b_rst) if (!b_rst) a1 <= 1'b0; else a1 <= 1'b1;
  always_ff @(posedge clkb or negedge b_rst) if (!b_rst) a_rst <= 1'b0; else a_rst <= a1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
