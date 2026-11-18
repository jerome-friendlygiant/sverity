// sverity-args: --top top
// Stub is an elaboration constant: the select chooses rst_ni, so the net is a plain alias of the reset.
// expect-none: LNT-047
module top #(parameter bit Stub = 0)(input logic clk, rst_ni, d, output logic q);
  logic core_rst_n;
  assign core_rst_n = Stub ? 1'b0 : rst_ni;
  always_ff @(posedge clk or negedge core_rst_n) if (!core_rst_n) q <= 1'b0; else q <= d;
endmodule
