// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The select sel is clocked by clk_u, which nothing drives, so its clock is unidentified; the rule does not stop there and still decides: sel's reset sources differ from the users' (r), so WARNING.
// expect: RDC-017 WARNING
module top(input logic rst_n, clka_u, d, sel_d, output logic q);
  logic clk_u, sel, r;
  always_ff @(posedge clk_u or negedge rst_n) if (!rst_n) sel <= 1'b0; else sel <= sel_d;
  assign r = rst_n | sel;
  always_ff @(posedge clka_u or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule
