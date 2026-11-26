// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Reset logic reads a clock net (clk_d, driven by divider div_q): RDC-007's logic walk stops at clock nets and does not walk back through clk_d to div_q.
// expect-none: RDC-007
module top(input logic clka, clkb, rst_n, en, d, output logic q, qd);
  logic div_q, clk_d, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) div_q <= 1'b0; else div_q <= ~div_q;
  assign clk_d = div_q;
  always_ff @(posedge clk_d or negedge rst_n) if (!rst_n) qd <= 1'b0; else qd <= d;
  assign r = !clk_d & en & rst_n;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
