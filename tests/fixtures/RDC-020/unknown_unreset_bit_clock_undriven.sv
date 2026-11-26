// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Bit v[1] has no reset and its clock clk_u is undriven, so the clock is unidentified; the model still decides: v[1] is unreset and feeds q, so WARNING.
// expect: RDC-020 WARNING
module top(input logic clka_u, rst_n, input logic [1:0] d, output logic [1:0] q);
  logic clk_u;
  logic [1:0] v;
  always_ff @(posedge clka_u or negedge rst_n) if (!rst_n) v[0] <= 1'b0; else v[0] <= d[0];
  always_ff @(posedge clk_u) v[1] <= d[1];
  assign q = v;
endmodule
