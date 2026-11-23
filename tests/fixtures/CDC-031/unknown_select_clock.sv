// sverity-args: --top top --sdc @DIR@/unknown.sdc
// The select register is clocked by an undeclared clock, so its relation to both mux leaves is unknown.
// expect: CDC-031 WARNING
// expect-unresolved: CDC-031
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, clkb, sel_clk, rst_n, d, sel, output logic q);
  logic sel_q, mclk;
  always_ff @(posedge sel_clk or negedge rst_n)
    if (!rst_n) sel_q <= 1'b0; else sel_q <= sel;
  assign mclk = sel_q ? clka : clkb;
  always_ff @(posedge mclk or negedge rst_n)
    if (!rst_n) q <= 1'b0; else q <= d;
endmodule
